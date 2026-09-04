# multilib.cmake - Build the 10-bit and 12-bit static libraries as nested
# sub-builds and link them into the 8-bit API library so that a single library
# can switch bit depths at runtime, following the upstream
# build/linux/multilib.sh recipe.
#
# Included from the top-level CMakeLists.txt when ENABLE_MULTILIB is ON. The
# sub-builds are defined as regular build targets so that they are only built
# when the library is built; the resulting archives are then merged into the
# 8-bit API library by a POST_BUILD step (see CMakeLists.txt).
#
# The nested builds inherit the generator, the toolchain and this project's
# build options. Toolchain-specific settings that CMake cannot infer (e.g. a
# target-selection variable such as ANDROID_ABI, or a packaging toolchain's
# triplet) can be passed through MULTILIB_CMAKE_ARGS.

set(_multilib_root "${CMAKE_CURRENT_BINARY_DIR}/multilib")
set(_multilib_source "${CMAKE_CURRENT_SOURCE_DIR}")

# Inherit the generator, toolchain and platform settings from this build
set(_multilib_common_args
    "-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    "-DENABLE_SHARED=OFF"
    "-DENABLE_CLI=OFF"
    "-DENABLE_PIC=ON"
    "-DEXPORT_C_API=OFF"
)
if(DEFINED CMAKE_GENERATOR_PLATFORM)
    list(APPEND _multilib_common_args "-DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED CMAKE_GENERATOR_TOOLSET AND NOT CMAKE_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _multilib_common_args "-T${CMAKE_GENERATOR_TOOLSET}")
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
# Inherit this project's build options
foreach(_option IN ITEMS ENABLE_ASSEMBLY ENABLE_LIBNUMA ENABLE_HDR10_PLUS
                           ENABLE_SVT_HEVC ENABLE_LIBVMAF ENABLE_ALPHA
                           ENABLE_MULTIVIEW ENABLE_SCC_EXT)
    if(DEFINED ${_option})
        list(APPEND _multilib_common_args "-D${_option}=${${_option}}")
    endif()
endforeach()
if(DEFINED CMAKE_DISABLE_FIND_PACKAGE_VLD)
    list(APPEND _multilib_common_args "-DCMAKE_DISABLE_FIND_PACKAGE_VLD=${CMAKE_DISABLE_FIND_PACKAGE_VLD}")
endif()
# Packager hook: extra CMake arguments for the nested builds (e.g. the
# toolchain's target selection such as ANDROID_ABI)
if(DEFINED MULTILIB_CMAKE_ARGS AND NOT MULTILIB_CMAKE_ARGS STREQUAL "")
    separate_arguments(_multilib_extra_args NATIVE_COMMAND "${MULTILIB_CMAKE_ARGS}")
    list(APPEND _multilib_common_args ${_multilib_extra_args})
endif()
# Forward the parallelism of the parent build when it is known
set(_multilib_parallel_args "")
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(_multilib_parallel_args "-j$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
endif()

# Track the x265 sources so that the nested builds re-run when they change;
# without this the produced archive would be considered up to date forever
file(GLOB_RECURSE _multilib_depends CONFIGURE_DEPENDS "${_multilib_source}/*")

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
# on them (see CMakeLists.txt).
#
# CMAKE_ARCHIVE_OUTPUT_DIRECTORY is forced for every configuration so that the
# produced archive always lands in the sub-build directory, independent of the
# generator (multi-config generators such as Visual Studio would otherwise
# place it in a per-configuration subdirectory).
function(x265_define_multilib_variant name dir archive)
    add_custom_command(
        OUTPUT "${archive}"
        COMMAND "${CMAKE_COMMAND}" -S "${_multilib_source}" -B "${dir}"
                ${_multilib_common_args} ${ARGN}
                "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${dir}"
                "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG=${dir}"
                "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE=${dir}"
                "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL=${dir}"
                "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO=${dir}"
        COMMAND "${CMAKE_COMMAND}" --build "${dir}" --config "$<CONFIG>"
                ${_multilib_parallel_args}
        DEPENDS ${_multilib_depends}
        COMMENT "Building the ${name} x265 sub-library"
        VERBATIM)
    add_custom_target(${name} DEPENDS "${archive}")
endfunction()

x265_define_multilib_variant(x265-multilib-10bit "${_multilib_root}/10bit"
    "${_multilib_root}/10bit/${_multilib_archive_name}" "-DHIGH_BIT_DEPTH=ON")
x265_define_multilib_variant(x265-multilib-12bit "${_multilib_root}/12bit"
    "${_multilib_root}/12bit/${_multilib_archive_name}" "-DHIGH_BIT_DEPTH=ON" "-DMAIN12=ON")
# Build the bit-depth sub-libraries sequentially (like build/linux/multilib.sh)
# so that a parallel parent build does not run two nested builds at once
add_dependencies(x265-multilib-12bit x265-multilib-10bit)

# Link the 10/12-bit archives into this build; the LINKED_* options enable the
# bit-depth dispatch in the exported C API (see encoder/api.cpp)
if(EXTRA_LIB)
    message(WARNING "ENABLE_MULTILIB is enabled but EXTRA_LIB is already set; the multilib archives will not be linked automatically")
else()
    set(EXTRA_LIB
        "${_multilib_root}/10bit/${_multilib_archive_name};${_multilib_root}/12bit/${_multilib_archive_name}"
        CACHE STRING "Extra libraries to link against" FORCE)
    set(LINKED_10BIT ON CACHE BOOL "10bit libx265 is being linked with this library" FORCE)
    set(LINKED_12BIT ON CACHE BOOL "12bit libx265 is being linked with this library" FORCE)
endif()
