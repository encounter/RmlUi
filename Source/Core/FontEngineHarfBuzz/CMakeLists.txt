# RMLUI_CMAKE_MINIMUM_VERSION_RAISE_NOTICE:
# Using absolute paths to prevent improper interpretation of relative paths Relative paths can be used once the minimum
# CMake version is greater or equal than CMake 3.13
target_sources(rmlui_core PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FontEngineDefault/FontTypes.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/../FontEngineDefault/FreeTypeInterface.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../FontEngineDefault/FreeTypeInterface.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontEngineInterfaceHarfBuzz.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontEngineInterfaceHarfBuzz.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontFace.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontFace.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontFaceHandleHarfBuzz.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontFaceHandleHarfBuzz.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontFaceLayer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontFaceLayer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontFamily.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontFamily.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontGlyph.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontProvider.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontProvider.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FreeTypeInterface.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FreeTypeInterface.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/LanguageData.h"
)

target_compile_definitions(rmlui_core PRIVATE "RMLUI_FONT_ENGINE_HARFBUZZ")
