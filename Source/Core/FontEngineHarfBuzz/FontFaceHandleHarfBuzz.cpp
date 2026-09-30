#include "FontFaceHandleHarfBuzz.h"
#include "../../../Include/RmlUi/Core/Log.h"
#include "../../../Include/RmlUi/Core/Math.h"
#include "../../../Include/RmlUi/Core/Profiling.h"
#include "../../../Include/RmlUi/Core/StringUtilities.h"
#include "FontFaceLayer.h"
#include "FontProvider.h"
#include "Rasterizer.h"
#include <algorithm>
#include <hb.h>
#include <numeric>
#include <utility>

namespace Rml {
namespace HarfBuzz {

	static bool IsControlCharacter(Character c)
	{
		return (char32_t)c < U' ' || ((char32_t)c >= U'\x7F' && (char32_t)c <= U'\x9F');
	}

	static int FixedToPixels(int value_26_6)
	{
		return (value_26_6 + 32) >> 6;
	}

	// Advances are rounded to whole pixels, like the hinted advances of the default font engine, so that the width of a string equals the sum of
	// the widths of its parts. Layout measures text word by word, while each line is rendered as a whole.
	static int RoundAdvance(int advance_26_6)
	{
		return FixedToPixels(advance_26_6) * 64;
	}

	static constexpr size_t width_cache_max_entries = 4096;

	static void BuildWidthCacheKey(String& key, StringView string, const TextShapingContext& text_shaping_context)
	{
		key.clear();
		const char context[] = {(char)text_shaping_context.text_direction, (char)text_shaping_context.font_kerning};
		key.append(context, sizeof(context));
		key.append(reinterpret_cast<const char*>(&text_shaping_context.letter_spacing), sizeof(text_shaping_context.letter_spacing));
		key += text_shaping_context.language;
		key += '\0';
		key.append(string.begin(), string.size());
	}

	static void GetTextShapingFeatures(const TextShapingContext& text_shaping_context, Vector<hb_feature_t>& shaping_features)
	{
		shaping_features.clear();

		// Kerning is enabled by default in HarfBuzz, so we need to explicitly disable it. Like the default font engine, 'auto' enables kerning.
		if (text_shaping_context.font_kerning == Style::FontKerning::None)
		{
			shaping_features.emplace_back();
			hb_feature_from_string("kern=0", -1, &shaping_features.back());
		}
	}

	FontFaceHandleHarfBuzz::FontFaceHandleHarfBuzz()
	{
		base_layer = nullptr;
		metrics = {};
		raster_face = 0;
		hb_font = nullptr;
		shaping_buffer = nullptr;
	}

	FontFaceHandleHarfBuzz::~FontFaceHandleHarfBuzz()
	{
		hb_buffer_destroy(shaping_buffer);
		hb_font_destroy(hb_font);

		glyphs.clear();
		layers.clear();
	}

	bool FontFaceHandleHarfBuzz::Initialize(Rasterizer::FaceHandle face, hb_face_t* hb_face, int font_size, bool load_default_glyphs)
	{
		raster_face = face;

		RMLUI_ASSERTMSG(layer_configurations.empty(), "Initialize must only be called once.");

		// Shape with HarfBuzz's own OpenType font functions, scaled so that positions are in 26.6 fixed-point pixels like FreeType's. The
		// advances are unhinted.
		hb_font = hb_font_create(hb_face);
		RMLUI_ASSERT(hb_font != nullptr);
		hb_font_set_scale(hb_font, font_size * 64, font_size * 64);
		hb_font_set_ppem(hb_font, (unsigned int)font_size, (unsigned int)font_size);
		hb_font_set_ptem(hb_font, (float)font_size);

		// Match the named instance of a variable font that the rasterizer loaded, if any.
		const unsigned int named_instance_index = Rasterizer::GetNamedInstanceIndex(raster_face);
		if (named_instance_index > 0)
			hb_font_set_var_named_instance(hb_font, named_instance_index - 1);

		if (!Rasterizer::InitialiseFaceHandle(raster_face, hb_font, font_size, glyphs, metrics, load_default_glyphs))
			return false;

		shaping_buffer = hb_buffer_create();
		RMLUI_ASSERT(shaping_buffer != nullptr);

		// Generate the default layer and layer configuration.
		base_layer = GetOrCreateLayer(nullptr);
		layer_configurations.push_back(LayerConfiguration{base_layer});

		return true;
	}

	const FontMetrics& FontFaceHandleHarfBuzz::GetFontMetrics() const
	{
		return metrics;
	}

	const FontGlyphMap& FontFaceHandleHarfBuzz::GetGlyphs() const
	{
		return glyphs;
	}

	const FallbackFontGlyphMap& FontFaceHandleHarfBuzz::GetFallbackGlyphs() const
	{
		return fallback_glyphs;
	}

	const FallbackFontClusterGlyphsMap& FontFaceHandleHarfBuzz::GetFallbackClusterGlyphs() const
	{
		return fallback_cluster_glyphs;
	}

	int FontFaceHandleHarfBuzz::GetStringWidth(StringView string, const TextShapingContext& text_shaping_context,
		const LanguageDataMap& registered_languages, Character /*prior_character*/)
	{
		RMLUI_ZoneScoped;

		BuildWidthCacheKey(width_cache_key, string, text_shaping_context);
		auto it = width_cache.find(width_cache_key);
		if (it != width_cache.end())
			return it->second;

		const int width = Math::Max(FixedToPixels(ShapeString(string, text_shaping_context, registered_languages, nullptr)), 0);

		if (width_cache.size() >= width_cache_max_entries)
			width_cache.clear();
		width_cache.emplace(width_cache_key, width);

		return width;
	}

	int FontFaceHandleHarfBuzz::GenerateLayerConfiguration(const FontEffectList& font_effects)
	{
		if (font_effects.empty())
			return 0;

		// Check each existing configuration for a match with this arrangement of effects.
		int configuration_index = 1;
		for (; configuration_index < (int)layer_configurations.size(); ++configuration_index)
		{
			const LayerConfiguration& configuration = layer_configurations[configuration_index];

			// Check the size is correct. For a match, there should be one layer in the configuration
			// plus an extra for the base layer.
			if (configuration.size() != font_effects.size() + 1)
				continue;

			// Check through each layer, checking it was created by the same effect as the one we're
			// checking.
			size_t effect_index = 0;
			for (size_t i = 0; i < configuration.size(); ++i)
			{
				// Skip the base layer ...
				if (configuration[i]->GetFontEffect() == nullptr)
					continue;

				// If the ith layer's effect doesn't match the equivalent effect, then this
				// configuration can't match.
				if (configuration[i]->GetFontEffect() != font_effects[effect_index].get())
					break;

				// Check the next one ...
				++effect_index;
			}

			if (effect_index == font_effects.size())
				return configuration_index;
		}

		// No match, so we have to generate a new layer configuration.
		layer_configurations.push_back(LayerConfiguration());
		LayerConfiguration& layer_configuration = layer_configurations.back();

		bool added_base_layer = false;

		for (size_t i = 0; i < font_effects.size(); ++i)
		{
			if (!added_base_layer && font_effects[i]->GetLayer() == FontEffect::Layer::Front)
			{
				layer_configuration.push_back(base_layer);
				added_base_layer = true;
			}

			FontFaceLayer* new_layer = GetOrCreateLayer(font_effects[i]);
			layer_configuration.push_back(new_layer);
		}

		// Add the base layer now if we still haven't added it.
		if (!added_base_layer)
			layer_configuration.push_back(base_layer);

		return (int)(layer_configurations.size() - 1);
	}

	bool FontFaceHandleHarfBuzz::GenerateLayerTexture(Vector<byte>& texture_data, Vector2i& texture_dimensions, const FontEffect* font_effect,
		int texture_id, int handle_version) const
	{
		if (handle_version != version)
		{
			RMLUI_ERRORMSG("While generating font layer texture: Handle version mismatch in texture vs font-face.");
			return false;
		}

		auto it = std::find_if(layers.begin(), layers.end(), [font_effect](const EffectLayerPair& pair) { return pair.font_effect == font_effect; });

		if (it == layers.end())
		{
			RMLUI_ERRORMSG("While generating font layer texture: Layer id not found.");
			return false;
		}

		return it->layer->GenerateTexture(texture_data, texture_dimensions, texture_id,
			FontGlyphMaps{&glyphs, &fallback_glyphs, &fallback_cluster_glyphs_lookup});
	}

	int FontFaceHandleHarfBuzz::GenerateString(RenderManager& render_manager, TexturedMeshList& mesh_list, StringView string, const Vector2f position,
		const ColourbPremultiplied colour, const float opacity, const TextShapingContext& text_shaping_context,
		const LanguageDataMap& registered_languages, const int layer_configuration_index)
	{
		RMLUI_ZoneScoped;

		RMLUI_ASSERT(layer_configuration_index >= 0);
		RMLUI_ASSERT(layer_configuration_index < (int)layer_configurations.size());

		// Shape the string once for all layers. Shaping appends any new glyphs, so update the layers afterwards to render them right away.
		const int width = ShapeString(string, text_shaping_context, registered_languages, &shaped_glyphs);

		UpdateLayersOnDirty();

		// Fetch the requested configuration and generate the geometry for each one.
		const LayerConfiguration& layer_configuration = layer_configurations[layer_configuration_index];

		// Each texture represents one geometry.
		const int num_geometries = std::accumulate(layer_configuration.begin(), layer_configuration.end(), 0,
			[](int sum, const FontFaceLayer* layer) { return sum + layer->GetNumTextures(); });

		mesh_list.resize(num_geometries);

		int geometry_index = 0;
		for (size_t layer_index = 0; layer_index < layer_configuration.size(); ++layer_index)
		{
			FontFaceLayer* layer = layer_configuration[layer_index];

			ColourbPremultiplied layer_colour;
			if (layer == base_layer)
				layer_colour = colour;
			else
				layer_colour = layer->GetColour(opacity);

			const int num_textures = layer->GetNumTextures();
			if (num_textures == 0)
				continue;

			RMLUI_ASSERT(geometry_index + num_textures <= (int)mesh_list.size());

			// Set the mesh and textures to the geometries.
			for (int tex_index = 0; tex_index < num_textures; ++tex_index)
				mesh_list[geometry_index + tex_index].texture = layer->GetTexture(render_manager, tex_index);

			mesh_list[geometry_index].mesh.indices.reserve(shaped_glyphs.size() * 6);
			mesh_list[geometry_index].mesh.vertices.reserve(shaped_glyphs.size() * 4);

			for (const ShapedGlyph& shaped_glyph : shaped_glyphs)
			{
				ColourbPremultiplied glyph_color = layer_colour;
				// Use white vertex colors on RGB glyphs.
				if (layer == base_layer && shaped_glyph.is_color)
					glyph_color = ColourbPremultiplied(layer_colour.alpha, layer_colour.alpha);

				const Vector2f glyph_position = position + Vector2f(shaped_glyph.position) * (1.f / 64.f);
				layer->GenerateGeometry(&mesh_list[geometry_index], shaped_glyph.glyph_index, shaped_glyph.character, shaped_glyph.is_cluster,
					glyph_position, glyph_color);
			}

			geometry_index += num_textures;
		}

		return Math::Max(FixedToPixels(width), 0);
	}

	int FontFaceHandleHarfBuzz::ShapeString(StringView string, const TextShapingContext& text_shaping_context,
		const LanguageDataMap& registered_languages, Vector<ShapedGlyph>* out_shaped_glyphs)
	{
		if (out_shaped_glyphs)
			out_shaped_glyphs->clear();

		Vector<hb_feature_t> shaping_features;
		GetTextShapingFeatures(text_shaping_context, shaping_features);

		FillTextShapingBuffer(shaping_buffer, string, text_shaping_context, registered_languages);
		hb_shape(hb_font, shaping_buffer, shaping_features.data(), (unsigned int)shaping_features.size());

		unsigned int glyph_count = 0;
		const hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(shaping_buffer, &glyph_count);
		const hb_glyph_position_t* glyph_positions = hb_buffer_get_glyph_positions(shaping_buffer, nullptr);

		// Whole pixels of letter spacing, like the default font engine.
		const int letter_spacing = (int)text_shaping_context.letter_spacing * 64;
		int pen = 0;

		for (int g = 0; g < (int)glyph_count; ++g)
		{
			Character character = StringUtilities::ToCharacter(string.begin() + glyph_info[g].cluster, string.end());

			// Don't render control characters.
			if (IsControlCharacter(character))
				continue;

			const FontGlyphIndex glyph_index = glyph_info[g].codepoint;
			// HarfBuzz offsets are y-up, while we render y-down.
			const Vector2i offset(glyph_positions[g].x_offset, -glyph_positions[g].y_offset);

			if (glyph_index == 0)
			{
				// Check to see if the glyph is the start of an unsupported multi-character cluster.
				int cluster_codepoint_count = 0;
				StringView cluster_string = GetCurrentClusterString(glyph_info, (int)glyph_count, g, character, string, cluster_codepoint_count);

				if (cluster_codepoint_count > 1)
				{
					// Unsupported cluster detected; use fallback cluster glyph if one is available.
					const Vector<FontClusterGlyphData>* cluster_glyphs =
						GetOrAppendFallbackClusterGlyphs(cluster_string, text_shaping_context, registered_languages, shaping_features);

					if (cluster_glyphs)
					{
						// Lay out the fallback glyphs with their unshaped advances.
						for (const FontClusterGlyphData& cluster_glyph : *cluster_glyphs)
						{
							const FontGlyph& bitmap = cluster_glyph.glyph_data.bitmap;
							if (out_shaped_glyphs)
								out_shaped_glyphs->push_back(ShapedGlyph{cluster_glyph.glyph_index, cluster_glyph.glyph_data.character, true,
									bitmap.color_format == ColorFormat::RGBA8, Vector2i(pen + offset.x, offset.y)});
							pen += bitmap.advance * 64 + letter_spacing;
						}

						g += cluster_codepoint_count - 1;
						continue;
					}
				}
			}

			const FontGlyph* glyph = GetOrAppendGlyph(glyph_index, character);
			if (!glyph)
				continue;

			if (out_shaped_glyphs)
				out_shaped_glyphs->push_back(
					ShapedGlyph{glyph_index, character, false, glyph->color_format == ColorFormat::RGBA8, Vector2i(pen + offset.x, offset.y)});

			// Use the unshaped advance for unsupported characters, which are rendered from fallback fonts or as the replacement character.
			pen += (glyph_index != 0 ? RoundAdvance(glyph_positions[g].x_advance) : glyph->advance * 64) + letter_spacing;
		}

		return pen;
	}

	bool FontFaceHandleHarfBuzz::UpdateLayersOnDirty()
	{
		bool result = false;

		// If we are dirty, regenerate all the layers and increment the version
		if (is_layers_dirty && base_layer)
		{
			is_layers_dirty = false;
			++version;

			// Regenerate all the layers.
			// Note: The layer regeneration needs to happen in the order in which the layers were created,
			// otherwise we may end up cloning a layer which has not yet been regenerated. This means trouble!
			for (auto& pair : layers)
			{
				GenerateLayer(pair.layer.get());
			}

			result = true;
		}

		return result;
	}

	int FontFaceHandleHarfBuzz::GetVersion() const
	{
		return version;
	}

	bool FontFaceHandleHarfBuzz::AppendGlyph(FontGlyphIndex glyph_index, Character character)
	{
		bool result = Rasterizer::AppendGlyph(raster_face, hb_font, metrics.size, glyph_index, character, glyphs);
		return result;
	}

	bool FontFaceHandleHarfBuzz::AppendFallbackGlyph(Character& character)
	{
		const int num_fallback_faces = FontProvider::CountFallbackFontFaces();
		for (int i = 0; i < num_fallback_faces; i++)
		{
			FontFaceHandleHarfBuzz* fallback_face = FontProvider::GetFallbackFontFace(i, metrics.size);
			if (!fallback_face || fallback_face == this)
				continue;

			hb_codepoint_t character_index = 0;
			if (!hb_font_get_nominal_glyph(fallback_face->hb_font, (hb_codepoint_t)character, &character_index) || character_index == 0)
				continue;

			const FontGlyph* glyph = fallback_face->GetOrAppendGlyph(character_index, character, false);
			if (glyph)
			{
				// Insert the new glyph into our own set of fallback glyphs
				auto pair = fallback_glyphs.emplace(character, glyph->WeakCopy());
				if (pair.second)
					is_layers_dirty = true;

				return true;
			}
		}

		return false;
	}

	const FontGlyph* FontFaceHandleHarfBuzz::GetOrAppendGlyph(FontGlyphIndex glyph_index, Character& character, bool look_in_fallback_fonts)
	{
		if (glyph_index == 0 && look_in_fallback_fonts && character != Character::Replacement)
		{
			auto fallback_glyph = GetOrAppendFallbackGlyph(character);
			if (fallback_glyph != nullptr)
				return fallback_glyph;
		}

		auto glyph_location = glyphs.find(glyph_index);
		if (glyph_location == glyphs.cend())
		{
			// Glyph index zero is always present as the replacement glyph, so only build glyphs for other indices.
			if (glyph_index == 0 || !AppendGlyph(glyph_index, character))
				return nullptr;

			glyph_location = glyphs.find(glyph_index);
			if (glyph_location == glyphs.cend())
			{
				RMLUI_ERROR;
				return nullptr;
			}

			is_layers_dirty = true;
		}

		if (glyph_index == 0)
			character = Character::Replacement;
		else if (character != glyph_location->second.character)
			character = glyph_location->second.character;

		const FontGlyph* glyph = &glyph_location->second.bitmap;
		return glyph;
	}

	const FontGlyph* FontFaceHandleHarfBuzz::GetOrAppendFallbackGlyph(Character& character)
	{
		auto fallback_glyph_location = fallback_glyphs.find(character);
		if (fallback_glyph_location != fallback_glyphs.cend())
			return &fallback_glyph_location->second;

		bool result = AppendFallbackGlyph(character);

		if (result)
		{
			fallback_glyph_location = fallback_glyphs.find(character);
			if (fallback_glyph_location == fallback_glyphs.cend())
			{
				RMLUI_ERROR;
				return nullptr;
			}

			is_layers_dirty = true;
		}
		else
			return nullptr;

		const FontGlyph* fallback_glyph = &fallback_glyph_location->second;
		return fallback_glyph;
	}

	bool FontFaceHandleHarfBuzz::AppendFallbackClusterGlyphs(StringView cluster, const TextShapingContext& text_shaping_context,
		const LanguageDataMap& registered_languages, Span<const hb_feature_t> text_shaping_features)
	{
		hb_buffer_t* cluster_buffer = hb_buffer_create();
		RMLUI_ASSERT(cluster_buffer != nullptr);

		bool result = false;

		// Iterate through all available fallback font faces.
		const int num_fallback_faces = FontProvider::CountFallbackFontFaces();
		for (int i = 0; i < num_fallback_faces && !result; i++)
		{
			FontFaceHandleHarfBuzz* fallback_face = FontProvider::GetFallbackFontFace(i, metrics.size);
			if (!fallback_face || fallback_face == this)
				continue;

			// Insert the cluster into a shaping buffer and perform text shaping.
			FillTextShapingBuffer(cluster_buffer, cluster, text_shaping_context, registered_languages);
			hb_shape(fallback_face->hb_font, cluster_buffer, text_shaping_features.data(), (unsigned int)text_shaping_features.size());

			unsigned int glyph_count = 0;
			hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(cluster_buffer, &glyph_count);
			if (glyph_count == 0)
				continue;

			const bool is_right_to_left = (hb_buffer_get_direction(cluster_buffer) == HB_DIRECTION_RTL);

			Vector<FontClusterGlyphData> cluster_glyphs;
			cluster_glyphs.reserve((size_t)glyph_count);

			int glyph_info_index_offset = is_right_to_left ? (int)glyph_count - 1 : 0;
			int cluster_string_offset = 0;
			bool has_supported_glyph = false;

			// Create the cluster glyphs.
			for (int g = 0; g < (int)glyph_count; ++g)
			{
				int glyph_info_index = g + glyph_info_index_offset;
				RMLUI_ASSERT(glyph_info_index < (int)glyph_count);

				// Reverse the order of the glyphs in right-to-left text.
				if (is_right_to_left)
					glyph_info_index_offset -= 2;

				Character character = StringUtilities::ToCharacter(cluster.begin() + cluster_string_offset, cluster.end());
				const FontGlyph* glyph = fallback_face->GetOrAppendGlyph(glyph_info[glyph_info_index].codepoint, character, false);
				if (glyph && glyph->bitmap_data && glyph->bitmap_dimensions.x > 0 && glyph->bitmap_dimensions.y > 0)
				{
					cluster_glyphs.push_back(
						FontClusterGlyphData{glyph_info[glyph_info_index].codepoint, FontGlyphData{glyph->WeakCopy(), character}});
					if (!has_supported_glyph && glyph_info[glyph_info_index].codepoint != 0)
						has_supported_glyph = true;
				}

				cluster_string_offset += (int)StringUtilities::BytesUTF8(character);
				RMLUI_ASSERT(cluster_string_offset <= (int)cluster.size());
			}

			if (cluster_glyphs.empty() || !has_supported_glyph)
				continue;

			// Insert the cluster glyphs into our own set of fallback cluster glyphs.
			auto pair = fallback_cluster_glyphs.emplace(cluster, std::move(cluster_glyphs));
			if (pair.second)
			{
				is_layers_dirty = true;

				// Populate quick-lookup glyph map to glyph search times during rendering.
				for (const auto& cluster_glyph : pair.first->second)
				{
					uint64_t cluster_glyph_id = GetFallbackFontClusterGlyphLookupID(cluster_glyph.glyph_index, cluster_glyph.glyph_data.character);
					fallback_cluster_glyphs_lookup.emplace(cluster_glyph_id, &cluster_glyph.glyph_data.bitmap);
				}
			}

			result = true;
		}

		hb_buffer_destroy(cluster_buffer);

		return result;
	}

	const Vector<FontClusterGlyphData>* FontFaceHandleHarfBuzz::GetOrAppendFallbackClusterGlyphs(StringView cluster,
		const TextShapingContext& text_shaping_context, const LanguageDataMap& registered_languages, Span<const hb_feature_t> text_shaping_features)
	{
		String cluster_string(cluster);
		auto fallback_cluster_glyphs_location = fallback_cluster_glyphs.find(cluster_string);
		if (fallback_cluster_glyphs_location != fallback_cluster_glyphs.cend())
		{
			return &fallback_cluster_glyphs_location->second;
		}

		bool result = AppendFallbackClusterGlyphs(cluster, text_shaping_context, registered_languages, text_shaping_features);

		if (result)
		{
			fallback_cluster_glyphs_location = fallback_cluster_glyphs.find(cluster_string);
			if (fallback_cluster_glyphs_location == fallback_cluster_glyphs.cend())
			{
				RMLUI_ERROR;
				return nullptr;
			}

			is_layers_dirty = true;
		}
		else
			return nullptr;

		const Vector<FontClusterGlyphData>* result_glyphs = &fallback_cluster_glyphs_location->second;
		return result_glyphs;
	}

	FontFaceLayer* FontFaceHandleHarfBuzz::GetOrCreateLayer(const SharedPtr<const FontEffect>& font_effect)
	{
		// Search for the font effect layer first, it may have been instanced before as part of a different configuration.
		const FontEffect* font_effect_ptr = font_effect.get();
		auto it = std::find_if(layers.begin(), layers.end(),
			[font_effect_ptr](const EffectLayerPair& pair) { return pair.font_effect == font_effect_ptr; });

		if (it != layers.end())
			return it->layer.get();

		// No existing effect matches, generate a new layer for the effect.
		layers.push_back(EffectLayerPair{font_effect_ptr, nullptr});
		auto& layer = layers.back().layer;

		layer = MakeUnique<FontFaceLayer>(font_effect);
		GenerateLayer(layer.get());

		return layer.get();
	}

	bool FontFaceHandleHarfBuzz::GenerateLayer(FontFaceLayer* layer)
	{
		RMLUI_ASSERT(layer);
		const FontEffect* font_effect = layer->GetFontEffect();
		bool result = false;

		if (!font_effect)
		{
			result = layer->Generate(this);
		}
		else
		{
			// Determine which, if any, layer the new layer should copy its geometry and textures from.
			FontFaceLayer* clone = nullptr;
			bool clone_glyph_origins = true;
			String generation_key;
			size_t fingerprint = font_effect->GetFingerprint();

			if (!font_effect->HasUniqueTexture())
			{
				clone = base_layer;
				clone_glyph_origins = false;
			}
			else
			{
				auto cache_iterator = layer_cache.find(fingerprint);
				if (cache_iterator != layer_cache.end() && cache_iterator->second != layer)
					clone = cache_iterator->second;
			}

			// Create a new layer.
			result = layer->Generate(this, clone, clone_glyph_origins);

			// Cache the layer in the layer cache if it generated its own textures (ie, didn't clone).
			if (!clone)
				layer_cache[fingerprint] = layer;
		}

		return result;
	}

	void FontFaceHandleHarfBuzz::FillTextShapingBuffer(hb_buffer_t* buffer, StringView string, const TextShapingContext& text_shaping_context,
		const LanguageDataMap& registered_languages) const
	{
		hb_buffer_clear_contents(buffer);

		// Set the buffer's language based on the value of the element's 'lang' attribute.
		hb_buffer_set_language(buffer, hb_language_from_string(text_shaping_context.language.c_str(), -1));

		// A registered language determines the script, and the text-flow direction unless one is set by the 'dir' attribute.
		auto registered_language_location = registered_languages.find(text_shaping_context.language);
		const LanguageData* registered_language =
			registered_language_location != registered_languages.cend() ? &registered_language_location->second : nullptr;
		if (registered_language)
			hb_buffer_set_script(buffer, hb_script_from_string(registered_language->script_code.c_str(), -1));

		switch (text_shaping_context.text_direction)
		{
		case Style::Direction::Auto:
			if (registered_language)
				hb_buffer_set_direction(buffer,
					registered_language->text_flow_direction == TextFlowDirection::RightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
			break;
		case Style::Direction::Ltr: hb_buffer_set_direction(buffer, HB_DIRECTION_LTR); break;
		case Style::Direction::Rtl: hb_buffer_set_direction(buffer, HB_DIRECTION_RTL); break;
		}

		// The string is shaped as a whole text, which matters for scripts that join characters, such as Arabic.
		hb_buffer_set_flags(buffer, (hb_buffer_flags_t)(HB_BUFFER_FLAG_BOT | HB_BUFFER_FLAG_EOT));
		hb_buffer_add_utf8(buffer, string.begin(), (int)string.size(), 0, (int)string.size());

		// Fill in any properties not set above: the script from the first character with a strong script, the direction from the script, and
		// the language from the system locale.
		hb_buffer_guess_segment_properties(buffer);
	}

	StringView FontFaceHandleHarfBuzz::GetCurrentClusterString(const hb_glyph_info_t* glyph_info, int glyph_count, int glyph_index,
		Character first_character, StringView string, int& cluster_codepoint_count) const
	{
		unsigned int cluster_index = glyph_info[glyph_index].cluster;
		cluster_codepoint_count = 1;
		int cluster_offset = glyph_index + 1;
		int cluster_string_size = (int)StringUtilities::BytesUTF8(first_character);

		// Continue counting characters that are part of the same cluster.
		while (cluster_offset < glyph_count && glyph_info[cluster_offset].cluster == cluster_index)
		{
			Character current_cluster_character =
				StringUtilities::ToCharacter(string.begin() + glyph_info[glyph_index].cluster + cluster_string_size, string.end());
			cluster_string_size += (int)StringUtilities::BytesUTF8(current_cluster_character);

			++cluster_codepoint_count;
			++cluster_offset;
		}

		return StringView(string.begin() + glyph_info[glyph_index].cluster, string.begin() + glyph_info[glyph_index].cluster + cluster_string_size);
	}

} // namespace HarfBuzz
} // namespace Rml
