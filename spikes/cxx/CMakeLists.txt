# Phase 0.2 spike XBE (throwaway): game C++ on nxdk's own triple.
cmake_minimum_required(VERSION 3.16)
project(px_spike C CXX)
set(MPP ${CMAKE_CURRENT_SOURCE_DIR}/../../ref/MetroidPrimePort)
set(NXDK_DIR $ENV{NXDK_DIR})
file(GLOB MATH ${MPP}/src/Kyoto/Math/*.cpp)
add_executable(px_spike main.cpp checks.cpp ${MATH}
    ${MPP}/src/Kyoto/Streams/CInputStream.cpp ${MPP}/src/Kyoto/Streams/CMemoryInStream.cpp
    ${MPP}/src/Kyoto/Streams/COutputStream.cpp ${MPP}/src/Kyoto/rstl/rstl_strings.cpp)
target_include_directories(px_spike PRIVATE ${MPP}/platform/include ${MPP}/include ${MPP}/extern/aurora/include
    ${MPP}/extern/musyx/include)
target_compile_definitions(px_spike PRIVATE TARGET_PC=1 TARGET_XBOX=1 MUSY_TARGET=0)
target_compile_options(px_spike PRIVATE -O2 -msse -mfpmath=sse -fno-rtti -std=c++20 -Wno-everything
    "SHELL:-Xclang -fms-memptr-rep=virtual"
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/prelude.h" "SHELL:-include ${MPP}/platform/compat.h")
set_source_files_properties(main.cpp checks.cpp PROPERTIES COMPILE_OPTIONS "-Wno-everything")
target_link_libraries(px_spike ${NXDK_DIR}/lib/libnxdk_automount_d.lib)
target_link_options(px_spike PRIVATE -include:_automount_d_drive)
add_custom_command(TARGET px_spike POST_BUILD
    COMMAND ${NXDK_DIR}/tools/cxbe/cxbe "-OUT:${CMAKE_BINARY_DIR}/default.xbe" "-TITLE:Prime-X spike" $<TARGET_FILE:px_spike>)
