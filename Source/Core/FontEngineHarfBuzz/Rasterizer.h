#pragma once

#include "../../../Include/RmlUi/Core/FontMetrics.h"
#include "../FontEngineDefault/FontTypes.h"
#include "FontGlyph.h"

struct hb_face_t;
struct hb_font_t;

namespace Rml {
namespace HarfBuzz {

	/*
	    Loads font faces and rasterizes their glyphs for the HarfBuzz font engine. Glyphs are rasterized through FreeType, or through
	    HarfBuzz itself when RmlUi is built with RMLUI_HARFBUZZ_RASTER, which removes the dependency on FreeType. Glyphs are addressed by glyph
	    index, as produced by shaping.
	*/
	namespace Rasterizer {

		// An opaque handle to a face of the rasterizer.
		using FaceHandle = FontFaceHandleFreetype;

		bool Initialise();
		void Shutdown();

		// Returns a sorted list of available font variations for the font face located in memory.
		bool GetFaceVariations(Span<const byte> data, Vector<FaceVariation>& out_face_variations, int face_index);

		// Loads a face from memory, 'source' is only used for logging. The data must outlive the face.
		FaceHandle LoadFace(Span<const byte> data, const String& source, int face_index, int named_instance_index = 0);

		// Releases the face.
		void ReleaseFace(FaceHandle face);

		// Retrieves the font family, style and weight of the given font face. Use nullptr to ignore a property.
		void GetFaceStyle(FaceHandle face, String* font_family, Style::FontStyle* style, Style::FontWeight* weight);

		// Returns a new HarfBuzz face for shaping text with the given face, loaded from the same data.
		hb_face_t* CreateShapingFace(FaceHandle face, Span<const byte> data);

		// Returns the one-based index of the named instance of a variable font that the face was loaded with, or zero for none.
		unsigned int GetNamedInstanceIndex(FaceHandle face);

		// Initializes a face for a given font size. Glyphs are filled with the ASCII subset, and the font face metrics are set.
		// @param[in] font The HarfBuzz font of the face at this size, scaled to 26.6 fixed-point pixels.
		bool InitialiseFaceHandle(FaceHandle face, hb_font_t* font, int font_size, FontGlyphMap& glyphs, FontMetrics& metrics,
			bool load_default_glyphs);

		// Build a new glyph representing the given glyph index and append to 'glyphs'.
		bool AppendGlyph(FaceHandle face, hb_font_t* font, int font_size, FontGlyphIndex glyph_index, Character character, FontGlyphMap& glyphs);

	} // namespace Rasterizer
} // namespace HarfBuzz
} // namespace Rml
