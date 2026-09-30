#pragma once

#include "../../Include/RmlUi/Core/CallbackTexture.h"
#include "../../Include/RmlUi/Core/Geometry.h"
#include "../../Include/RmlUi/Core/Types.h"

namespace Rml {

struct BoxShadowRenderable;

class ElementBackgroundBorder {
public:
	ElementBackgroundBorder();
	void Render(Element* element);

	void DirtyBackground();
	void DirtyBorder();
	void DirtyBoxShadowExtents();

	Geometry* GetClipGeometry(Element* element, BoxArea clip_area);

	/// Returns how far the element's outer box shadows extend beyond its border box.
	void GetBoxShadowExtents(Element* element, Vector2f& out_top_left, Vector2f& out_bottom_right);

private:
	enum class BackgroundType { BackgroundBorder, BoxShadowAndBackgroundBorder, ClipBorder, ClipPadding, ClipContent, Count };
	struct Background {
		Geometry geometry;
		Texture texture;
		SharedPtr<BoxShadowRenderable> box_shadow_and_background_border;
	};

	Background* GetBackground(BackgroundType type);
	Background& GetOrCreateBackground(BackgroundType type);
	void EraseBackground(BackgroundType type);

	void GenerateGeometry(Element* element);

	bool background_dirty = false;
	bool border_dirty = false;

	struct LengthContext {
		float font_size = 0.f;
		float document_font_size = 0.f;
		float dp_ratio = 1.f;
		Vector2i viewport;
		bool operator!=(const LengthContext& other) const
		{
			return font_size != other.font_size || document_font_size != other.document_font_size || dp_ratio != other.dp_ratio ||
				viewport != other.viewport;
		}
	};

	// Resolving the box shadows requires substituting their variables and parsing them, so their extents are cached.
	bool box_shadow_extents_dirty = true;
	LengthContext box_shadow_length_context;
	Vector2f box_shadow_extent_top_left;
	Vector2f box_shadow_extent_bottom_right;

	StableMap<BackgroundType, Background> backgrounds;
};

} // namespace Rml
