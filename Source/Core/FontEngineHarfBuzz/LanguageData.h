#pragma once

#include "../../../Include/RmlUi/Core/Types.h"

namespace Rml {
namespace HarfBuzz {

	enum class TextFlowDirection {
		LeftToRight,
		RightToLeft,
	};

	struct LanguageData {
		String script_code;
		TextFlowDirection text_flow_direction;
	};

	using LanguageDataMap = UnorderedMap<String, LanguageData>;

} // namespace HarfBuzz
} // namespace Rml
