#pragma once

#include "../../../Include/RmlUi/Core/StyleTypes.h"
#include "../FontEngineDefault/FontTypes.h"

struct hb_face_t;

namespace Rml {
namespace HarfBuzz {

	class FontFaceHandleHarfBuzz;

	/**
	    A single font face, holding its rasterizer face and its HarfBuzz face for shaping.
	 */
	class FontFace {
	public:
		/// @param[in] face The rasterizer face.
		/// @param[in] data The font data backing the face, must outlive this object.
		FontFace(FontFaceHandleFreetype face, Span<const byte> data, Style::FontStyle style, Style::FontWeight weight);
		~FontFace();

		Style::FontStyle GetStyle() const;
		Style::FontWeight GetWeight() const;

		/// Returns a handle for positioning and rendering this face at the given size.
		/// @param[in] size The size of the desired handle, in points.
		/// @param[in] load_default_glyphs True to load the default set of glyph (ASCII range).
		/// @return The font handle.
		FontFaceHandleHarfBuzz* GetHandle(int size, bool load_default_glyphs);

		/// Releases resources owned by sized font faces, including their textures and rendered glyphs.
		void ReleaseFontResources();

	private:
		Style::FontStyle style;
		Style::FontWeight weight;

		// Key is font size
		using HandleMap = UnorderedMap<int, UniquePtr<FontFaceHandleHarfBuzz>>;
		HandleMap handles;

		FontFaceHandleFreetype face;
		// Shared by the handles of every size. It reads the font tables directly from the font data, without copying them.
		hb_face_t* hb_face;
	};

} // namespace HarfBuzz
} // namespace Rml
