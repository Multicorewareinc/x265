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
# build options, plus the common Android/OHOS target-selection variables. Any
# other toolchain-specific setting that CMake cannot infer (e.g. a packaging
# toolchain's triplet) can be passed through MULTILIB_CMAKE_ARGS.

set(_multilib_root "${CMAKE_CURRENT_BINARY_DIR}/multilib")
set(_multilib_source "${CMAKE_CURRENT_SOURCE_DIR}")

# Inherit the generator, toolchain and platform settings from this build
set(_multilib_common_args
    "-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    "-DENABLE_SHARED=OFF"
    "-DENABLE_CLI=OFF"
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
# Inherit the compilers (and any launcher such as ccache/sccache) so the nested
# builds use the same toolchain as the parent instead of whatever CMake happens
# to detect in the build environment
if(DEFINED CMAKE_C_COMPILER)
    list(APPEND _multilib_common_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif()
if(DEFINED CMAKE_CXX_COMPILER)
    list(APPEND _multilib_common_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif()
if(DEFINED CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_C_COMPILER_LAUNCHER STREQUAL "")
    list(APPEND _multilib_common_args "-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}")
endif()
if(DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "")
    list(APPEND _multilib_common_args "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
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
foreach(_option IN ITEMS ENABLE_ASSEMBLY ENABLE_PIC ENABLE_LIBNUMA
                           ENABLE_HDR10_PLUS ENABLE_SVT_HEVC ENABLE_LIBVMAF
                           ENABLE_ALPHA ENABLE_MULTIVIEW ENABLE_SCC_EXT)
    if(DEFINED ${_option})
        list(APPEND _multilib_common_args "-D${_option}=${${_option}}")
    endif()
endforeach()
if(DEFINED CMAKE_DISABLE_FIND_PACKAGE_VLD)
    list(APPEND _multilib_common_args "-DCMAKE_DISABLE_FIND_PACKAGE_VLD=${CMAKE_DISABLE_FIND_PACKAGE_VLD}")
endif()
# These are often provided on the configure command line rather than by the
# toolchain file, so forward them explicitly when present
if(DEFINED VERSION)
    list(APPEND _multilib_common_args "-DVERSION=${VERSION}")
endif()
if(DEFINED NASM_EXECUTABLE)
    list(APPEND _multilib_common_args "-DNASM_EXECUTABLE=${NASM_EXECUTABLE}")
endif()
# Toolchain target selection (e.g. the Android NDK ABI) is passed on the
# command line and lives in plain cache variables, not in the toolchain file;
# forward the common ones so the nested builds target the same ABI.
foreach(_var IN ITEMS ANDROID_ABI ANDROID_ARM_NEON ANDROID_ARM_MODE
                       ANDROID_PLATFORM ANDROID_STL ANDROID_NDK
                       ANDROID_TOOLCHAIN ANDROID_CPP_FEATURES
                       OHOS_ARCH CMAKE_PLATFORM_NO_VERSIONED_SONAME)
    if(DEFINED ${_var})
        list(APPEND _multilib_common_args "-D${_var}=${${_var}}")
    endif()
endforeach()
# Packager hook: extra CMake arguments for the nested builds (e.g. the
# toolchain's target selection such as ANDROID_ABI)
if(DEFINED MULTILIB_CMAKE_ARGS AND NOT MULTILIB_CMAKE_ARGS STREQUAL "")
    separate_arguments(_multilib_extra_args NATIVE_COMMAND "${MULTILIB_CMAKE_ARGS}")
    list(APPEND _multilib_common_args ${_multilib_extra_args})
endif()
# Forward the parallelism of the parent build so the nested builds do not fall
# back to -j1. Prefer CMAKE_BUILD_PARALLEL_LEVEL and otherwise fall back to the
# machine's processor count; the two bit-depth sub-builds are chained
# sequentially, so each may safely use all cores.
set(_multilib_parallel_args "")
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(_multilib_parallel_args "-j$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
else()
    include(ProcessorCount)
    ProcessorCount(_multilib_ncpu)
    if(_multilib_ncpu GREATER 0)
        set(_multilib_parallel_args "-j${_multilib_ncpu}")
    endif()
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

# Archiver used by the static merge. When cross-compiling, x265-merge.cmake
# would auto-detect a host tool (in `cmake -P` mode WIN32 reflects the host),
# which cannot process target objects; pass the target toolchain's archiver
# instead. Native builds keep the script's platform default (libtool on macOS,
# lib.exe/llvm-lib on MSVC, ar elsewhere).
if(MULTILIB_ARCHIVER)
    set(_multilib_archiver "${MULTILIB_ARCHIVER}")
elseif(CMAKE_CROSSCOMPILING AND CMAKE_AR)
    set(_multilib_archiver "${CMAKE_AR}")
else()
    set(_multilib_archiver "")
endif()

# Define the bit-depth sub-builds as regular targets that declare their
# produced archive as an output, so that the build system knows how to build
# the archives the main library links against; the main library targets depend
# on them (see CMakeLists.txt).
#
# CMAKE_ARCHIVE_OUTPUT_DIRECTORY is forced so that the produced archive always
# lands directly in the sub-build directory. Multi-config generators such as
# Visual Studio additionally need the per-configuration variants, otherwise
# they append a per-configuration subdirectory to the archive path.
function(x265_define_multilib_variant name dir archive)
    set(_archive_dir_args "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${dir}")
    if(CMAKE_CONFIGURATION_TYPES)
        # Multi-config generators (Visual Studio, Xcode) append a per-config
        # subdirectory to the generic path unless the per-config path is set
        foreach(_config IN ITEMS DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
            list(APPEND _archive_dir_args "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_config}=${dir}")
        endforeach()
    endif()
    add_custom_command(
        OUTPUT "${archive}"
        COMMAND "${CMAKE_COMMAND}" -S "${_multilib_source}" -B "${dir}"
                ${_multilib_common_args} ${ARGN}
                ${_archive_dir_args}
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
# bit-depth dispatch in the exported C API (see encoder/api.cpp). A user
# provided EXTRA_LIB is respected (with a warning) instead of being silently
# discarded; the cache entries are marked as owned so they can be cleared when
# ENABLE_MULTILIB is turned back OFF in the same build tree.
set(_multilib_extra_lib
    "${_multilib_root}/10bit/${_multilib_archive_name};${_multilib_root}/12bit/${_multilib_archive_name}")
if(EXTRA_LIB AND NOT "${EXTRA_LIB}" STREQUAL "${_multilib_extra_lib}")
    message(WARNING "ENABLE_MULTILIB is enabled but EXTRA_LIB is already set; the multilib archives will not be linked automatically")
else()
    set(EXTRA_LIB "${_multilib_extra_lib}"
        CACHE STRING "Extra libraries to link against" FORCE)
    set(LINKED_10BIT ON CACHE BOOL "10bit libx265 is being linked with this library" FORCE)
    set(LINKED_12BIT ON CACHE BOOL "12bit libx265 is being linked with this library" FORCE)
    set(X265_MULTILIB_CACHE_OWNED ON CACHE INTERNAL "x265 multilib force-set EXTRA_LIB/LINKED_10BIT/LINKED_12BIT")
endif()
