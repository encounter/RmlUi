#include "../../../Include/RmlUi/Core/Log.h"
#include "../../../Include/RmlUi/Core/Math.h"
#include "Rasterizer.h"
#include <algorithm>
#include <hb-ot.h>
#include <hb-raster.h>
#include <hb.h>
#include <string.h>

namespace Rml {
namespace HarfBuzz {
	namespace Rasterizer {

		/*
		    Rasterizes glyphs with HarfBuzz's own rasterizer, so that no FreeType is needed. Glyphs are not hinted. Face information and metrics
		    are read the way FreeType reads them, so that text lays out the same as with the FreeType rasterizer.
		*/

		struct Face {
			hb_face_t* face;
			unsigned int named_instance_index;
		};

		static hb_raster_draw_t* raster_draw = nullptr;
		static hb_raster_paint_t* raster_paint = nullptr;

		static Face* GetFace(FaceHandle face)
		{
			return reinterpret_cast<Face*>(face);
		}

		// Font tables are read directly for the values that FreeType exposes, since HarfBuzz applies different fallbacks for some of them.
		class TableReader {
		public:
			TableReader(hb_face_t* face, hb_tag_t tag) : blob(hb_face_reference_table(face, tag))
			{
				unsigned int length = 0;
				data = reinterpret_cast<const uint8_t*>(hb_blob_get_data(blob, &length));
				size = length;
			}
			~TableReader() { hb_blob_destroy(blob); }

			bool Has(unsigned int end_offset) const { return data && size >= end_offset; }
			uint16_t U16(unsigned int offset) const { return Has(offset + 2) ? uint16_t((data[offset] << 8) | data[offset + 1]) : 0; }
			int16_t S16(unsigned int offset) const { return int16_t(U16(offset)); }

		private:
			hb_blob_t* blob;
			const uint8_t* data;
			unsigned int size;
		};

		static constexpr hb_tag_t tag_head = HB_TAG('h', 'e', 'a', 'd');
		static constexpr hb_tag_t tag_hhea = HB_TAG('h', 'h', 'e', 'a');
		static constexpr hb_tag_t tag_os2 = HB_TAG('O', 'S', '/', '2');
		static constexpr hb_tag_t tag_post = HB_TAG('p', 'o', 's', 't');
		static constexpr hb_tag_t tag_cmap = HB_TAG('c', 'm', 'a', 'p');

		static bool BuildGlyph(const Face& face, hb_font_t* font, FontGlyphIndex glyph_index, Character character, FontGlyphMap& glyphs);
		static void BuildGlyphMap(const Face& face, hb_font_t* font, int size, FontGlyphMap& glyphs, bool load_default_glyphs);
		static void GenerateMetrics(const Face& face, hb_font_t* font, int font_size, FontMetrics& metrics);

		bool Initialise()
		{
			RMLUI_ASSERT(!raster_draw && !raster_paint);

			raster_draw = hb_raster_draw_create_or_fail();
			raster_paint = hb_raster_paint_create_or_fail();
			if (!raster_draw || !raster_paint)
			{
				Log::Message(Log::LT_ERROR, "Failed to initialise the HarfBuzz rasterizer.");
				Shutdown();
				return false;
			}

			// Fonts are scaled to 26.6 fixed-point pixels, rasterize them at pixel size.
			hb_raster_draw_set_scale_factor(raster_draw, 64.f, 64.f);
			hb_raster_paint_set_scale_factor(raster_paint, 64.f, 64.f);
			// Like FreeType, paint the foreground color of color glyphs in opaque black.
			hb_raster_paint_set_foreground(raster_paint, HB_COLOR(0, 0, 0, 255));

			return true;
		}

		void Shutdown()
		{
			hb_raster_draw_destroy(raster_draw);
			hb_raster_paint_destroy(raster_paint);
			raster_draw = nullptr;
			raster_paint = nullptr;
		}

		static hb_face_t* CreateFace(Span<const byte> data, int face_index)
		{
			hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(data.data()), static_cast<unsigned int>(data.size()),
				HB_MEMORY_MODE_READONLY, nullptr, nullptr);
			hb_face_t* face = hb_face_create(blob, static_cast<unsigned int>(face_index));
			hb_blob_destroy(blob);

			// HarfBuzz returns an empty face for data it can't read.
			if (hb_face_get_glyph_count(face) == 0)
			{
				hb_face_destroy(face);
				return nullptr;
			}
			return face;
		}

		bool GetFaceVariations(Span<const byte> data, Vector<FaceVariation>& out_face_variations, int face_index)
		{
			hb_face_t* face = CreateFace(data, face_index);
			if (!face)
				return false;

			const unsigned int num_axes = hb_ot_var_get_axis_count(face);
			const unsigned int num_named_instances = hb_ot_var_get_named_instance_count(face);
			if (num_axes > 0 && num_named_instances > 0)
			{
				Vector<hb_ot_var_axis_info_t> axes(num_axes);
				unsigned int axis_count = num_axes;
				hb_ot_var_get_axis_infos(face, 0, &axis_count, axes.data());

				unsigned int axis_index_weight = num_axes;
				unsigned int axis_index_width = num_axes;
				for (unsigned int i = 0; i < axis_count; i++)
				{
					if (axes[i].tag == HB_OT_TAG_VAR_AXIS_WEIGHT)
						axis_index_weight = i;
					else if (axes[i].tag == HB_OT_TAG_VAR_AXIS_WIDTH)
						axis_index_width = i;
				}

				Vector<float> coords(num_axes);
				for (unsigned int i = 0; i < num_named_instances; i++)
				{
					unsigned int coords_length = num_axes;
					hb_ot_var_named_instance_get_design_coords(face, i, &coords_length, coords.data());

					const uint16_t weight = (axis_index_weight < coords_length ? (uint16_t)coords[axis_index_weight] : 0);
					const uint16_t width = (axis_index_width < coords_length ? (uint16_t)coords[axis_index_width] : 0);
					const int named_instance_index = int(i + 1);

					out_face_variations.push_back(FaceVariation{weight == 0 ? Style::FontWeight::Normal : (Style::FontWeight)weight,
						width == 0 ? (uint16_t)100 : width, named_instance_index});
				}
			}

			std::sort(out_face_variations.begin(), out_face_variations.end());

			hb_face_destroy(face);
			return true;
		}

		FaceHandle LoadFace(Span<const byte> data, const String& source, int face_index, int named_instance_index)
		{
			hb_face_t* face = CreateFace(data, face_index);
			if (!face)
			{
				Log::Message(Log::LT_ERROR, "Invalid or unsupported font face file format while loading face from %s.", source.c_str());
				return 0;
			}

			if (!TableReader(face, tag_cmap).Has(4))
			{
				Log::Message(Log::LT_ERROR, "Font face (from %s) does not contain a character map.", source.c_str());
				hb_face_destroy(face);
				return 0;
			}

			return reinterpret_cast<FaceHandle>(new Face{face, static_cast<unsigned int>(named_instance_index)});
		}

		void ReleaseFace(FaceHandle face_handle)
		{
			Face* face = GetFace(face_handle);
			hb_face_destroy(face->face);
			delete face;
		}

		static String GetName(hb_face_t* face, hb_ot_name_id_t name_id)
		{
			hb_language_t language = hb_language_from_string("en", -1);
			unsigned int length = hb_ot_name_get_utf8(face, name_id, language, nullptr, nullptr);
			if (length == 0)
				return String();

			String name(length + 1, '\0');
			unsigned int text_size = length + 1;
			hb_ot_name_get_utf8(face, name_id, language, &text_size, &name[0]);
			name.resize(text_size);
			return name;
		}

		void GetFaceStyle(FaceHandle face_handle, String* font_family, Style::FontStyle* style, Style::FontWeight* weight)
		{
			hb_face_t* face = GetFace(face_handle)->face;
			const TableReader os2(face, tag_os2);
			const bool has_os2 = os2.Has(64);
			const uint16_t fs_selection = has_os2 ? os2.U16(62) : 0;

			if (font_family)
			{
				// Like FreeType, prefer the WWS family name unless the font says its family names are WWS already, then the typographic family.
				String family;
				const bool is_wws = (fs_selection & (1 << 8)) != 0;
				if (!is_wws)
					family = GetName(face, HB_OT_NAME_ID_WWS_FAMILY);
				if (family.empty())
					family = GetName(face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY);
				if (family.empty())
					family = GetName(face, HB_OT_NAME_ID_FONT_FAMILY);
				*font_family = std::move(family);
			}

			const uint16_t mac_style = TableReader(face, tag_head).U16(44);
			const bool is_bold = has_os2 ? (fs_selection & (1 << 5)) != 0 : (mac_style & 1) != 0;
			const bool is_italic = has_os2 ? (fs_selection & 1) != 0 : (mac_style & 2) != 0;

			if (style)
				*style = is_italic ? Style::FontStyle::Italic : Style::FontStyle::Normal;

			if (weight)
			{
				const uint16_t weight_class = has_os2 ? os2.U16(4) : 0;
				if (weight_class != 0)
					*weight = (Style::FontWeight)weight_class;
				else
					*weight = is_bold ? Style::FontWeight::Bold : Style::FontWeight::Normal;
			}
		}

		hb_face_t* CreateShapingFace(FaceHandle face, Span<const byte> /*data*/)
		{
			return hb_face_reference(GetFace(face)->face);
		}

		unsigned int GetNamedInstanceIndex(FaceHandle face)
		{
			return GetFace(face)->named_instance_index;
		}

		bool InitialiseFaceHandle(FaceHandle face_handle, hb_font_t* font, int font_size, FontGlyphMap& glyphs, FontMetrics& metrics,
			bool load_default_glyphs)
		{
			const Face& face = *GetFace(face_handle);

			metrics.size = font_size;

			// Construct the initial list of glyphs.
			BuildGlyphMap(face, font, font_size, glyphs, load_default_glyphs);

			// Generate the metrics for the handle.
			GenerateMetrics(face, font, font_size, metrics);

			return true;
		}

		bool AppendGlyph(FaceHandle face, hb_font_t* font, int /*font_size*/, FontGlyphIndex glyph_index, Character character, FontGlyphMap& glyphs)
		{
			RMLUI_ASSERT(glyphs.find(glyph_index) == glyphs.end());
			return BuildGlyph(*GetFace(face), font, glyph_index, character, glyphs);
		}

		static bool IsColorGlyph(hb_face_t* face, hb_font_t* font, FontGlyphIndex glyph_index)
		{
			if (hb_ot_color_glyph_has_paint(face, glyph_index))
				return true;
			if (hb_ot_color_has_layers(face) && hb_ot_color_glyph_get_layers(face, glyph_index, 0, nullptr, nullptr) > 0)
				return true;
			if (hb_ot_color_has_png(face))
			{
				hb_blob_t* png = hb_ot_color_glyph_reference_png(font, glyph_index);
				const bool has_png = hb_blob_get_length(png) > 0;
				hb_blob_destroy(png);
				return has_png;
			}
			return false;
		}

		// Copies an image into the glyph. Images are stored bottom-up, while glyph bitmaps are stored top-down.
		static void CopyImage(hb_raster_image_t* image, FontGlyph& glyph)
		{
			hb_raster_extents_t extents = {};
			hb_raster_image_get_extents(image, &extents);
			const uint8_t* source = hb_raster_image_get_buffer(image);
			if (!source || extents.width == 0 || extents.height == 0)
				return;

			const bool is_color = (hb_raster_image_get_format(image) == HB_RASTER_FORMAT_BGRA32);
			const int bytes_per_pixel = is_color ? 4 : 1;
			const int width = (int)extents.width;
			const int height = (int)extents.height;

			glyph.color_format = is_color ? ColorFormat::RGBA8 : ColorFormat::A8;
			glyph.bitmap_dimensions = Vector2i(width, height);
			glyph.bearing = Vector2i(extents.x_origin, extents.y_origin + height);

			const int row_size = width * bytes_per_pixel;
			glyph.bitmap_owned_data.reset(new byte[row_size * height]);
			glyph.bitmap_data = glyph.bitmap_owned_data.get();

			for (int y = 0; y < height; y++)
			{
				byte* destination = glyph.bitmap_owned_data.get() + y * row_size;
				const uint8_t* source_row = source + (height - 1 - y) * extents.stride;
				memcpy(destination, source_row, row_size);

				// Premultiplied BGRA to premultiplied RGBA.
				if (is_color)
				{
					for (int x = 0; x < row_size; x += 4)
						std::swap(destination[x], destination[x + 2]);
				}
			}
		}

		static bool BuildGlyph(const Face& face, hb_font_t* font, FontGlyphIndex glyph_index, Character character, FontGlyphMap& glyphs)
		{
			if (glyph_index == 0)
				return false;

			auto result = glyphs.emplace(glyph_index, FontGlyphData{FontGlyph{}, character});
			if (!result.second)
			{
				Log::Message(Log::LT_WARNING, "Glyph index '%u' is already loaded in the font face.", (unsigned int)glyph_index);
				return false;
			}

			FontGlyph& glyph = result.first->second.bitmap;
			glyph.advance = (hb_font_get_glyph_h_advance(font, glyph_index) + 32) >> 6;

			// Glyphs without extents, such as spaces, have no bitmap.
			hb_glyph_extents_t glyph_extents = {};
			if (!hb_font_get_glyph_extents(font, glyph_index, &glyph_extents))
				return true;

			hb_raster_image_t* image = nullptr;
			if (IsColorGlyph(face.face, font, glyph_index))
			{
				if (hb_raster_paint_set_glyph_extents(raster_paint, &glyph_extents) && hb_raster_paint_glyph_or_fail(raster_paint, font, glyph_index))
					image = hb_raster_paint_render(raster_paint);
				hb_raster_paint_clear(raster_paint);
			}

			if (!image)
			{
				if (hb_raster_draw_set_glyph_extents(raster_draw, &glyph_extents))
				{
					hb_raster_draw_glyph(raster_draw, font, glyph_index);
					image = hb_raster_draw_render(raster_draw);
				}
				hb_raster_draw_clear(raster_draw);
			}

			if (image)
			{
				CopyImage(image, glyph);
				hb_raster_image_destroy(image);
			}

			return true;
		}

		static void BuildGlyphMap(const Face& face, hb_font_t* font, int size, FontGlyphMap& glyphs, bool load_default_glyphs)
		{
			if (load_default_glyphs)
			{
				glyphs.reserve(128);

				// Add the ASCII characters now. Other characters are added later as needed.
				for (hb_codepoint_t character_code = 32; character_code <= 126; ++character_code)
				{
					// Several characters can map to the same glyph, such as upper and lower case letters in icon fonts.
					hb_codepoint_t index = 0;
					if (hb_font_get_nominal_glyph(font, character_code, &index) && glyphs.find(index) == glyphs.end())
						BuildGlyph(face, font, index, static_cast<Character>(character_code), glyphs);
				}
			}

			// Add a replacement glyph for rendering unknown characters. It takes the place of the font's own '.notdef' glyph at index zero, which is
			// what shaping produces for characters missing from the font.
			FontGlyph glyph;
			glyph.bitmap_dimensions = {size / 3, (size * 2) / 3};
			glyph.advance = glyph.bitmap_dimensions.x + 2;
			glyph.bearing = {1, glyph.bitmap_dimensions.y};

			glyph.bitmap_owned_data.reset(new byte[glyph.bitmap_dimensions.x * glyph.bitmap_dimensions.y]);
			glyph.bitmap_data = glyph.bitmap_owned_data.get();

			for (int y = 0; y < glyph.bitmap_dimensions.y; y++)
			{
				for (int x = 0; x < glyph.bitmap_dimensions.x; x++)
				{
					constexpr int stroke = 1;
					int i = y * glyph.bitmap_dimensions.x + x;
					bool near_edge = (x < stroke || x >= glyph.bitmap_dimensions.x - stroke || y < stroke || y >= glyph.bitmap_dimensions.y - stroke);
					glyph.bitmap_owned_data[i] = (near_edge ? 0xdd : 0);
				}
			}

			glyphs[0] = FontGlyphData{std::move(glyph), Character::Replacement};
		}

		static void GenerateMetrics(const Face& face, hb_font_t* font, int font_size, FontMetrics& metrics)
		{
			const TableReader head(face.face, tag_head);
			const TableReader hhea(face.face, tag_hhea);
			const TableReader os2(face.face, tag_os2);
			const TableReader post(face.face, tag_post);

			const float units_per_em = float(Math::Max(head.U16(18), uint16_t(1)));
			const float scale = float(font_size) / units_per_em;

			// Like FreeType, the vertical metrics come from the 'hhea' table, or else from the typographic or Windows metrics in 'OS/2'.
			float ascender = hhea.S16(4);
			float descender = hhea.S16(6);
			float line_gap = hhea.S16(8);
			if (ascender == 0 && descender == 0 && os2.Has(78))
			{
				if (os2.S16(68) != 0 || os2.S16(70) != 0)
				{
					ascender = os2.S16(68);
					descender = os2.S16(70);
					line_gap = os2.S16(72);
				}
				else
				{
					ascender = os2.U16(74);
					descender = -float(os2.U16(76));
					line_gap = 0;
				}
			}

			// Apply variable font deltas, which FreeType also applies from the 'MVAR' table.
			ascender += hb_ot_metrics_get_variation(font, HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER);
			descender += hb_ot_metrics_get_variation(font, HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER);
			line_gap += hb_ot_metrics_get_variation(font, HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP);

			// Round to whole pixels like FreeType's scaled size metrics. TrueType fonts that ask for integer ppem sizes round all three.
			const float height = ascender - descender + line_gap;
			const bool round_all = (head.U16(16) & 8) != 0;
			metrics.ascent = round_all ? Math::Round(ascender * scale) : Math::RoundUp(ascender * scale);
			metrics.descent = -(round_all ? Math::Round(descender * scale) : Math::RoundDown(descender * scale));
			metrics.line_spacing = Math::Round(height * scale);

			// Like FreeType, the underline position is centered on its thickness.
			const float underline_thickness = post.S16(10);
			const float underline_position = post.S16(8) - float(int(underline_thickness) / 2);
			metrics.underline_position = -underline_position * scale;
			metrics.underline_thickness = Math::Max(underline_thickness * scale, 1.0f);

			// Determine the x-height of this font face.
			hb_codepoint_t x_index = 0;
			hb_glyph_extents_t x_extents = {};
			if (hb_font_get_nominal_glyph(font, 'x', &x_index) && hb_font_get_glyph_extents(font, x_index, &x_extents))
				metrics.x_height = -x_extents.height / 64.f;
			else
				metrics.x_height = 0.5f * metrics.line_spacing;

			hb_codepoint_t ellipsis_index = 0;
			metrics.has_ellipsis = hb_font_get_nominal_glyph(font, 0x2026, &ellipsis_index);
		}

	} // namespace Rasterizer
} // namespace HarfBuzz
} // namespace Rml
