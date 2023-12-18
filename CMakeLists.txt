cmake_minimum_required (VERSION 3.2.2)
project (webcachesim)
set(CMAKE_CXX_STANDARD 17)

#include(CheckCXXCompilerFlag)
#CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
#if(COMPILER_SUPPORTS_MARCH_NATIVE)
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
#endif()
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffast-math")

#logging eviction information
add_definitions(-DEVICTION_LOGGING)
#remove_definitions(-DEVICTION_LOGGING)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

SET(WEBCACHESIM_HEADER_DIR ${PROJECT_SOURCE_DIR}/include/webcachesim)

add_subdirectory(src)
add_subdirectory(integration)
