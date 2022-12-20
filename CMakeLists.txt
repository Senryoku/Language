cmake_minimum_required(VERSION 3.22)
project(Lang)

enable_testing()
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    enable_testing()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE Debug)
endif()
set(Lang_VERSION_MINOR 1)
set(Lang_VERSION_MAJOR 0)
include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories("${CMAKE_SOURCE_DIR}/src/core")
include_directories("${CMAKE_SOURCE_DIR}/src/utils")
include_directories("${CMAKE_SOURCE_DIR}/src/compiler")

set(CMAKE_CXX_STANDARD 20)

if (MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

set(STDLIB_BASE_FOLDER "${PROJECT_SOURCE_DIR}/stdlib/exports/")
configure_file(
    "${PROJECT_SOURCE_DIR}/src/core/Config.hpp.in"
    "${PROJECT_SOURCE_DIR}/src/core/Config.hpp"
)

# Define sources
aux_source_directory("src/core" SOURCES)
aux_source_directory("src/core/formatters" SOURCES)
aux_source_directory("test/" TEST_FILES)
file(GLOB HEADERS src/**/*.hpp)

add_library(langlib STATIC ${SOURCES} ${HEADERS})

add_executable(repl ${HEADERS} src/repl.cpp src/repl/Prompt.cpp)
add_executable(tester ${TEST_FILES} ${HEADERS} test/main.cpp)

target_link_libraries(repl langlib)
target_link_libraries(tester langlib)

set_property(TARGET langlib PROPERTY CXX_STANDARD ${CMAKE_CXX_STANDARD})
set_property(TARGET repl PROPERTY CXX_STANDARD ${CMAKE_CXX_STANDARD})
set_property(TARGET tester PROPERTY CXX_STANDARD ${CMAKE_CXX_STANDARD})

set_property(TARGET tester PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/test")

target_include_directories(repl PRIVATE "${CMAKE_SOURCE_DIR}/src/repl")

# Use included version of FMT
set(FMT_ROOT "./ext/fmt-9.1.0")
add_subdirectory(${FMT_ROOT})
file(GLOB FMT_HEADERS "${FMT_ROOT}/include/*.hpp")
target_include_directories(langlib SYSTEM PRIVATE "${FMT_ROOT}/include")
target_include_directories(repl SYSTEM PRIVATE "${FMT_ROOT}/include")
target_include_directories(tester SYSTEM PRIVATE "${FMT_ROOT}/include")
target_precompile_headers(langlib PRIVATE ${FMT_HEADERS})
target_link_libraries(langlib fmt::fmt)

find_package(LLVM CONFIG)
if(DEFINED LLVM_PACKAGE_VERSION)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    message(STATUS "LLVM_INCLUDE_DIRS=${LLVM_INCLUDE_DIRS}")
    separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
    add_definitions(${LLVM_DEFINITIONS_LIST})
    llvm_map_components_to_libnames(llvm_libs support core irreader orcjit support native)
    foreach(target ${LLVM_TARGETS_TO_BUILD})
      list(APPEND targets "LLVM${target}CodeGen")
    endforeach()
    
    file(GLOB COMPILER_SRC src/compiler/*.cpp)
    add_executable(compiler ${HEADERS} src/compiler.cpp ${COMPILER_SRC})
    set_property(TARGET compiler PROPERTY CXX_STANDARD 20)
    target_include_directories(compiler SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/ext")
    target_include_directories(compiler SYSTEM PRIVATE "ext/fmt-9.1.0/include")
    target_include_directories(compiler SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

    target_link_libraries(compiler langlib)
    target_link_libraries(compiler fmt::fmt)
    target_link_libraries(compiler ${llvm_libs} ${targets})
else()
    message(WARNING "Could NOT find LLVM, skipping compiler.")
endif()

# Compile standard library
set(STDLIB_PATH ${CMAKE_SOURCE_DIR}/build/stdlib.o)
file(GLOB STDLIB_SOURCES "./stdlib/*.cpp" "./stdlib/**/*.cpp")
string(REGEX REPLACE ".cpp" ".o" STDLIB_OBJECTS "${STDLIB_SOURCES}")
string(REGEX REPLACE "stdlib/" "build/stdlib/" STDLIB_OBJECTS "${STDLIB_OBJECTS}")
foreach(src obj IN ZIP_LISTS STDLIB_SOURCES STDLIB_OBJECTS)
    add_custom_command( 
        OUTPUT ${obj} 
        DEPENDS ${src} 
        COMMAND clang++ -c ${src} -o ${obj} -std=c++20
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endforeach()
add_custom_command( 
    OUTPUT ${STDLIB_PATH} 
    DEPENDS ${STDLIB_OBJECTS} 
    COMMAND llvm-ar rvs ${STDLIB_PATH} ${STDLIB_OBJECTS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_custom_target(stdlib ALL DEPENDS ${STDLIB_PATH})
set_property(TARGET compiler APPEND PROPERTY OBJECT_DEPENDS ${STDLIB_PATH})
add_definitions(-DLANG_STDLIB_PATH="${STDLIB_PATH}")

# Get GTest online
include(FetchContent)
FetchContent_Declare(
  googletest
  # Specify the commit you depend on and update it regularly.
  URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
# Now simply link against gtest or gtest_main as needed.
target_link_libraries(tester gtest_main)
include(GoogleTest)

gtest_discover_tests(tester WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/test")

# Compiler tests
file(MAKE_DIRECTORY "ignore")
function(CompilerTest filename)
    file(STRINGS "${CMAKE_SOURCE_DIR}/test/compiler/${filename}.lang" EXPECTED LIMIT_COUNT 4)
    foreach(LINE ${EXPECTED})
        string(LENGTH ${LINE} LINE_LENGTH)
        if(LINE MATCHES "// RET : ")
            string(SUBSTRING ${LINE} 9 ${LINE_LENGTH} RET)
        endif()
        if(LINE MATCHES "// PASS: ")
            string(SUBSTRING ${LINE} 9 ${LINE_LENGTH} PASS)
        endif()
        if(LINE MATCHES "// FAIL: ")
            string(SUBSTRING ${LINE} 9 ${LINE_LENGTH} FAIL)
        endif()
        if(LINE MATCHES "// ARGS: ")
            string(SUBSTRING ${LINE} 9 ${LINE_LENGTH} ARGS)
        endif()
    endforeach()
    add_test(NAME "Compiler.${filename}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/ignore" COMMAND compiler "${CMAKE_SOURCE_DIR}/test/compiler/${filename}.lang" --bypass-cache -r ${ARGS})
    if(NOT ${RET} STREQUAL "")
        set_property(TEST "Compiler.${filename}" PROPERTY PASS_REGULAR_EXPRESSION "returned ${RET}")
    endif()
    if(NOT ${PASS} STREQUAL "")
        set_property(TEST "Compiler.${filename}" PROPERTY PASS_REGULAR_EXPRESSION ${PASS})
    endif()
    if(NOT ${FAIL} STREQUAL "")
        set_property(TEST "Compiler.${filename}" PROPERTY FAIL_REGULAR_EXPRESSION ${FAIL})
    endif()
endfunction()

file(GLOB COMPILER_TEST_FILES "${CMAKE_SOURCE_DIR}/test/compiler/*.lang")
foreach(FUll_PATH ${COMPILER_TEST_FILES})
    get_filename_component(FILENAME ${FUll_PATH} NAME_WE)
    CompilerTest(${FILENAME})
endforeach()
