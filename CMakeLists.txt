cmake_minimum_required(VERSION 3.16)
project(vmgen VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- libyaml (fetched via FetchContent) ---
include(FetchContent)

# libyaml 0.2.5 uses cmake_minimum_required(VERSION 2.6) which newer CMake rejects.
# Allow it to configure anyway.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

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
    src/codegen/gen_verify.c
    src/codegen/gen_optimizer.c
    src/codegen/gen_single_header.c
)

target_include_directories(vmgen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vmgen PRIVATE yaml tomlc99_lib)

# Compiler warnings — target-specific to avoid warning noise from dependencies
if(MSVC)
    target_compile_options(vmgen PRIVATE /W4)
else()
    target_compile_options(vmgen PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
    src/codegen/gen_verify.c
    src/codegen/gen_optimizer.c
    src/codegen/gen_single_header.c
)

target_include_directories(vmgen_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/testy
)

target_link_libraries(vmgen_tests PRIVATE yaml tomlc99_lib)

if(MSVC)
    target_compile_options(vmgen_tests PRIVATE /W4)
else()
    target_compile_options(vmgen_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

# vmgen_spec_t is ~127KB (instructions[256]); tests create 30+ on the stack.
# Increase stack size to 8MB so builds don't overflow.
if(MSVC)
    target_link_options(vmgen_tests PRIVATE /STACK:8388608)
else()
    target_link_options(vmgen_tests PRIVATE -Wl,-z,stacksize=8388608)
endif()

add_test(NAME vmgen_tests COMMAND vmgen_tests)

# End-to-end integration tests: generate → compile → run for each spec
set(E2E_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e_test.cmake")
set(E2E_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/e2e")

add_test(NAME e2e_minimal
    COMMAND ${CMAKE_COMMAND}
        -DVMGEN=$<TARGET_FILE:vmgen>
        -DSPEC=${CMAKE_CURRENT_SOURCE_DIR}/specs/minimal.yaml
        -DWORKDIR=${E2E_WORKDIR}/minimal
        -P ${E2E_SCRIPT}
)
set_tests_properties(e2e_minimal PROPERTIES TIMEOUT 120 DEPENDS vmgen)

add_test(NAME e2e_calculator
    COMMAND ${CMAKE_COMMAND}
        -DVMGEN=$<TARGET_FILE:vmgen>
        -DSPEC=${CMAKE_CURRENT_SOURCE_DIR}/specs/calculator.yaml
        -DWORKDIR=${E2E_WORKDIR}/calculator
        -P ${E2E_SCRIPT}
)
set_tests_properties(e2e_calculator PROPERTIES TIMEOUT 120 DEPENDS vmgen)

add_test(NAME e2e_full
    COMMAND ${CMAKE_COMMAND}
        -DVMGEN=$<TARGET_FILE:vmgen>
        -DSPEC=${CMAKE_CURRENT_SOURCE_DIR}/specs/full.yaml
        -DWORKDIR=${E2E_WORKDIR}/full
        -P ${E2E_SCRIPT}
)
set_tests_properties(e2e_full PROPERTIES TIMEOUT 120 DEPENDS vmgen)

add_test(NAME e2e_full_toml
    COMMAND ${CMAKE_COMMAND}
        -DVMGEN=$<TARGET_FILE:vmgen>
        -DSPEC=${CMAKE_CURRENT_SOURCE_DIR}/specs/full.toml
        -DWORKDIR=${E2E_WORKDIR}/full_toml
        -P ${E2E_SCRIPT}
)
set_tests_properties(e2e_full_toml PROPERTIES TIMEOUT 120 DEPENDS vmgen)
