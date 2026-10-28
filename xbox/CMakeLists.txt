# Prime-X: default.xbe. Included by the top-level CMakeLists.txt; configure
# with nxdk's toolchain file (xbox/build.sh).
#
# One triple for everything: nxdk's i386-pc-win32 (MSVC C++ ABI, nxdk's
# libc++), decided by the phase 0.2 spike (docs/decisions.md). Four kinds of
# code, four compile setups (docs/toolchain.md):
#   game  src/ (MetroidPrimePort's PrimeDecomp, C++) and extern/musyx (C)
#   port  xbox/src/port: the Port* hooks the game calls, Xbox versions (C++)
#   sdk   xbox/src/sdk (+ gx/): the Dolphin SDK on the Xbox (C, from Melee-X)
#   hw    xbox/src/hw: kernel, pbkit, AC97, USB; NV2A back end (C, from Melee-X)
# The build is configured from the checkout's root so object paths stay short:
# an archive's or the link's command line holds every object path, and Windows
# stops at 32 KB (the game is split in two libraries for the same reason).
set(ROOT "${CMAKE_SOURCE_DIR}")
set(XBOX "${ROOT}/xbox")
# __FILE__ relative to the checkout: no builder's home directory in the XBE
get_filename_component(ROOT_ABS "${ROOT}" ABSOLUTE)
add_compile_options("-ffile-prefix-map=${ROOT_ABS}/=")
set(NXDK_DIR $ENV{NXDK_DIR})
option(PX_GAME_ONLY "Compile the game objects only (no SDK, no link)" OFF)
# PX_LTO (xbox/build.sh: XBOX_LTO=1): game, port and sdk code as bitcode,
# ThinLTO in lld-link. PX_PGO (XBOX_PGO): "gen" instruments the same code
# (counters out through xbox/src/hw/xhw_pgo.c), a .profdata path optimizes
# with it (docs/pgo.md). hw and pbkit stay plain: the runtime that dumps
# the counters must not count itself.
option(PX_LTO "ThinLTO over the game, port and sdk code" OFF)
# lld's default import limit (100) grew .text by 1.75 MB; 30 with the
# profile: +0.3 MB over the plain build (docs/pgo.md)
set(PX_LTO_LINK_FLAGS "-mllvm:-import-instr-limit=30" CACHE STRING "lld-link flags for the LTO backends")
set(PX_PGO "" CACHE STRING "PGO: empty, gen, or a .profdata path")
set(PX_OPT_FLAGS)
if(PX_LTO)
    list(APPEND PX_OPT_FLAGS -flto=thin)
endif()
if(PX_PGO STREQUAL "gen")
    # no compiler-rt: no value profiling; static functions named by their
    # file's name alone, so the -pgogen and -pgo folders' objects agree
    list(APPEND PX_OPT_FLAGS -fprofile-generate "SHELL:-mllvm -disable-vp"
        "SHELL:-mllvm -static-func-full-module-prefix=false")
    add_compile_definitions(XHW_PGO=1)
elseif(PX_PGO)
    list(APPEND PX_OPT_FLAGS -fprofile-use=${PX_PGO} "SHELL:-mllvm -static-func-full-module-prefix=false")
endif()

# ---- game: src/, extern/musyx ----
include(${XBOX}/game_files.cmake)
list(TRANSFORM PX_GAME_SOURCES PREPEND "${ROOT}/")
list(APPEND PX_GAME_SOURCES ${ROOT}/src/MetroidPrime/main.cpp)
file(GLOB_RECURSE PX_MUSYX_SOURCES CONFIGURE_DEPENDS ${ROOT}/extern/musyx/src/musyx/runtime/*.c)
list(FILTER PX_MUSYX_SOURCES EXCLUDE REGEX "/profile\\.c$")

set(PX_GAME_INCLUDES
    ${XBOX}/src/port/include
    ${XBOX}/include/game
    ${XBOX}/include/sdk
    ${ROOT}/include
    ${ROOT}/extern/aurora/include
    ${ROOT}/extern/musyx/include)
set(PX_GAME_DEFS TARGET_PC=1 TARGET_XBOX=1 VERSION=0 MUSY_TARGET=0 NDEBUG)
# Same floating-point contract as MetroidPrimePort's game: no contraction, no
# fast-math; SSE so float math rounds to single like Gekko, not x87's 80 bits.
# Plain Pentium III: SSE1 only (docs/boot-safety.md).
# nxdk's wrapper passes -ffreestanding -fno-builtin; -fhosted -fbuiltin after
# it gives the game the builtins back: fabsf is andps, sqrtf sqrtss (correctly
# rounded either way), fixed-size memcpy/memset inline moves. Without errno
# the maths can inline (docs/plan.md phase 6, "Compiler optimizations").
set(PX_GAME_FLAGS -O2 -msse -mfpmath=sse -ffp-contract=off -fno-strict-aliasing -fwrapv
    -ffunction-sections -Wno-everything -Werror=implicit-function-declaration -Werror=return-type
    -fhosted -fbuiltin -fno-math-errno ${PX_OPT_FLAGS})
# C++: no RTTI; `throw` and `try` compile (-fcxx-exceptions) but without
# unwind tables, so a throw is fatal (xbox/src/port/px_eh.cpp) and catch arms
# are dead code. MSVC picks a member-function pointer's size from whether the
# class is complete; the general (virtual) representation everywhere, as
# MetroidPrimePort's /vmg (its CMakeLists.txt, issue #4).
set(PX_GAME_CXX_FLAGS -std=c++20 -fno-rtti "SHELL:-Xclang -fcxx-exceptions"
    "SHELL:-Xclang -fms-memptr-rep=virtual" -Wno-c++11-narrowing)

set(PX_GAME_MP_SOURCES ${PX_GAME_SOURCES})
list(FILTER PX_GAME_MP_SOURCES INCLUDE REGEX "/src/MetroidPrime/")
list(FILTER PX_GAME_SOURCES EXCLUDE REGEX "/src/MetroidPrime/")
foreach(lib px_game px_game_mp)
    if(lib STREQUAL px_game)
        add_library(${lib} STATIC ${PX_GAME_SOURCES})
    else()
        add_library(${lib} STATIC ${PX_GAME_MP_SOURCES})
    endif()
    target_include_directories(${lib} BEFORE PRIVATE ${PX_GAME_INCLUDES})
    target_compile_definitions(${lib} PRIVATE ${PX_GAME_DEFS})
    target_compile_options(${lib} PRIVATE ${PX_GAME_FLAGS}
        $<$<COMPILE_LANGUAGE:CXX>:${PX_GAME_CXX_FLAGS}>
        $<$<COMPILE_LANGUAGE:C>:-std=c2x>
        "SHELL:-include ${XBOX}/include/game/prime_compat.h")
endforeach()

add_library(px_musyx STATIC ${PX_MUSYX_SOURCES})
target_include_directories(px_musyx BEFORE PRIVATE ${PX_GAME_INCLUDES})
target_compile_definitions(px_musyx PRIVATE ${PX_GAME_DEFS})
target_compile_options(px_musyx PRIVATE ${PX_GAME_FLAGS} -std=c2x
    "SHELL:-include ${XBOX}/include/game/prime_compat.h")

if(PX_GAME_ONLY)
    return()
endif()

# ---- pbkit, patched so a GPU fault can't freeze the console ----
set(PBKIT_SRC ${NXDK_DIR}/lib/pbkit)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/pbkit/pbkit.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/pbkit
    COMMAND python3 ${ROOT}/tools/xbox/patch_pbkit.py ${PBKIT_SRC}/pbkit.c ${CMAKE_BINARY_DIR}/pbkit/pbkit.c
    DEPENDS ${PBKIT_SRC}/pbkit.c ${ROOT}/tools/xbox/patch_pbkit.py
    COMMENT "patch pbkit.c")
add_library(px_pbkit STATIC
    ${CMAKE_BINARY_DIR}/pbkit/pbkit.c
    ${PBKIT_SRC}/pbkit_gamma.c ${PBKIT_SRC}/pbkit_dma.c ${PBKIT_SRC}/pbkit_draw.c
    ${PBKIT_SRC}/pbkit_print.c ${PBKIT_SRC}/pbkit_pushbuffer.c)
target_include_directories(px_pbkit PRIVATE ${PBKIT_SRC})
target_compile_options(px_pbkit PRIVATE -w)

# ---- port: the Port* hooks and runtime pieces the game calls (C++) ----
file(GLOB PX_PORT_SOURCES CONFIGURE_DEPENDS ${XBOX}/src/port/*.cpp
    ${XBOX}/src/port/*.c ${XBOX}/src/port/thp/*.cpp)
add_library(px_port STATIC ${PX_PORT_SOURCES})
target_include_directories(px_port BEFORE PRIVATE ${PX_GAME_INCLUDES} ${XBOX}/include ${NXDK_DIR}/lib/zlib/zlib)
# settings.ini for the Options rows (port_xbox_options.cpp)
target_include_directories(px_port AFTER PRIVATE ${XBOX}/src/sdk)
target_compile_definitions(px_port PRIVATE ${PX_GAME_DEFS})
target_compile_options(px_port PRIVATE ${PX_GAME_FLAGS}
    $<$<COMPILE_LANGUAGE:CXX>:${PX_GAME_CXX_FLAGS}>
    "SHELL:-include ${XBOX}/include/game/prime_compat.h")

# ---- sdk: the Dolphin SDK on the Xbox (C) ----
file(GLOB PX_SDK_SOURCES CONFIGURE_DEPENDS ${XBOX}/src/sdk/*.c
    ${XBOX}/src/sdk/gx/*.c ${ROOT}/extern/aurora/lib/dolphin/mtx/*.c)
add_library(px_sdk STATIC ${PX_SDK_SOURCES})
target_include_directories(px_sdk BEFORE PRIVATE
    ${XBOX}/include/sdk
    ${XBOX}/include/game
    ${XBOX}/include
    ${ROOT}/extern/aurora/include
    ${XBOX}/src/port/include
    ${XBOX}/src/sdk)
target_compile_definitions(px_sdk PRIVATE TARGET_PC=1 TARGET_XBOX=1)
target_compile_options(px_sdk PRIVATE -O2 -msse -mfpmath=sse -fno-strict-aliasing -fwrapv -ffp-contract=off
    -ffunction-sections -Wno-everything -Werror=implicit-function-declaration -Werror=implicit-int ${PX_OPT_FLAGS}
    "SHELL:-include ${XBOX}/include/sdk/xsdk_prelude.h")
# log.c defines the printf-family redirections the prelude points at
set_source_files_properties(${XBOX}/src/sdk/log.c PROPERTIES COMPILE_DEFINITIONS XSDK_NO_STDIO_RENAME)

# ---- hw: kernel, pbkit, AC97, USB (C) ----
file(GLOB PX_HW_SOURCES CONFIGURE_DEPENDS ${XBOX}/src/hw/*.c)
add_library(px_hw STATIC ${PX_HW_SOURCES})
target_include_directories(px_hw PRIVATE
    ${XBOX}/include
    ${XBOX}/src/hw
    ${NXDK_DIR}/lib/sdl/SDL2/include
    ${NXDK_DIR}/lib/usb/libusbohci/inc
    ${NXDK_DIR}/lib/usb/libusbohci_xbox
    ${NXDK_DIR}/lib/zlib/zlib)
target_compile_definitions(px_hw PRIVATE XBOX "USBH_USE_EXTERNAL_CONFIG=\"usbh_config_xbox.h\"")
target_compile_options(px_hw PRIVATE -O2 -march=pentium3 -ffunction-sections -Wall -Wno-unused-function
    "SHELL:-include ${XBOX}/include/xbuiltin.h")
# the string routines must not be recognised as themselves (xhw_string.c)
set_source_files_properties(${XBOX}/src/hw/xhw_string.c PROPERTIES
    COMPILE_OPTIONS "-fno-builtin;-ffreestanding" COMPILE_DEFINITIONS XBUILTIN_OFF)

# ---- link ----
# ~700 objects are past the command-line limit, so each part is a static
# library linked whole: every object goes in, as MetroidPrimePort links its
# registration-only units (its CMakeLists.txt, "explicit source manifest")
file(WRITE ${CMAKE_BINARY_DIR}/empty.c "")
add_executable(prime_x ${CMAKE_BINARY_DIR}/empty.c)
set_target_properties(prime_x PROPERTIES LINKER_LANGUAGE CXX)
add_dependencies(prime_x px_game px_game_mp px_musyx px_port px_sdk px_hw)
foreach(lib px_game px_game_mp px_musyx px_port px_sdk px_hw)
    target_link_options(prime_x PRIVATE "-wholearchive:$<TARGET_FILE:${lib}>")
endforeach()
target_link_libraries(prime_x
    px_pbkit
    ${NXDK_DIR}/lib/libSDL2.lib
    ${NXDK_DIR}/lib/libzlib.lib
    ${NXDK_DIR}/lib/libnxdk_automount_d.lib)
target_link_options(prime_x PRIVATE
    -stack:1048576
    -include:_automount_d_drive
    -map:${CMAKE_BINARY_DIR}/prime_x.map
    # functions are COMDAT sections (-ffunction-sections): never fold two
    # identical ones into one address, the game compares function pointers
    -opt:noicf
    -errorlimit:0)
if(PX_LTO)
    # Functions the code generator calls on its own (a throw's
    # _CxxThrowException, the maths and libc fill-ins) must be native
    # objects: in bitcode, LTO drops them before codegen asks for them.
    set_source_files_properties(${XBOX}/src/port/px_eh.cpp ${XBOX}/src/sdk/libm_fill.c ${XBOX}/src/sdk/libc_fill.c
        PROPERTIES COMPILE_OPTIONS -fno-lto)
    # the backends on every core; unchanged modules come from the cache
    target_link_options(prime_x PRIVATE -opt:lldltojobs=all -lldltocache:${CMAKE_BINARY_DIR}/lto-cache
        ${PX_LTO_LINK_FLAGS})
endif()
# The code's layout from the PGO profile (tools/xbox/make_order.py): the
# functions the training ran first, hottest first, the rest in link order.
# Only with the profile it came from: a name the link doesn't have is
# skipped without a warning.
set(PX_ORDER_FILE ${XBOX}/order.txt)
if(PX_PGO AND NOT PX_PGO STREQUAL "gen" AND EXISTS ${PX_ORDER_FILE})
    target_link_options(prime_x PRIVATE "-order:@${PX_ORDER_FILE}" -ignore:4037)
    set_property(TARGET prime_x APPEND PROPERTY LINK_DEPENDS ${PX_ORDER_FILE})
endif()
if(PX_PGO STREQUAL "gen")
    # nothing refers to the version word; tools/xbox/pgo_raw.py reads it
    # (and clang asks for compiler-rt's profile library, which nxdk has not: xhw_pgo.c)
    target_link_options(prime_x PRIVATE -include:___llvm_profile_raw_version -nodefaultlib:clang_rt.profile.lib)
endif()
add_custom_command(TARGET prime_x POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/xbe
    COMMAND ${NXDK_DIR}/tools/cxbe/cxbe "-OUT:${CMAKE_BINARY_DIR}/xbe/default.xbe" "-TITLE:Prime-X" $<TARGET_FILE:prime_x>
    COMMENT "cxbe -> xbe/default.xbe")
