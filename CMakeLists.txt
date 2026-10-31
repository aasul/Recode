cmake_minimum_required(VERSION 3.16)
project(recode VERSION 0.5.0 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR
   NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Recode requires Linux x86-64 with 64-bit pointers")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(RECODE_THREAD_SANITIZE "Enable ThreadSanitizer" OFF)
option(RECODE_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Werror)
if(RECODE_THREAD_SANITIZE AND RECODE_SANITIZE)
    message(FATAL_ERROR "Select ThreadSanitizer or ASan/UBSan, not both")
endif()
if(RECODE_THREAD_SANITIZE)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
if(RECODE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Generated functions do not carry Clang's function-type metadata.
        add_compile_options(-fno-sanitize=function)
    endif()
    add_link_options(-fsanitize=address,undefined)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
configure_file(include/recode_version.h.in generated/recode_version.h @ONLY)
add_library(recode src/ir.c src/decoder.c src/optimizer.c src/codegen.c src/runtime.c src/interpreter.c src/adaptive.c src/trace.c)
add_library(Recode::recode ALIAS recode)
target_include_directories(recode PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_compile_features(recode PUBLIC c_std_11)
set_target_properties(recode PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)
target_link_libraries(recode PUBLIC Threads::Threads)
option(RECODE_BUILD_TOOLS "Build demos and benchmarks" ON)
if(RECODE_BUILD_TOOLS)
    add_executable(recode_demo src/demo.c)
    target_link_libraries(recode_demo PRIVATE recode)
    add_executable(recode_trace_bench src/trace_bench.c)
    target_include_directories(recode_trace_bench PRIVATE src)
    target_link_libraries(recode_trace_bench PRIVATE recode)
    add_executable(recode_runtime_bench src/runtime_bench.c)
    target_link_libraries(recode_runtime_bench PRIVATE recode)
    add_executable(recode_bench src/bench.c)
    target_compile_definitions(recode_bench PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(recode_bench PRIVATE recode)
endif()
include(CTest)
if(BUILD_TESTING)
    # Linker wrapping must reach library calls even when testing a shared build.
    set(failure_library recode)
    if(BUILD_SHARED_LIBS)
        get_target_property(recode_sources recode SOURCES)
        add_library(recode_failure_static STATIC ${recode_sources})
        target_include_directories(recode_failure_static PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/generated PRIVATE src)
        target_link_libraries(recode_failure_static PUBLIC Threads::Threads)
        set(failure_library recode_failure_static)
    endif()
    foreach(shared IN ITEMS OFF ON)
        add_test(NAME embedding-${shared} COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/embedding-${shared}
            -DC_COMPILER=${CMAKE_C_COMPILER} -DSHARED=${shared}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestEmbedding.cmake)
        set_tests_properties(embedding-${shared} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endforeach()
    add_executable(recode_trace_tests tests/test_trace.c)
    target_include_directories(recode_trace_tests PRIVATE src)
    target_link_libraries(recode_trace_tests PRIVATE recode)
    add_test(NAME trace COMMAND recode_trace_tests)
    set_tests_properties(trace PROPERTIES TIMEOUT 90)
    add_executable(recode_adaptive_failure_tests tests/test_adaptive_failures.c)
    target_link_libraries(recode_adaptive_failure_tests PRIVATE ${failure_library})
    target_link_options(recode_adaptive_failure_tests PRIVATE
        -Wl,--wrap=malloc -Wl,--wrap=mmap -Wl,--wrap=mprotect -Wl,--wrap=munmap
        -Wl,--wrap=clock_gettime -Wl,--wrap=calloc -Wl,--wrap=free
        -Wl,--wrap=pthread_mutex_init)
    add_test(NAME adaptive_failures COMMAND recode_adaptive_failure_tests)
    set_tests_properties(adaptive_failures PROPERTIES TIMEOUT 30)
    add_executable(recode_adaptive_tests tests/test_adaptive.c)
    target_include_directories(recode_adaptive_tests PRIVATE src)
    target_link_libraries(recode_adaptive_tests PRIVATE recode)
    add_test(NAME adaptive COMMAND recode_adaptive_tests)
    set_tests_properties(adaptive PROPERTIES TIMEOUT 90)
    add_executable(recode_tests tests/test_recode.c)
    target_compile_definitions(recode_tests PRIVATE _GNU_SOURCE)
    target_link_libraries(recode_tests PRIVATE recode)
    add_executable(recode_failure_tests tests/test_failures.c)
    target_compile_definitions(recode_failure_tests PRIVATE _GNU_SOURCE)
    target_link_libraries(recode_failure_tests PRIVATE ${failure_library})
    target_link_options(recode_failure_tests PRIVATE
        -Wl,--wrap=malloc -Wl,--wrap=mmap -Wl,--wrap=mprotect -Wl,--wrap=munmap)
    add_executable(recode_control_flow_tests tests/test_control_flow.c)
    target_compile_definitions(recode_control_flow_tests PRIVATE _GNU_SOURCE)
    target_include_directories(recode_control_flow_tests PRIVATE src)
    target_link_libraries(recode_control_flow_tests PRIVATE recode)
    add_test(NAME control_flow COMMAND recode_control_flow_tests)
    set_tests_properties(control_flow PROPERTIES TIMEOUT 90)
    add_test(NAME failures COMMAND recode_failure_tests)
    add_test(NAME translator COMMAND recode_tests)
    if(RECODE_BUILD_TOOLS)
        add_test(NAME demo COMMAND recode_demo)
        set_tests_properties(demo PROPERTIES TIMEOUT 30)
    endif()
    set_tests_properties(translator failures PROPERTIES TIMEOUT 30)
endif()

install(TARGETS recode EXPORT RecodeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/recode.h ${CMAKE_CURRENT_BINARY_DIR}/generated/recode_version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT RecodeTargets NAMESPACE Recode:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Recode)
configure_package_config_file(cmake/RecodeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/RecodeConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Recode)
# Experimental releases require the same minor version. This is no ABI promise.
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/RecodeConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/RecodeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/RecodeConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Recode)
