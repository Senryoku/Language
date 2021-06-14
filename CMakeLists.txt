cmake_minimum_required(VERSION 3.2)
project(Lang)

if(CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(Lang_VERSION_MINOR 1)
set(Lang_VERSION_MAJOR 0)
include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories("${CMAKE_SOURCE_DIR}/src/core")

if (MSVC)
    # warning level 4 and all warnings as errors
    add_compile_options(/W4)
else()
    # lots of warnings and all warnings as errors
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Define sources and executable
set(EXECUTABLE_NAME "compiler")
aux_source_directory("src/core" SOURCES)
file(GLOB HEADERS src/**/*.hpp)

add_executable(compiler ${SOURCES} ${HEADERS} src/compiler.cpp)
add_executable(prompt ${SOURCES} ${HEADERS} src/prompt.cpp)

# FIXME
#find_package(fmt)
#target_link_libraries(compiler fmt::fmt)
#target_link_libraries(compiler PRIVATE fmt::fmt-header-only)

set_property(TARGET compiler PROPERTY CXX_STANDARD 20)
set_property(TARGET prompt PROPERTY CXX_STANDARD 20)

target_link_libraries(compiler)
