#include "ElementBackgroundBorder.h"
#include "../../Include/RmlUi/Core/Box.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/DecorationTypes.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementDocument.h"
#include "../../Include/RmlUi/Core/MeshUtilities.h"
#include "../../Include/RmlUi/Core/Profiling.h"
#include "../../Include/RmlUi/Core/Property.h"
#include "../../Include/RmlUi/Core/RenderManager.h"
#include "BoxShadowCache.h"
#include "ElementStyle.h"
#include "GeometryBoxShadow.h"

namespace Rml {

ElementBackgroundBorder::ElementBackgroundBorder() {}

void ElementBackgroundBorder::Render(Element* element)
{
	if (background_dirty || border_dirty)
	{
		for (auto& background : backgrounds)
		{
			if (background.first != BackgroundType::BackgroundBorder)
				background.second.geometry.Release();
		}

		GenerateGeometry(element);

		background_dirty = false;
		border_dirty = false;
	}

	if (Background* shadow = GetBackground(BackgroundType::BoxShadowAndBackgroundBorder))
	{
		const Vector2f offset = element->GetAbsoluteOffset(BoxArea::Border);
		shadow->box_shadow_and_background_border->geometry.Render(offset, shadow->box_shadow_and_background_border->texture);
	}
	else if (Background* background = GetBackground(BackgroundType::BackgroundBorder))
	{
		const Vector2f offset = element->GetAbsoluteOffset(BoxArea::Border);
		background->geometry.Render(offset);
	}
}

void ElementBackgroundBorder::DirtyBackground()
{
	background_dirty = true;
}

void ElementBackgroundBorder::DirtyBoxShadowExtents()
{
	box_shadow_extents_dirty = true;
}

void ElementBackgroundBorder::DirtyBorder()
{
	border_dirty = true;
}

void ElementBackgroundBorder::GetBoxShadowExtents(Element* element, Vector2f& out_top_left, Vector2f& out_bottom_right)
{
	// The shadow lengths may be relative to any of these.
	LengthContext length_context;
	length_context.font_size = element->GetComputedValues().font_size();
	if (ElementDocument* document = element->GetOwnerDocument())
		length_context.document_font_size = document->GetComputedValues().font_size();
	if (Context* context = element->GetContext())
	{
		length_context.dp_ratio = context->GetDensityIndependentPixelRatio();
		length_context.viewport = context->GetDimensions();
	}

	if (box_shadow_extents_dirty || length_context != box_shadow_length_context)
	{
		box_shadow_extents_dirty = false;
		box_shadow_length_context = length_context;
		box_shadow_extent_top_left = {};
		box_shadow_extent_bottom_right = {};

		const Property* p_box_shadow = nullptr;
		if (element->GetComputedValues().has_box_shadow())
			p_box_shadow = element->GetStyle()->GetLocalPropertyWithResolvedVariables(PropertyId::BoxShadow);

		if (p_box_shadow)
		{
			RMLUI_ASSERT(p_box_shadow->value.GetType() == Variant::BOXSHADOWLIST);
			const BoxShadowList& shadow_list = p_box_shadow->value.GetReference<BoxShadowList>();

			for (const BoxShadow& shadow : shadow_list)
			{
				if (!shadow.inset)
				{
					const float extent = 1.5f * element->ResolveLength(shadow.blur_radius) + element->ResolveLength(shadow.spread_distance);
					const Vector2f offset = {element->ResolveLength(shadow.offset_x), element->ResolveLength(shadow.offset_y)};

					box_shadow_extent_top_left = Math::Max(box_shadow_extent_top_left, -offset + Vector2f(extent));
					box_shadow_extent_bottom_right = Math::Max(box_shadow_extent_bottom_right, offset + Vector2f(extent));
				}
			}
		}
	}

	out_top_left = box_shadow_extent_top_left;
	out_bottom_right = box_shadow_extent_bottom_right;
}

Geometry* ElementBackgroundBorder::GetClipGeometry(Element* element, BoxArea clip_area)
{
	BackgroundType type = {};
	switch (clip_area)
	{
	case Rml::BoxArea::Border: type = BackgroundType::ClipBorder; break;
	case Rml::BoxArea::Padding: type = BackgroundType::ClipPadding; break;
	case Rml::BoxArea::Content: type = BackgroundType::ClipContent; break;
	default: RMLUI_ERROR; return nullptr;
	}

	RenderManager* render_manager = element->GetRenderManager();
	Geometry& geometry = GetOrCreateBackground(type).geometry;
	if (render_manager && !geometry)
	{
		Mesh mesh = geometry.Release(Geometry::ReleaseMode::ClearMesh);
		MeshUtilities::GenerateBackground(mesh, element->GetRenderBox(clip_area), ColourbPremultiplied(255));
		geometry = render_manager->MakeGeometry(std::move(mesh));
	}

	return &geometry;
}

ElementBackgroundBorder::Background* ElementBackgroundBorder::GetBackground(BackgroundType type)
{
	auto it = backgrounds.find(type);
	if (it != backgrounds.end())
		return &it->second;
	return nullptr;
}

ElementBackgroundBorder::Background& ElementBackgroundBorder::GetOrCreateBackground(BackgroundType type)
{
	auto it = backgrounds.find(type);
	if (it != backgrounds.end())
		return it->second;

	Background& background = backgrounds[type];
	return background;
}

void ElementBackgroundBorder::EraseBackground(BackgroundType type)
{
	backgrounds.erase(type);
}

void ElementBackgroundBorder::GenerateGeometry(Element* element)
{
	RMLUI_ZoneScoped;
	RenderManager* render_manager = element->GetRenderManager();
	if (!render_manager)
		return;

	const ComputedValues& computed = element->GetComputedValues();
	const bool has_box_shadow = computed.has_box_shadow();

	if (has_box_shadow)
	{
		// The box shadow geometry also includes the element's background and border, thus we can skip the normal background generation.
		EraseBackground(BackgroundType::BackgroundBorder);
		Background& shadow_background = GetOrCreateBackground(BackgroundType::BoxShadowAndBackgroundBorder);
		shadow_background.box_shadow_and_background_border = BoxShadowCache::GetHandle(element, computed);
		return;
	}

	EraseBackground(BackgroundType::BoxShadowAndBackgroundBorder);

	const float opacity = computed.opacity();
	ColourbPremultiplied background_color = computed.background_color().ToPremultiplied(opacity);
	Array<ColourbPremultiplied, 4> border_colors = {
		computed.border_top_color().ToPremultiplied(opacity),
		computed.border_right_color().ToPremultiplied(opacity),
		computed.border_bottom_color().ToPremultiplied(opacity),
		computed.border_left_color().ToPremultiplied(opacity),
	};

	Geometry& geometry = GetOrCreateBackground(BackgroundType::BackgroundBorder).geometry;
	Mesh mesh = geometry.Release(Geometry::ReleaseMode::ClearMesh);

	for (int i = 0; i < element->GetNumBoxes(); i++)
		MeshUtilities::GenerateBackgroundBorder(mesh, element->GetRenderBox(BoxArea::Padding, i), background_color, border_colors.data());

	geometry = render_manager->MakeGeometry(std::move(mesh));
}

} // namespace Rml
