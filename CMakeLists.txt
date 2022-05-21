cmake_minimum_required(VERSION 3.23)
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
include_directories("${CMAKE_SOURCE_DIR}/ext")

if (MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Define sources
aux_source_directory("src/core" SOURCES)
aux_source_directory("test/" TEST_FILES)
file(GLOB HEADERS src/**/*.hpp)

add_library(langlib STATIC ${SOURCES} ${HEADERS})

add_executable(compiler ${HEADERS} src/compiler.cpp)
add_executable(repl ${HEADERS} src/repl.cpp)
add_executable(tester ${TEST_FILES} ${HEADERS} test/main.cpp)
add_executable(assembler_test ${HEADERS} src/assembler_test.cpp)

target_include_directories(compiler PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ext")

target_link_libraries(compiler langlib)
target_link_libraries(repl langlib)
target_link_libraries(tester langlib)
target_link_libraries(assembler_test langlib)

set_property(TARGET langlib PROPERTY CXX_STANDARD 20)
set_property(TARGET compiler PROPERTY CXX_STANDARD 20)
set_property(TARGET repl PROPERTY CXX_STANDARD 20)
set_property(TARGET tester PROPERTY CXX_STANDARD 20)
set_property(TARGET assembler_test PROPERTY CXX_STANDARD 20)

set_property(TARGET tester PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/test")

# Use included version of FMT
add_subdirectory("./ext/fmt-8.1.1")
include_directories(SYSTEM "ext/fmt-8.1.1/include")
target_link_libraries(compiler fmt::fmt)
target_link_libraries(repl fmt::fmt)
target_link_libraries(tester fmt::fmt)
target_link_libraries(assembler_test fmt::fmt)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
llvm_map_components_to_libnames(llvm_libs support core irreader orcjit support native)
foreach(target ${LLVM_TARGETS_TO_BUILD})
  list(APPEND targets "LLVM${target}CodeGen")
endforeach()

target_include_directories(compiler SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(compiler ${llvm_libs} ${targets})

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