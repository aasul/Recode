cmake_minimum_required(VERSION 3.16)
function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${ARGV}\n${output}\n${errors}")
    endif()
endfunction()
file(REMOVE_RECURSE "${TEST_DIR}")
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${TEST_DIR}/library"
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=${C_COMPILER}"
    "-DBUILD_SHARED_LIBS=${SHARED}" -DBUILD_TESTING=OFF -DRECODE_BUILD_TOOLS=OFF
    "-DCMAKE_INSTALL_PREFIX=${TEST_DIR}/prefix")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/library" --parallel 2)
run("${CMAKE_COMMAND}" --install "${TEST_DIR}/library")
# Relocation catches accidental build/prefix paths in the package exports.
file(RENAME "${TEST_DIR}/prefix" "${TEST_DIR}/relocated")
file(GLOB_RECURSE exports "${TEST_DIR}/relocated/*.cmake")
foreach(export IN LISTS exports)
    file(READ "${export}" contents)
    string(FIND "${contents}" "${SOURCE_DIR}" source_path)
    string(FIND "${contents}" "${TEST_DIR}/prefix" prefix_path)
    if(NOT source_path EQUAL -1 OR NOT prefix_path EQUAL -1)
        message(FATAL_ERROR "Nonrelocatable export: ${export}")
    endif()
endforeach()
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/examples/embed" -B "${TEST_DIR}/consumer"
    "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/consumer" --parallel 2)
run("${TEST_DIR}/consumer/embed")
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/examples/rules" -B "${TEST_DIR}/rules"
    "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/rules" --parallel 2)
run("${TEST_DIR}/rules/rules")

run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/examples/data-rules" -B "${TEST_DIR}/data-rules"
    "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_PREFIX_PATH=${TEST_DIR}/relocated")
run("${CMAKE_COMMAND}" --build "${TEST_DIR}/data-rules" --parallel 2)
run("${TEST_DIR}/data-rules/data-rules")
