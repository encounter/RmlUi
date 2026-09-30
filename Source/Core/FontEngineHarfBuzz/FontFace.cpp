#include "FontFace.h"
#include "../../../Include/RmlUi/Core/Log.h"
#include "FontFaceHandleHarfBuzz.h"
#include "Rasterizer.h"
#include <hb.h>

namespace Rml {
namespace HarfBuzz {

	FontFace::FontFace(FontFaceHandleFreetype _face, Span<const byte> data, Style::FontStyle _style, Style::FontWeight _weight)
	{
		style = _style;
		weight = _weight;
		face = _face;

		hb_face = Rasterizer::CreateShapingFace(face, data);
	}

	FontFace::~FontFace()
	{
		// The handles reference both the HarfBuzz and rasterizer faces, release them first.
		handles.clear();
		hb_face_destroy(hb_face);

		if (face)
			Rasterizer::ReleaseFace(face);
	}

	Style::FontStyle FontFace::GetStyle() const
	{
		return style;
	}

	Style::FontWeight FontFace::GetWeight() const
	{
		return weight;
	}

	FontFaceHandleHarfBuzz* FontFace::GetHandle(int size, bool load_default_glyphs)
	{
		auto it = handles.find(size);
		if (it != handles.end())
			return it->second.get();

		// See if this face has been released.
		if (!face)
		{
			Log::Message(Log::LT_WARNING, "Font face has been released, unable to generate new handle.");
			return nullptr;
		}

		// Construct and initialise the new handle.
		auto handle = MakeUnique<FontFaceHandleHarfBuzz>();
		if (!handle->Initialize(face, hb_face, size, load_default_glyphs))
		{
			handles[size] = nullptr;
			return nullptr;
		}

		FontFaceHandleHarfBuzz* result = handle.get();

		// Save the new handle to the font face
		handles[size] = std::move(handle);

		return result;
	}

	void FontFace::ReleaseFontResources()
	{
		HandleMap().swap(handles);
	}

} // namespace HarfBuzz
} // namespace Rml
