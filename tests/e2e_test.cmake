# e2e_test.cmake — End-to-end integration test for vmgen
#
# Usage: cmake -P e2e_test.cmake -DVMGEN=<path> -DSPEC=<spec> -DWORKDIR=<dir>
# Or via CTest: set VMGEN, SPEC, WORKDIR before running
#
# Steps:
#   1. Run vmgen on the spec to generate code
#   2. Configure the generated project with cmake
#   3. Build the generated project
#   4. Run the generated test executable
#   5. Verify exit code = 0

cmake_minimum_required(VERSION 3.16)

# Validate inputs
if(NOT VMGEN)
    message(FATAL_ERROR "VMGEN not set (path to vmgen executable)")
endif()
if(NOT SPEC)
    message(FATAL_ERROR "SPEC not set (path to .yaml spec file)")
endif()
if(NOT WORKDIR)
    message(FATAL_ERROR "WORKDIR not set (working directory for generated output)")
endif()

# Clean and create work directory
file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")

# Step 1: Generate code
message(STATUS "E2E: Generating from ${SPEC} into ${WORKDIR}")
execute_process(
    COMMAND "${VMGEN}" "${SPEC}" -o "${WORKDIR}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "vmgen failed (rc=${rc}):\n${out}\n${err}")
endif()
message(STATUS "E2E: Generation succeeded")

# Step 2: Configure generated project
file(MAKE_DIRECTORY "${WORKDIR}/build")
message(STATUS "E2E: Configuring generated project")
execute_process(
    COMMAND "${CMAKE_COMMAND}" ..
        -DCMAKE_C_STANDARD=11
    WORKING_DIRECTORY "${WORKDIR}/build"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "cmake configure failed (rc=${rc}):\n${out}\n${err}")
endif()

# Step 3: Build generated project
message(STATUS "E2E: Building generated project")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build . --config Debug
    WORKING_DIRECTORY "${WORKDIR}/build"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "build failed (rc=${rc}):\n${out}\n${err}")
endif()

# Check for warnings in build output
string(FIND "${out}" "warning" warn_pos)
string(FIND "${err}" "warning" warn_pos2)
if(NOT warn_pos EQUAL -1 OR NOT warn_pos2 EQUAL -1)
    message(WARNING "Build produced warnings:\n${out}\n${err}")
endif()
message(STATUS "E2E: Build succeeded")

# Step 4: Run generated tests
# Find the test executable
# Try multi-config layout first (MSVC: build/Debug/*.exe)
file(GLOB test_exes_dbg "${WORKDIR}/build/Debug/*_tests*")
file(GLOB test_exes_rel "${WORKDIR}/build/Release/*_tests*")
file(GLOB test_exes_top "${WORKDIR}/build/*_tests*")

set(test_exe "")
foreach(candidate ${test_exes_dbg} ${test_exes_rel} ${test_exes_top})
    # Skip .obj .lib .pdb .ilk files
    if(NOT candidate MATCHES "\\.(obj|o|lib|pdb|ilk)$")
        set(test_exe "${candidate}")
        break()
    endif()
endforeach()

if(NOT test_exe OR NOT EXISTS "${test_exe}")
    message(FATAL_ERROR "Could not find test executable in ${WORKDIR}/build")
endif()

message(STATUS "E2E: Running tests: ${test_exe}")
execute_process(
    COMMAND "${test_exe}"
    WORKING_DIRECTORY "${WORKDIR}/build"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    TIMEOUT 60
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Generated tests failed (rc=${rc}):\n${out}\n${err}")
endif()

message(STATUS "E2E: All tests passed!")
message(STATUS "${out}")
