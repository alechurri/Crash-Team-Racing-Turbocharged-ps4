# PlayStation 4 (OpenOrbis toolchain + the orbis-sdk-v1 bundle from orbis-ports).
#
# SDL is not built on this platform. upstream SDL3 has no PS4 support, and SDL does not accept
# AI-generated code (externals/SDL/CLAUDE.md), so this port does not add a backend to it. Instead
# ps4/ provides the subset of the SDL3 API this game calls, implemented on the console's own APIs,
# compiled against SDL's public headers unmodified. The game links SDL3::SDL3 exactly as before.
#
# Graphics: Mesa's EGL "orbis" platform -> zink (GL on Vulkan) -> RADV -> video out. Desktop GL
# (the game's PC renderer, GL 3.3 core) is available through eglGetProcAddress.

if(NOT CTR_NATIVE_64BIT)
    message(FATAL_ERROR "The PS4 build needs -DCTR_NATIVE_64BIT=ON (the console runs 64-bit code only).")
endif()
foreach(_v ORBIS_COMPAT_DIR ORBIS_MESA_SRC ORBIS_MESA_BUILD)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "${_v} is not set: configure with the orbis-sdk-v1 toolchain file.")
    endif()
endforeach()

# Jaguar code generation and frame pointers (the crash handler walks them) for everything below.
add_compile_options(-march=btver2 -fno-omit-frame-pointer)

# An OBJECT library: its objects go into the executable whole, so the early constructor in
# ps4_platform.c and the wide-character functions are always linked.
add_library(ctr_ps4_platform OBJECT
    ps4/ps4_sdl.c
    ps4/ps4_platform.c
    ps4/ps4_wide.c
)
set_source_files_properties(ps4/ps4_wide.c PROPERTIES COMPILE_OPTIONS "-fno-builtin")
target_include_directories(ctr_ps4_platform PRIVATE "${CMAKE_SOURCE_DIR}")
string(TIMESTAMP _ctr_ps4_stamp "%Y%m%dT%H%M%SZ" UTC)
execute_process(COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --short=9 HEAD
    OUTPUT_VARIABLE _ctr_ps4_rev OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(CTR_PS4_BUILD_ID "${_ctr_ps4_stamp}-${_ctr_ps4_rev}")
target_compile_definitions(ctr_ps4_platform PRIVATE CTR_PS4_BUILD_ID="${CTR_PS4_BUILD_ID}")
set_target_properties(ctr_ps4_platform PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON)
target_include_directories(ctr_ps4_platform PUBLIC "${CMAKE_SOURCE_DIR}/externals/SDL/include")
target_include_directories(ctr_ps4_platform PRIVATE "${ORBIS_MESA_SRC}/include")
target_compile_definitions(ctr_ps4_platform PUBLIC SDL_MAIN_HANDLED)

# The Vulkan C ABI for zink: RADV is statically linked and reached through orbis-compat's loader
# shim (no libvulkan to dlopen on this console).
set(_ctr_vkl "${ORBIS_COMPAT_DIR}/vkloader")
add_library(ctr_ps4_vkloader STATIC "${_ctr_vkl}/vkloader.c" "${_ctr_vkl}/vkthunks.c")
target_include_directories(ctr_ps4_vkloader PRIVATE "${_ctr_vkl}" "${ORBIS_MESA_SRC}/include")

file(GLOB _ctr_orbis_zlib "${ORBIS_MESA_BUILD}/subprojects/zlib-*/libz.a")
file(GLOB _ctr_orbis_gallium "${ORBIS_MESA_BUILD}/src/gallium/targets/dri/libgallium-*.a")
target_link_libraries(ctr_ps4_platform PUBLIC
    -Wl,--start-group
    "${ORBIS_MESA_BUILD}/src/egl/libEGL.a"
    ${_ctr_orbis_gallium}
    ctr_ps4_vkloader
    # Mesa's dispatch tables reference entry points weakly; without --whole-archive they resolve
    # to zero (orbis-ports' RetroArch notes).
    -Wl,--whole-archive "${ORBIS_MESA_BUILD}/src/amd/vulkan/libvulkan_radeon.a" -Wl,--no-whole-archive
    -Wl,--end-group
    # libvulkan_radeon.a and libgallium both carry Mesa's util/ objects from the same build; with
    # RADV taken whole, the duplicates are identical copies.
    -Wl,--allow-multiple-definition
    -Wl,--error-limit=0
    ${_ctr_orbis_zlib}
    -lSceGnmDriver -lSceVideoOut -lSceAudioOut -lScePad -lSceUserService -lSceSystemService
    -lSceSysmodule
)

add_library(SDL3::SDL3 ALIAS ctr_ps4_platform)
