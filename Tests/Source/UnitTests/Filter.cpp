#include "../Common/TestsInterface.h"
#include "../Common/TestsShell.h"
#include <RmlUi/Core/CompiledFilterShader.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Filter.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderManager.h>
#include <algorithm>
#include <doctest.h>

using namespace Rml;
class FilterTest;

struct CompiledTestFilter {
	String element_id;
	const FilterTest* filter;
};
static Vector<CompiledTestFilter> compiled_test_filters;

class FilterTest : public Filter {
public:
	FilterTest(float value, Unit unit) : value(value), unit(unit) {}

	CompiledFilter CompileFilter(Element* element) const override
	{
		compiled_test_filters.push_back({element->GetId(), this});
		return element->GetRenderManager()->CompileFilter("FilterTest", {});
	}

	float value = 0.f;
	Unit unit = Unit::UNKNOWN;
};

class FilterTestInstancer : public FilterInstancer {
public:
	enum class ValueType { NumberPercent, Angle };

	FilterTestInstancer()
	{
		id = RegisterProperty("value", "10cm").AddParser("length").GetId();
		RegisterShorthand("filter", "value", ShorthandType::FallThrough);
	}

	SharedPtr<Filter> InstanceFilter(const String& /*name*/, const PropertyDictionary& properties) override
	{
		const Property* p_value = properties.GetProperty(id);
		if (!p_value)
			return nullptr;

		CHECK(Any(p_value->unit & Unit::LENGTH));
		num_instances += 1;

		return MakeShared<FilterTest>(p_value->Get<float>(), p_value->unit);
	}

	int num_instances = 0;

private:
	PropertyId id = {};
};

static const String document_filter_rml = R"(
<rml>
<head>
	<style>
		body {
			top: 100px;
			left: 200px;
			width: 800px;
			height: 600px;
		}
		div {
			display: block;
			width: 400px;
			height: 300px;
		}
		#a    { filter: test(); }
		#b    { filter: test(0); }
		#c,#d { filter: test(1dp); }
	</style>
</head>

<body>
	<div id="a"/>
	<div id="b"/>
	<div id="c"/>
	<div id="d"/>
</body>
</rml>
)";

TEST_CASE("filter")
{
	compiled_test_filters.clear();
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	FilterTestInstancer instancer;
	Rml::Factory::RegisterFilterInstancer("test", &instancer);

	ElementDocument* document = context->LoadDocumentFromMemory(document_filter_rml);
	document->Show();

	TestsShell::RenderLoop();

	struct ExpectedCompiledFilter {
		float value;
		Unit unit;
	};

	// Map element ID to its expected filter value and unit.
	UnorderedMap<String, ExpectedCompiledFilter> expected_compiled_filters = {
		{"a", {10.f, Unit::CM}},
		{"b", {0.f, Unit::PX}},
		{"c", {1.f, Unit::DP}},
		{"d", {1.f, Unit::DP}},
	};
	for (const auto& compiled_filter : compiled_test_filters)
	{
		INFO("ID #", compiled_filter.element_id);
		auto it = expected_compiled_filters.find(compiled_filter.element_id);

		const bool id_found = (it != expected_compiled_filters.end());
		CHECK(id_found);
		if (!id_found)
			continue;

		const ExpectedCompiledFilter& expected = it->second;
		CHECK(compiled_filter.filter->value == expected.value);
		CHECK(compiled_filter.filter->unit == expected.unit);
	}

	// Check that filters are not compiled more than once for each element.
	CHECK(compiled_test_filters.size() == expected_compiled_filters.size());

	// Filters aren't cached like decorators are, so each element will instance a new decorator even if they refer to
	// the same style rule. Thus, here producing 4 instead of 3 unique instances.
	CHECK(instancer.num_instances == expected_compiled_filters.size());

	auto& counters = TestsShell::GetTestsRenderInterface()->GetCounters();
	CHECK(counters.compile_filter == expected_compiled_filters.size());
	CHECK(counters.release_filter == 0);

	document->Close();
	context->Update();

	CHECK(counters.compile_filter == expected_compiled_filters.size());
	CHECK(counters.release_filter == expected_compiled_filters.size());

	TestsShell::ShutdownShell();
}

TEST_CASE("filter.layer_operations_identity")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(R"(
<rml>
<head>
	<style>
		body { width: 200px; height: 200px; }
		#target {
			display: block;
			width: 100px;
			height: 100px;
			background: white;
			filter: opacity(1);
		}
	</style>
</head>
<body>
	<div id="target"/>
</body>
</rml>
)");
	document->Show();

	TestsRenderInterface* render_interface = TestsShell::GetTestsRenderInterface();
	render_interface->ResetCounters();
	TestsShell::RenderLoop();

	Element* target = document->GetElementById("target");
	REQUIRE(target);
	CHECK(target->GetComputedValues().has_filter());

	const auto& counters = render_interface->GetCounters();
	CHECK(counters.compile_filter == 0);
	CHECK(counters.push_layer == 0);
	CHECK(counters.composite_layers == 0);
	CHECK(counters.pop_layer == 0);

	document->Close();
	context->Update();
	TestsShell::ShutdownShell();
}

TEST_CASE("filter.layer_operations_mixed_chain")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(R"(
<rml>
<head>
	<style>
		body { width: 200px; height: 200px; }
		#target {
			display: block;
			width: 100px;
			height: 100px;
			background: white;
			filter: opacity(1) brightness(2);
		}
	</style>
</head>
<body>
	<div id="target"/>
</body>
</rml>
)");
	document->Show();

	TestsRenderInterface* render_interface = TestsShell::GetTestsRenderInterface();
	render_interface->ResetCounters();
	TestsShell::RenderLoop();

	const auto& counters = render_interface->GetCounters();
	CHECK(counters.compile_filter == 1);
	CHECK(counters.push_layer == 1);
	CHECK(counters.composite_layers == 1);
	CHECK(counters.pop_layer == 1);

	document->Close();
	context->Update();
	CHECK(counters.release_filter == 1);
	TestsShell::ShutdownShell();
}

TEST_CASE("filter.layer_operations_discard")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(R"(
<rml>
<head>
	<style>
		body { width: 200px; height: 200px; }
		#target {
			display: block;
			width: 100px;
			height: 100px;
			background: white;
			filter: opacity(0);
		}
		#target > div {
			display: block;
			width: 50px;
			height: 50px;
			background: red;
		}
	</style>
</head>
<body>
	<div id="target"><div/></div>
</body>
</rml>
)");
	document->Show();

	TestsRenderInterface* render_interface = TestsShell::GetTestsRenderInterface();
	render_interface->ResetCounters();
	TestsShell::RenderLoop();

	const auto& counters = render_interface->GetCounters();
	CHECK(counters.compile_filter == 0);
	CHECK(counters.render_geometry == 0);
	CHECK(counters.push_layer == 0);
	CHECK(counters.composite_layers == 0);
	CHECK(counters.pop_layer == 0);

	document->Close();
	context->Update();
	TestsShell::ShutdownShell();
}

TEST_CASE("filter.layer_operations_backdrop_identity")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(R"(
<rml>
<head>
	<style>
		body {
			width: 200px;
			height: 200px;
			background: red;
		}
		#target {
			display: block;
			width: 100px;
			height: 100px;
			background: white;
			backdrop-filter: opacity(1);
		}
	</style>
</head>
<body>
	<div id="target"/>
</body>
</rml>
)");
	document->Show();

	TestsRenderInterface* render_interface = TestsShell::GetTestsRenderInterface();
	render_interface->ResetCounters();
	TestsShell::RenderLoop();

	const auto& counters = render_interface->GetCounters();
	CHECK(counters.compile_filter == 0);
	CHECK(counters.push_layer == 0);
	CHECK(counters.composite_layers == 0);
	CHECK(counters.pop_layer == 0);

	document->Close();
	context->Update();
	TestsShell::ShutdownShell();
}
