#include "../../Include/RmlUi/Core/Filter.h"
#include "../../Include/RmlUi/Core/RenderManager.h"

namespace Rml {

Filter::Filter() {}

Filter::~Filter() {}

FilterLayerOperation Filter::GetLayerOperation(Element* /*element*/, FilterLayerState /*input_state*/) const
{
	return FilterLayerOperation::Push;
}

void Filter::ExtendInkOverflow(Element* /*element*/, Rectanglef& /*scissor_region*/) const {}

FilterInstancer::FilterInstancer() {}

FilterInstancer::~FilterInstancer() {}

} // namespace Rml
