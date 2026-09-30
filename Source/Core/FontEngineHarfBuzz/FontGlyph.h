#pragma once

#include "../../../Include/RmlUi/Core/FontGlyph.h"
#include "../../../Include/RmlUi/Core/Types.h"

namespace Rml {
namespace HarfBuzz {

	using FontGlyphIndex = uint32_t;

	struct FontGlyphData {
		FontGlyph bitmap;
		Character character;
	};

	struct FontGlyphReference {
		const FontGlyph* bitmap;
		Character character;
	};

	struct FontClusterGlyphData {
		FontGlyphIndex glyph_index;
		FontGlyphData glyph_data;
	};

	// Glyphs are keyed by glyph index rather than character, since shaping can produce glyphs that no character maps to, such as ligatures.
	using FontGlyphMap = UnorderedMap<FontGlyphIndex, FontGlyphData>;
	using FallbackFontGlyphMap = UnorderedMap<Character, FontGlyph>;
	using FallbackFontClusterGlyphsMap = UnorderedMap<String, Vector<FontClusterGlyphData>>;
	using FallbackFontClusterGlyphLookupMap = UnorderedMap<uint64_t, const FontGlyph*>;

	struct FontGlyphMaps {
		const FontGlyphMap* glyphs;
		const FallbackFontGlyphMap* fallback_glyphs;
		const FallbackFontClusterGlyphLookupMap* fallback_cluster_glyphs;
	};

	inline uint64_t GetFallbackFontClusterGlyphLookupID(FontGlyphIndex glyph_index, Character character)
	{
		// Combine 32-bit glyph index and 32-bit character into a single 64-bit integer.
		return (static_cast<uint64_t>(glyph_index) << (sizeof(Character) * 8)) | static_cast<uint64_t>(character);
	}

} // namespace HarfBuzz
} // namespace Rml
