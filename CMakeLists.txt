cmake_minimum_required(VERSION 3.16)
project(vmgen VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- libyaml (fetched via FetchContent) ---
include(FetchContent)
FetchContent_Declare(
    libyaml
    GIT_REPOSITORY https://github.com/yaml/libyaml.git
    GIT_TAG        0.2.5
)
FetchContent_MakeAvailable(libyaml)

# --- vmgen executable ---
add_executable(vmgen
    src/main.c
    src/util/buffer.c
    src/spec/model.c
    src/spec/parser.c
    src/spec/validate.c
    src/codegen/emit.c
    src/codegen/gen_opcodes.c
    src/codegen/gen_vm.c
    src/codegen/action_parser.c
    src/codegen/action_emit.c
    src/codegen/gen_assembler.c
    src/codegen/gen_disasm.c
    src/codegen/gen_gc.c
    src/codegen/gen_strings.c
    src/codegen/gen_closures.c
    src/codegen/gen_modules.c
    src/codegen/gen_fibers.c
    src/codegen/gen_native.c
    src/codegen/gen_build.c
)

target_include_directories(vmgen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vmgen PRIVATE yaml)

# --- Tests (testy) ---
enable_testing()

add_executable(vmgen_tests
    third_party/testy/test_main.c
    tests/test_entry.c
    tests/test_parser.c
    tests/test_codegen.c
    src/util/buffer.c
    src/spec/model.c
    src/spec/parser.c
    src/spec/validate.c
    src/codegen/emit.c
    src/codegen/gen_opcodes.c
    src/codegen/gen_vm.c
    src/codegen/action_parser.c
    src/codegen/action_emit.c
    src/codegen/gen_assembler.c
    src/codegen/gen_disasm.c
    src/codegen/gen_gc.c
    src/codegen/gen_strings.c
    src/codegen/gen_closures.c
    src/codegen/gen_modules.c
    src/codegen/gen_fibers.c
    src/codegen/gen_native.c
    src/codegen/gen_build.c
)

target_include_directories(vmgen_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/testy
)

target_link_libraries(vmgen_tests PRIVATE yaml)

add_test(NAME vmgen_tests COMMAND vmgen_tests)
