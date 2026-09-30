#pragma once

#include "../../../Include/RmlUi/Core/FontMetrics.h"
#include "../FontEngineDefault/FontTypes.h"
#include "FontGlyph.h"

namespace Rml {
namespace HarfBuzz {

	/*
	    Glyph rasterization for the HarfBuzz font engine. Face loading and library management are shared with the default font engine in
	    Rml::FreeType, these functions only differ in that they address glyphs by glyph index instead of by character.
	*/
	namespace FreeType {

		// Initializes a face for a given font size. Glyphs are filled with the ASCII subset, and the font face metrics are set.
		bool InitialiseFaceHandle(FontFaceHandleFreetype face, int font_size, FontGlyphMap& glyphs, FontMetrics& metrics, bool load_default_glyphs);

		// Build a new glyph representing the given glyph index and append to 'glyphs'.
		bool AppendGlyph(FontFaceHandleFreetype face, int font_size, FontGlyphIndex glyph_index, Character character, FontGlyphMap& glyphs);

		// Returns the corresponding glyph index from a character code.
		FontGlyphIndex GetGlyphIndexFromCharacter(FontFaceHandleFreetype face, Character character);

	} // namespace FreeType
} // namespace HarfBuzz
} // namespace Rml
