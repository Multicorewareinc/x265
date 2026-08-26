# multilib.cmake - Build the 10-bit and 12-bit static libraries as nested
# sub-builds and link them into the 8-bit API library so that a single library
# can switch bit depths at runtime, following the upstream
# build/linux/multilib.sh recipe.
#
# Included from the top-level CMakeLists.txt when ENABLE_MULTILIB is ON. The
# sub-builds are defined as regular build targets so that they are only built
# when the library is built; the resulting archives are then merged into the
# 8-bit API library by a POST_BUILD step (see CMakeLists.txt).

if(NOT ENABLE_MULTILIB)
    return()
endif()

set(_multilib_root "${CMAKE_CURRENT_BINARY_DIR}/multilib")
set(_multilib_source "${CMAKE_CURRENT_SOURCE_DIR}")

# Inherit the generator, toolchain and platform settings from this build
set(_multilib_common_args
    "-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    "-DENABLE_SHARED=OFF"
    "-DENABLE_CLI=OFF"
    "-DENABLE_LIBNUMA=OFF"
    "-DENABLE_PIC=ON"
    "-DEXPORT_C_API=OFF"
    "-DCMAKE_DISABLE_FIND_PACKAGE_VLD=ON"
)
if(DEFINED CMAKE_GENERATOR_PLATFORM)
    list(APPEND _multilib_common_args "-DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED CMAKE_MAKE_PROGRAM)
    list(APPEND _multilib_common_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
endif()
if(DEFINED CMAKE_BUILD_TYPE)
    list(APPEND _multilib_common_args "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()
if(DEFINED CMAKE_SYSTEM_VERSION)
    list(APPEND _multilib_common_args "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}")
endif()
if(CMAKE_CROSSCOMPILING AND DEFINED CMAKE_SYSTEM_NAME)
    # Required for cross-compilation; not propagated for native builds, where
    # an explicit CMAKE_SYSTEM_NAME would wrongly put the nested builds into
    # cross-compiling mode
    list(APPEND _multilib_common_args "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif()
if(DEFINED VERSION)
    list(APPEND _multilib_common_args "-DVERSION=${VERSION}")
endif()
if(DEFINED NASM_EXECUTABLE)
    list(APPEND _multilib_common_args "-DNASM_EXECUTABLE=${NASM_EXECUTABLE}")
endif()
if(DEFINED ENABLE_ASSEMBLY)
    list(APPEND _multilib_common_args "-DENABLE_ASSEMBLY=${ENABLE_ASSEMBLY}")
endif()
# vcpkg: propagate the triplet settings so the nested builds use the same
# compiler and flags as the parent build
foreach(_var IN ITEMS VCPKG_TARGET_TRIPLET VCPKG_CHAINLOAD_TOOLCHAIN_FILE VCPKG_CXX_FLAGS
                       VCPKG_CXX_FLAGS_RELEASE VCPKG_CXX_FLAGS_DEBUG VCPKG_C_FLAGS
                       VCPKG_C_FLAGS_RELEASE VCPKG_C_FLAGS_DEBUG VCPKG_CRT_LINKAGE
                       VCPKG_LINKER_FLAGS VCPKG_LINKER_FLAGS_RELEASE VCPKG_LINKER_FLAGS_DEBUG
                       VCPKG_TARGET_ARCHITECTURE VCPKG_SET_CHARSET_FLAG VCPKG_PLATFORM_TOOLSET
                       VCPKG_INSTALLED_DIR _VCPKG_INSTALLED_DIR VCPKG_MANIFEST_INSTALL)
    if(DEFINED ${_var})
        list(APPEND _multilib_common_args "-D${_var}=${${_var}}")
    endif()
endforeach()
# The triplet's configure options (e.g. ANDROID_ABI) are spliced into the
# configure command line by vcpkg_cmake_configure, so they are visible here as
# plain cache variables and must reach the nested builds too
foreach(_var IN ITEMS ANDROID_ABI ANDROID_ARM_NEON ANDROID_ARM_MODE
                       OHOS_ARCH CMAKE_PLATFORM_NO_VERSIONED_SONAME)
    if(DEFINED ${_var})
        list(APPEND _multilib_common_args "-D${_var}=${${_var}}")
    endif()
endforeach()

# The nested builds produce the static archive of the x265-static target
# (libx265.a on non-MSVC, x265-static.lib on MSVC)
if(MSVC)
    set(_multilib_archive_name "x265-static.lib")
else()
    set(_multilib_archive_name "libx265.a")
endif()

# Define the bit-depth sub-builds as regular targets that declare their
# produced archive as an output, so that the build system knows how to build
# the archives the main library links against; the main library targets depend
# on them (see CMakeLists.txt)
function(x265_define_multilib_variant name dir archive)
    add_custom_command(
        OUTPUT "${archive}"
        COMMAND "${CMAKE_COMMAND}" -S "${_multilib_source}" -B "${dir}"
                ${_multilib_common_args} ${ARGN}
        COMMAND "${CMAKE_COMMAND}" --build "${dir}" --config "$<CONFIG>"
        COMMENT "Building the ${name} x265 sub-library"
        VERBATIM)
    add_custom_target(${name} DEPENDS "${archive}")
endfunction()

x265_define_multilib_variant(x265-multilib-10bit "${_multilib_root}/10bit"
    "${_multilib_root}/10bit/${_multilib_archive_name}" "-DHIGH_BIT_DEPTH=ON")
x265_define_multilib_variant(x265-multilib-12bit "${_multilib_root}/12bit"
    "${_multilib_root}/12bit/${_multilib_archive_name}" "-DHIGH_BIT_DEPTH=ON" "-DMAIN12=ON")

# Link the 10/12-bit archives into this build; the LINKED_* options enable the
# bit-depth dispatch in the exported C API (see encoder/api.cpp)
set(EXTRA_LIB
    "${_multilib_root}/10bit/${_multilib_archive_name};${_multilib_root}/12bit/${_multilib_archive_name}"
    CACHE STRING "Extra libraries to link against" FORCE)
set(LINKED_10BIT ON CACHE BOOL "10bit libx265 is being linked with this library" FORCE)
set(LINKED_12BIT ON CACHE BOOL "12bit libx265 is being linked with this library" FORCE)
