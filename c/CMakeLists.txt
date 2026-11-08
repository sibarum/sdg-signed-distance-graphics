cmake_minimum_required(VERSION 3.16)
project(sdg C)

# SDG in C: the library (read a compiled drawing, rasterise it), the sdgrender tool, and the conformance test.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(sdg src/read.c src/render.c src/png.c)
target_include_directories(sdg PUBLIC include)
if(NOT MSVC)
    target_link_libraries(sdg PUBLIC m)
    target_compile_options(sdg PRIVATE -Wall -Wextra -O2)
else()
    target_compile_options(sdg PRIVATE /W3 /O2)
    target_compile_definitions(sdg PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(sdgrender tools/sdgrender.c)
target_link_libraries(sdgrender PRIVATE sdg)

add_executable(conformance tests/conformance.c)
target_link_libraries(conformance PRIVATE sdg)

enable_testing()
add_test(NAME conformance COMMAND conformance ${CMAKE_CURRENT_SOURCE_DIR}/../conformance)
