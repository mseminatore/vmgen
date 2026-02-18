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

# --- tomlc99 (fetched via FetchContent) ---
FetchContent_Declare(
    tomlc99
    GIT_REPOSITORY https://github.com/cktan/tomlc99.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(tomlc99)

# tomlc99 doesn't provide a CMake target; create one
add_library(tomlc99_lib STATIC
    ${tomlc99_SOURCE_DIR}/toml.c
)
target_include_directories(tomlc99_lib PUBLIC ${tomlc99_SOURCE_DIR})
if(MSVC)
    target_compile_options(tomlc99_lib PRIVATE /W3 /wd4996)
else()
    target_compile_options(tomlc99_lib PRIVATE -w)
endif()

# --- vmgen executable ---
add_executable(vmgen
    src/main.c
    src/util/buffer.c
    src/spec/model.c
    src/spec/parser.c
    src/spec/parser_toml.c
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
    src/codegen/gen_bytecode.c
    src/codegen/gen_test.c
    src/codegen/gen_linemap.c
    src/codegen/gen_single_header.c
)

target_include_directories(vmgen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vmgen PRIVATE yaml tomlc99_lib)

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
    src/spec/parser_toml.c
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
    src/codegen/gen_bytecode.c
    src/codegen/gen_test.c
    src/codegen/gen_linemap.c
    src/codegen/gen_single_header.c
)

target_include_directories(vmgen_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/testy
)

target_link_libraries(vmgen_tests PRIVATE yaml tomlc99_lib)

# vmgen_spec_t is ~127KB (instructions[256]); tests create several on the stack.
# Increase stack size to 4MB so Debug builds don't overflow.
if(MSVC)
    target_link_options(vmgen_tests PRIVATE /STACK:8388608)
else()
    target_link_options(vmgen_tests PRIVATE -Wl,-z,stacksize=4194304)
endif()

add_test(NAME vmgen_tests COMMAND vmgen_tests)
