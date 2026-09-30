#pragma once

#include "../../../Include/RmlUi/Core/FontEffect.h"
#include "../../../Include/RmlUi/Core/FontMetrics.h"
#include "../../../Include/RmlUi/Core/Geometry.h"
#include "../../../Include/RmlUi/Core/TextShapingContext.h"
#include "../../../Include/RmlUi/Core/Traits.h"
#include "../FontEngineDefault/FontTypes.h"
#include "FontFaceLayer.h"
#include "FontGlyph.h"
#include "LanguageData.h"

struct hb_buffer_t;
struct hb_face_t;
struct hb_feature_t;
struct hb_font_t;
struct hb_glyph_info_t;

namespace Rml {
namespace HarfBuzz {

	/**
	    A font face at a given size, shaping text through HarfBuzz and rasterizing glyphs through FreeType.
	 */
	class FontFaceHandleHarfBuzz : public NonCopyMoveable {
	public:
		FontFaceHandleHarfBuzz();
		~FontFaceHandleHarfBuzz();

		/// @param[in] face The FreeType face used to rasterize glyphs.
		/// @param[in] hb_face The HarfBuzz face used to shape text, shared with the handles of the other sizes of this face.
		bool Initialize(FontFaceHandleFreetype face, hb_face_t* hb_face, int font_size, bool load_default_glyphs);

		const FontMetrics& GetFontMetrics() const;

		const FontGlyphMap& GetGlyphs() const;
		const FallbackFontGlyphMap& GetFallbackGlyphs() const;
		const FallbackFontClusterGlyphsMap& GetFallbackClusterGlyphs() const;

		/// Returns the width a string will take up if rendered with this handle.
		/// @param[in] string The string to measure.
		/// @param[in] text_shaping_context Extra parameters that provide context for text shaping.
		/// @param[in] registered_languages A list of languages registered in the font engine interface.
		/// @param[in] prior_character Unused, shaping only considers the string itself.
		/// @return The width, in pixels, this string will occupy if rendered with this handle.
		int GetStringWidth(StringView string, const TextShapingContext& text_shaping_context, const LanguageDataMap& registered_languages,
			Character prior_character = Character::Null);

		/// Generates, if required, the layer configuration for a given list of font effects.
		/// @param[in] font_effects The list of font effects to generate the configuration for.
		/// @return The index to use when generating geometry using this configuration.
		int GenerateLayerConfiguration(const FontEffectList& font_effects);
		/// Generates the texture data for a layer (for the texture database).
		/// @param[out] texture_data The generated texture data.
		/// @param[out] texture_dimensions The dimensions of the texture.
		/// @param[in] font_effect The font effect used for the layer.
		/// @param[in] texture_id The index of the texture within the layer to generate.
		/// @param[in] handle_version The version of the handle data. Function returns false if out of date.
		bool GenerateLayerTexture(Vector<byte>& texture_data, Vector2i& texture_dimensions, const FontEffect* font_effect, int texture_id,
			int handle_version) const;

		/// Generates the geometry required to render a single line of text.
		/// @param[in] render_manager The render manager responsible for rendering the string.
		/// @param[out] mesh_list A list to place the new meshes into.
		/// @param[in] string The string to render.
		/// @param[in] position The position of the baseline of the first character to render.
		/// @param[in] colour The colour to render the text.
		/// @param[in] opacity The opacity of the text, should be applied to font effects.
		/// @param[in] text_shaping_context Extra parameters that provide context for text shaping.
		/// @param[in] registered_languages A list of languages registered in the font engine interface.
		/// @param[in] layer_configuration Face configuration index to use for generating string.
		/// @return The width, in pixels, of the string geometry.
		int GenerateString(RenderManager& render_manager, TexturedMeshList& mesh_list, StringView string, Vector2f position,
			ColourbPremultiplied colour, float opacity, const TextShapingContext& text_shaping_context, const LanguageDataMap& registered_languages,
			int layer_configuration = 0);

		/// Version is changed whenever the layers are dirtied, requiring regeneration of string geometry.
		int GetVersion() const;

	private:
		// A glyph positioned by shaping, with its position relative to the string origin in 26.6 fixed-point pixels.
		struct ShapedGlyph {
			FontGlyphIndex glyph_index;
			Character character;
			bool is_cluster;
			bool is_color;
			Vector2i position;
		};

		/// Shapes a string, appending any glyphs it uses that are not yet built.
		/// @param[out] shaped_glyphs The positioned glyphs of the string, or nullptr if only the width is needed.
		/// @return The advance of the string, in 26.6 fixed-point pixels.
		int ShapeString(StringView string, const TextShapingContext& text_shaping_context, const LanguageDataMap& registered_languages,
			Vector<ShapedGlyph>* shaped_glyphs);

		// Build and append glyph to 'glyphs'.
		bool AppendGlyph(FontGlyphIndex glyph_index, Character character);

		// Build and append fallback glyph to 'fallback_glyphs'.
		bool AppendFallbackGlyph(Character& character);

		/// Retrieve a glyph from the given code index, building and appending a new glyph if not already built.
		/// @param[in] glyph_index  The glyph index.
		/// @param[in-out] character  The character codepoint, can be changed e.g. to the replacement character if no glyph is found.
		/// @param[in] look_in_fallback_fonts  Look for the glyph in fallback fonts if not found locally, adding it to our fallback glyph map.
		/// @return The font glyph for the returned glyph index.
		const FontGlyph* GetOrAppendGlyph(FontGlyphIndex glyph_index, Character& character, bool look_in_fallback_fonts = true);

		/// Retrieve a fallback glyph from the given character, building and appending a new fallback glyph if not already built.
		/// @param[in-out] character  The character codepoint, can be changed e.g. to the replacement character if no glyph is found.
		/// @return The fallback font glyph for character.
		const FontGlyph* GetOrAppendFallbackGlyph(Character& character);

		// Build and append fallback cluster glyph to 'fallback_cluster_glyphs'.
		bool AppendFallbackClusterGlyphs(StringView cluster, const TextShapingContext& text_shaping_context,
			const LanguageDataMap& registered_languages, Span<const hb_feature_t> text_shaping_features);

		/// Retrieve a fallback cluster glyph from the given cluster and text-shaping/language data, building and appending a new fallback cluster
		/// glyph if not already built.
		/// @param[in] cluster  The cluster.
		/// @param[in] text_shaping_context  Extra parameters that provide context for text shaping.
		/// @param[in] registered_languages  A list of languages registered in the font engine interface.
		/// @param[in] text_shaping_features  A list of OpenType feature settings used for text shaping.
		/// @return The fallback glyphs of the cluster.
		const Vector<FontClusterGlyphData>* GetOrAppendFallbackClusterGlyphs(StringView cluster, const TextShapingContext& text_shaping_context,
			const LanguageDataMap& registered_languages, Span<const hb_feature_t> text_shaping_features);

		// Regenerate layers if dirty, such as after adding new glyphs.
		bool UpdateLayersOnDirty();

		// Create a new layer from the given font effect if it does not already exist.
		FontFaceLayer* GetOrCreateLayer(const SharedPtr<const FontEffect>& font_effect);

		// (Re-)generate a layer in this font face handle.
		bool GenerateLayer(FontFaceLayer* layer);

		/// Fills a shaping buffer with a string and its segment properties.
		/// @param[in] shaping_buffer  The shaping buffer to be filled, it is cleared first.
		/// @param[in] string  The string currently being measured/rendered.
		/// @param[in] text_shaping_context  Extra parameters that provide context for text shaping.
		/// @param[in] registered_languages  A list of languages registered in the font engine interface.
		void FillTextShapingBuffer(hb_buffer_t* shaping_buffer, StringView string, const TextShapingContext& text_shaping_context,
			const LanguageDataMap& registered_languages) const;

		/// Creates a cluster string from shaped glyph info and index.
		/// @param[in] glyph_info  The shaped glyph info list (supplied by HarfBuzz).
		/// @param[in] glyph_count  The number of shaped glyphs in glyph_info.
		/// @param[in] glyph_index  The current glyph index.
		/// @param[in] first_character  The first character of the cluster.
		/// @param[in] string  The string currently being measured/rendered.
		/// @param[out] cluster_codepoint_count  The number of codepoints in the cluster (which may differ from the length of the returned string).
		/// @return A UTF8 string built from all codepoints in the current glyph cluster.
		StringView GetCurrentClusterString(const hb_glyph_info_t* glyph_info, int glyph_count, int glyph_index, Character first_character,
			StringView string, int& cluster_codepoint_count) const;

		FontGlyphMap glyphs;
		FallbackFontGlyphMap fallback_glyphs;

		FallbackFontClusterGlyphsMap fallback_cluster_glyphs;
		FallbackFontClusterGlyphLookupMap fallback_cluster_glyphs_lookup;

		struct EffectLayerPair {
			const FontEffect* font_effect;
			UniquePtr<FontFaceLayer> layer;
		};
		using FontLayerMap = Vector<EffectLayerPair>;
		using FontLayerCache = SmallUnorderedMap<size_t, FontFaceLayer*>;
		using LayerConfiguration = Vector<FontFaceLayer*>;
		using LayerConfigurationList = Vector<LayerConfiguration>;

		// The list of all font layers, index by the effect that instanced them.
		FontFaceLayer* base_layer;
		FontLayerMap layers;
		// Each font layer that generated geometry or textures, indexed by the font-effect's fingerprint key.
		FontLayerCache layer_cache;

		bool is_layers_dirty = false;
		int version = 0;

		// All configurations currently in use on this handle. New configurations will be generated as required.
		LayerConfigurationList layer_configurations;

		FontMetrics metrics;

		FontFaceHandleFreetype ft_face;
		hb_font_t* hb_font;

		// Reused between calls to avoid allocating for every string.
		hb_buffer_t* shaping_buffer;
		Vector<ShapedGlyph> shaped_glyphs;

		// Layout measures the same words again on every pass, and shaping costs much more than summing advances. Widths only depend on the
		// string and its shaping context, so remember them for the lifetime of the handle, up to a limit.
		UnorderedMap<String, int> width_cache;
		String width_cache_key;
	};

} // namespace HarfBuzz
} // namespace Rml
