cmake_minimum_required(VERSION 3.16)
project(sdg C)

# SDG in C: the library (read a compiled drawing, rasterise it) as a static library and as a shared one (sdg.dll,
# libsdg.so, libsdg.dylib, for a binding from another language), the sdgrender tool, and the conformance test.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SDG_SOURCES src/read.c src/render.c src/png.c)

function(sdg_options target)
    target_include_directories(${target} PUBLIC include)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /O2)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_link_libraries(${target} PUBLIC m)
        target_compile_options(${target} PRIVATE -Wall -Wextra -O2)
    endif()
endfunction()

add_library(sdg_static STATIC ${SDG_SOURCES})
sdg_options(sdg_static)

add_library(sdg SHARED ${SDG_SOURCES})
sdg_options(sdg)
target_compile_definitions(sdg PRIVATE SDG_SHARED_BUILD)
set_target_properties(sdg PROPERTIES C_VISIBILITY_PRESET hidden)

add_executable(sdgrender tools/sdgrender.c)
target_link_libraries(sdgrender PRIVATE sdg_static)

add_executable(conformance tests/conformance.c)
target_link_libraries(conformance PRIVATE sdg_static)

enable_testing()
add_test(NAME conformance COMMAND conformance ${CMAKE_CURRENT_SOURCE_DIR}/../conformance)
