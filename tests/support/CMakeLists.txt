add_library(
    inferenceos_test_support
    STATIC
        fake_block.c
        fault_injector.c
)
add_library(InferenceOS::TestSupport ALIAS inferenceos_test_support)
target_include_directories(
    inferenceos_test_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/src/kernel/include"
)
target_compile_features(inferenceos_test_support PUBLIC c_std_17)
target_compile_options(
    inferenceos_test_support
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<BOOL:${INFERENCEOS_WARNINGS_AS_ERRORS}>:-Werror>
)
set_target_properties(
    inferenceos_test_support
    PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
)

add_library(inferenceos_test_main STATIC test_main.c)
add_library(InferenceOS::TestMain ALIAS inferenceos_test_main)
target_include_directories(
    inferenceos_test_main
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(inferenceos_test_main PUBLIC c_std_17)
target_compile_options(
    inferenceos_test_main
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<BOOL:${INFERENCEOS_WARNINGS_AS_ERRORS}>:-Werror>
)
set_target_properties(
    inferenceos_test_main
    PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
)
