find_package(Qt6 REQUIRED COMPONENTS Test)

# Each test is one Qt Test executable, run on the offscreen platform so CI needs no display.
function(shoal_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "LIBRARIES")
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBRARIES})
    target_compile_definitions(${name} PRIVATE
        SHOAL_EXPECTED_VERSION="${SHOAL_VERSION_TAG}"
        SHOAL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    shoal_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

shoal_add_test(CoreTests LIBRARIES shoal::core)
shoal_add_test(JsonWriterTests LIBRARIES shoal::core)
shoal_add_test(ProjectCodecTests LIBRARIES shoal::core)
shoal_add_test(ProjectStoreTests LIBRARIES shoal::core)
shoal_add_test(CommandStackTests LIBRARIES shoal::core)
shoal_add_test(ProjectDocumentTests LIBRARIES shoal::core)
shoal_add_test(AssetsTests LIBRARIES shoal::assets)
shoal_add_test(EditorTests LIBRARIES shoal::editor)
