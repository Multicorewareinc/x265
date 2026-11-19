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

# ENABLE_MULTILIB follows the project's own CMake minimum (3.10); the few
# newer features it can use are version-gated below
if(CMAKE_VERSION VERSION_LESS 3.10)
    message(FATAL_ERROR "ENABLE_MULTILIB requires CMake 3.10 or later. "
        "Use build/linux/multilib.sh or the build/vc*-x86_64/multilib.bat "
        "scripts to build the multi-bit-depth library with older CMake.")
endif()

# CMAKE_CONFIGURATION_TYPES can be set by the user for a single-config
# generator too. The generator property is the authoritative distinction.
get_property(_multilib_generator_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

# Append "-D<var>=<value>" to the argument list, preserving list values. A
# backslash-escaped semicolon is unescaped again when the command is generated,
# splitting CMAKE_OSX_ARCHITECTURES=arm64;x86_64 into two arguments. The
# $<SEMICOLON> generator expression survives list processing and expands only
# when the custom command argument is emitted.
function(x265_multilib_append_d list_var var)
    if(DEFINED ${var} AND NOT "${${var}}" STREQUAL "")
        string(REPLACE ";" "$<SEMICOLON>" _value "${${var}}")
        list(APPEND ${list_var} "-D${var}=${_value}")
        set(${list_var} "${${list_var}}" PARENT_SCOPE)
    endif()
endfunction()

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
# Inherit this project's build options: the CPU feature switches and code
# generation options so that all bit depths are compiled with the same
# instruction sets and hardening, and the debugging/profiling switches so the
# sub-builds match the parent (STATIC_LINK_CRT matters too: mixing /MT and /MD
# objects fails with LNK2038 on MSVC). The architecture-specific options are
# only declared on their target platform, hence the DEFINED check.
foreach(_option IN ITEMS ENABLE_ASSEMBLY ENABLE_PIC ENABLE_LIBNUMA
                           STATIC_LINK_CRT ENABLE_HDR10_PLUS ENABLE_SVT_HEVC
                           ENABLE_LIBVMAF ENABLE_ALPHA ENABLE_MULTIVIEW
                           ENABLE_SCC_EXT CHECKED_BUILD
                           ENABLE_AGGRESSIVE_CHECKS DETAILED_CU_STATS
                           ENABLE_PPA ENABLE_VTUNE
                           WARNINGS_AS_ERRORS FPROFILE_GENERATE FPROFILE_USE
                           NATIVE_BUILD NO_ATOMICS WINXP_SUPPORT ENABLE_CET
                           AARCH64_RUNTIME_CPU_DETECT
                           AARCH64_WARNINGS_AS_ERRORS ENABLE_NEON
                           ENABLE_NEON_DOTPROD ENABLE_NEON_I8MM ENABLE_SVE
                           ENABLE_SVE2 ENABLE_SVE2_BITPERM
                           RISCV64_RUNTIME_CPU_DETECT ENABLE_RVV
                           LOONGARCH64_RUNTIME_CPU_DETECT ENABLE_ALTIVEC
                           CPU_POWER8)
    if(DEFINED ${_option})
        list(APPEND _multilib_common_args "-D${_option}=${${_option}}")
    endif()
endforeach()
# Inherit user-provided compiler flags so the sub-builds are compiled with
# the same settings as the parent
x265_multilib_append_d(_multilib_common_args CMAKE_C_FLAGS)
x265_multilib_append_d(_multilib_common_args CMAKE_CXX_FLAGS)
x265_multilib_append_d(_multilib_common_args FSANITIZE)
foreach(_config IN ITEMS DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
    x265_multilib_append_d(_multilib_common_args CMAKE_C_FLAGS_${_config})
    x265_multilib_append_d(_multilib_common_args CMAKE_CXX_FLAGS_${_config})
endforeach()
# The MSVC runtime selection must match the parent's (LNK2038 otherwise)
if(MSVC)
    x265_multilib_append_d(_multilib_common_args CMAKE_MSVC_RUNTIME_LIBRARY)
endif()
# Universal macOS builds (e.g. -DCMAKE_OSX_ARCHITECTURES=arm64;x86_64) must
# produce universal sub-libraries as well
if(APPLE)
    x265_multilib_append_d(_multilib_common_args CMAKE_OSX_ARCHITECTURES)
endif()
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
# forward the common ones so the nested builds target the same ABI. The
# values may themselves be lists (several ANDROID_ABI entries,
# ANDROID_CPP_FEATURES=rtti;exceptions), hence the escaped append.
foreach(_var IN ITEMS ANDROID_ABI ANDROID_ARM_NEON ANDROID_ARM_MODE
                       ANDROID_PLATFORM ANDROID_STL ANDROID_NDK
                       ANDROID_TOOLCHAIN ANDROID_CPP_FEATURES
                       OHOS_ARCH CMAKE_PLATFORM_NO_VERSIONED_SONAME)
    x265_multilib_append_d(_multilib_common_args ${_var})
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
#
# `cmake --build` only gained its own -j option in CMake 3.12 (as a separate
# argument: the combined "-jN" form is only accepted from 3.14), so on older
# versions the native build tool is driven directly instead. That is only
# safe for the Ninja and Makefile generators; other generators fall back to a
# plain `cmake --build` without a parallelism option.
set(_multilib_parallel_level "")
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(_multilib_parallel_level "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
else()
    include(ProcessorCount)
    ProcessorCount(_multilib_ncpu)
    if(_multilib_ncpu GREATER 0)
        set(_multilib_parallel_level "${_multilib_ncpu}")
    endif()
endif()
set(_multilib_native_build FALSE)
set(_multilib_cmake_parallel_args "")
if(CMAKE_VERSION VERSION_LESS 3.12)
    if(CMAKE_GENERATOR MATCHES "Ninja" OR
       (CMAKE_GENERATOR MATCHES "Makefiles" AND NOT CMAKE_GENERATOR MATCHES "NMake"))
        set(_multilib_native_build TRUE)
    endif()
elseif(_multilib_parallel_level)
    set(_multilib_cmake_parallel_args -j "${_multilib_parallel_level}")
endif()

# Track the x265 sources so that the nested builds re-run when they change;
# without this the produced archive would be considered up to date forever.
#
# The build tree is excluded from the tracking: it can be located inside
# source/ (CLion and VS Code do this when they open source/ as the project
# root), and its generated files would make the sub-builds depend on their
# own outputs (circular dependency).
#
# CONFIGURE_DEPENDS (CMake 3.12+) re-runs the configure step whenever the glob
# result changes. When the build tree is inside source/, its generated files
# are part of the glob and the configure step would re-run on every build, so
# it is only used when the build tree is outside source/.
string(FIND "${CMAKE_CURRENT_BINARY_DIR}/" "${CMAKE_CURRENT_SOURCE_DIR}/" _multilib_pos)
set(_multilib_source_glob "${_multilib_source}/*")
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
    # In a true in-source build every file starts with the build prefix. Track
    # only source-like files and exclude the nested build trees below instead
    # of filtering out all sources along with the generated files.
    set(_multilib_source_glob
        "${_multilib_source}/CMakeLists.txt"
        "${_multilib_source}/*.c" "${_multilib_source}/*.cc"
        "${_multilib_source}/*.cpp" "${_multilib_source}/*.cxx"
        "${_multilib_source}/*.h" "${_multilib_source}/*.hh"
        "${_multilib_source}/*.hpp" "${_multilib_source}/*.hxx"
        "${_multilib_source}/*.asm" "${_multilib_source}/*.s"
        "${_multilib_source}/*.S" "${_multilib_source}/*.cmake"
        "${_multilib_source}/*.in")
endif()
if(CMAKE_VERSION VERSION_LESS 3.12 OR _multilib_pos EQUAL 0)
    file(GLOB_RECURSE _multilib_depends ${_multilib_source_glob})
else()
    file(GLOB_RECURSE _multilib_depends CONFIGURE_DEPENDS ${_multilib_source_glob})
endif()
set(_multilib_build_prefix "${CMAKE_CURRENT_BINARY_DIR}/")
set(_multilib_nested_prefix "${_multilib_root}/")
set(_multilib_cmake_files_prefix "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/")
set(_multilib_testing_prefix "${CMAKE_CURRENT_BINARY_DIR}/Testing/")
set(_multilib_generated_depends
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_install.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/x265_config.h")
set(_multilib_sources "")
foreach(_dep IN LISTS _multilib_depends)
    string(FIND "${_dep}" "${_multilib_build_prefix}" _multilib_dep_pos)
    string(FIND "${_dep}" "${_multilib_nested_prefix}" _multilib_nested_pos)
    string(FIND "${_dep}" "${_multilib_cmake_files_prefix}" _multilib_cmake_files_pos)
    string(FIND "${_dep}" "${_multilib_testing_prefix}" _multilib_testing_pos)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
        get_filename_component(_dep_name "${_dep}" NAME)
        # add_subdirectory() writes cmake_install.cmake beside each source
        # directory in an in-source build, and CTest can add test files there
        # as well. They are refreshed on every configure and must not make the
        # nested bit-depth archives dirty.
        if(_dep_name MATCHES "^(cmake_install|cmake_uninstall|CTestTestfile)\\.cmake$")
            continue()
        endif()
        list(FIND _multilib_generated_depends "${_dep}" _multilib_generated_pos)
        if(NOT _multilib_generated_pos EQUAL -1)
            continue()
        endif()
        get_filename_component(_dep_dir "${_dep}" DIRECTORY)
        # CMake writes generated .cmake files directly into an in-source
        # build root. The source tree keeps its CMake modules in cmake/, so
        # direct-root .cmake files are generated dependencies and would make
        # every reconfigure rebuild the sub-libraries.
        if(_dep_dir STREQUAL "${CMAKE_CURRENT_BINARY_DIR}" AND _dep MATCHES "\\.cmake$")
            continue()
        endif()
        if(NOT _multilib_nested_pos EQUAL 0 AND
           NOT _multilib_cmake_files_pos EQUAL 0 AND
           NOT _multilib_testing_pos EQUAL 0)
            list(APPEND _multilib_sources "${_dep}")
        endif()
    elseif(NOT _multilib_dep_pos EQUAL 0)
        list(APPEND _multilib_sources "${_dep}")
    endif()
endforeach()
set(_multilib_depends "${_multilib_sources}")

# The nested builds produce the static archive of the x265-static target
# (libx265.a on non-MSVC, x265-static.lib on MSVC)
if(MSVC)
    set(_multilib_archive_name "x265-static.lib")
else()
    set(_multilib_archive_name "libx265.a")
endif()

# Archiver used by the static merge. x265-merge.cmake runs in `cmake -P`
# script mode, where WIN32/APPLE/MINGW describe the *host* rather than the
# target, so it cannot be left to auto-detect the tool whenever host and
# target can differ; pass the target toolchain's archiver instead (llvm-ar
# for the Android NDK, GNU ar for MinGW, ...). Apple targets (macOS and
# iOS/tvOS cross-builds) keep the script default: libtool merges Mach-O
# archives for every Apple platform, while Apple's ar does not support the
# MRI scripts used for ELF/COFF archives. Native MSVC and Linux builds also
# keep the default (lib.exe / ar).
if(MULTILIB_ARCHIVER)
    set(_multilib_archiver "${MULTILIB_ARCHIVER}")
elseif(MINGW OR (CMAKE_CROSSCOMPILING AND NOT APPLE))
    set(_multilib_archiver "${CMAKE_AR}")
else()
    set(_multilib_archiver "")
endif()

# Define the bit-depth sub-builds as regular targets; the main library targets
# depend on them (see CMakeLists.txt). Single-config builds also declare the
# produced archive as an output so source timestamps can skip the nested work.
#
# CMAKE_ARCHIVE_OUTPUT_DIRECTORY is forced so that the produced archive is
# independent of the library's install location. Multi-config generators append
# their configuration name to this path; those per-configuration directories
# are required to prevent Debug and Release builds from overwriting each other.
function(x265_define_multilib_variant name dir archive)
    # -S/-B were added in CMake 3.13; the earlier releases support the
    # long-standing (undocumented) -H/-B spelling
    if(CMAKE_VERSION VERSION_LESS 3.13)
        set(_multilib_source_args "-H${_multilib_source}" "-B${dir}")
    else()
        set(_multilib_source_args -S "${_multilib_source}" -B "${dir}")
    endif()
    # Remove stale per-config values from an earlier revision before setting
    # the generic path; otherwise cached values suppress the config subdirs.
    set(_archive_dir_args "-UCMAKE_ARCHIVE_OUTPUT_DIRECTORY_*"
        "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${dir}")
    # Unlike Ninja, Makefiles do not rebuild when only a custom command's
    # command line changes. Keep a content-stable stamp for the configure
    # arguments so changing a forwarded option also invalidates the archive.
    string(SHA256 _multilib_args_hash
        "${_multilib_source_args};${_multilib_common_args};${ARGN};${_archive_dir_args}")
    set(_multilib_args_file "${dir}/x265-multilib-args.txt")
    if(NOT _multilib_generator_is_multi_config)
        # configure_file() updates the output only when its content changes.
        # file(GENERATE) refreshes the timestamp on every configure, which
        # would make an unchanged in-source reconfigure rebuild this archive.
        set(_multilib_args_template
            "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/x265-multilib-args-${name}.in")
        file(WRITE "${_multilib_args_template}" "${_multilib_args_hash}\n")
        configure_file("${_multilib_args_template}" "${_multilib_args_file}" COPYONLY)
    endif()
    if(_multilib_native_build)
        set(_multilib_build_command "${CMAKE_MAKE_PROGRAM}" -C "${dir}")
        if(_multilib_parallel_level)
            list(APPEND _multilib_build_command -j "${_multilib_parallel_level}")
        endif()
    else()
        set(_multilib_build_command "${CMAKE_COMMAND}" --build "${dir}"
            --config "$<CONFIG>" ${_multilib_cmake_parallel_args})
    endif()
    # The nested build tree is named with -S/-B on CMake 3.13+ and with the
    # long-standing -H/-B on 3.10-3.12. A positional source argument is not
    # used: when the source directory contains a CMakeCache.txt (in-source
    # parent build), CMake mistakes it for an existing build directory and
    # configures the parent tree instead of the sub-build.
    set(_multilib_configure_command "${CMAKE_COMMAND}" ${_multilib_source_args}
        ${_multilib_common_args} ${ARGN} ${_archive_dir_args})
    # Run the nested build with an empty MAKEFLAGS so the nested make does not
    # try to join the parent's jobserver ("resetting jobserver mode" warnings).
    set(_multilib_run_build_command
        "${CMAKE_COMMAND}" -E env MAKEFLAGS= ${_multilib_build_command})
    if(_multilib_generator_is_multi_config)
        # A multi-config target cannot select its per-config archive through a
        # single OUTPUT file (generator expressions in OUTPUT require a newer
        # CMake than this project's minimum). Invoke the nested native build
        # every time and let it do its own incremental up-to-date checks. This
        # also handles switching configurations in either direction correctly.
        # Concrete byproducts work on CMake 3.10+; $<CONFIG> is not allowed in
        # BYPRODUCTS until 3.20. List every configuration so Ninja Multi-Config
        # has a generating rule for whichever configuration is requested.
        set(_multilib_byproducts "")
        foreach(_multilib_config IN LISTS CMAKE_CONFIGURATION_TYPES)
            list(APPEND _multilib_byproducts "${dir}/${_multilib_config}/${_multilib_archive_name}")
        endforeach()
        add_custom_target(${name}
            BYPRODUCTS ${_multilib_byproducts}
            COMMAND ${_multilib_configure_command}
            COMMAND ${_multilib_run_build_command}
            COMMENT "Building the ${name} x265 sub-library"
            VERBATIM)
    else()
        add_custom_command(
            OUTPUT "${archive}"
            COMMAND ${_multilib_configure_command}
            COMMAND ${_multilib_run_build_command}
            # Keep the declared output newer than the source/config
            # dependencies even if the nested build has nothing to recompile.
            COMMAND "${CMAKE_COMMAND}" -E touch "${archive}"
            DEPENDS "${_multilib_args_file}" ${_multilib_depends}
            COMMENT "Building the ${name} x265 sub-library"
            VERBATIM)
        add_custom_target(${name} DEPENDS "${archive}")
    endif()
endfunction()

set(_multilib_archive_subdir "")
if(_multilib_generator_is_multi_config)
    set(_multilib_archive_subdir "$<CONFIG>/")
endif()
x265_define_multilib_variant(x265-multilib-10bit "${_multilib_root}/10bit"
    "${_multilib_root}/10bit/${_multilib_archive_subdir}${_multilib_archive_name}" "-DHIGH_BIT_DEPTH=ON")
x265_define_multilib_variant(x265-multilib-12bit "${_multilib_root}/12bit"
    "${_multilib_root}/12bit/${_multilib_archive_subdir}${_multilib_archive_name}" "-DHIGH_BIT_DEPTH=ON" "-DMAIN12=ON")

# The parent's 8-bit archive is built as an intermediate target and merged
# into the final x265-static archive by a dedicated rule (see CMakeLists.txt).
# This keeps the sub-archives as file-level inputs on every generator without
# adding a dependency-anchor object to the installed library.
set(_multilib_raw_static_target "x265-8bit-static")
set(_multilib_raw_archive "$<TARGET_FILE:${_multilib_raw_static_target}>")
set(_multilib_final_archive
    "${CMAKE_CURRENT_BINARY_DIR}/${_multilib_archive_subdir}${_multilib_archive_name}")
set(_multilib_merge_stamp "${_multilib_root}/x265-merge-args.txt")
string(SHA256 _multilib_merge_args_hash
    "${_multilib_raw_archive};${_multilib_root}/10bit/${_multilib_archive_subdir}${_multilib_archive_name};${_multilib_root}/12bit/${_multilib_archive_subdir}${_multilib_archive_name};${_multilib_archiver};${CMAKE_CURRENT_SOURCE_DIR}/cmake/x265-merge.cmake")
set(_multilib_merge_args_template
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/x265-multilib-merge-args.in")
file(WRITE "${_multilib_merge_args_template}" "${_multilib_merge_args_hash}\n")
configure_file("${_multilib_merge_args_template}" "${_multilib_merge_stamp}" COPYONLY)

# Build the bit-depth sub-libraries sequentially (like build/linux/multilib.sh)
# so that a parallel parent build does not run two nested builds at once
add_dependencies(x265-multilib-12bit x265-multilib-10bit)

# Link the 10/12-bit archives into this build; the LINKED_* options enable the
# bit-depth dispatch in the exported C API (see encoder/api.cpp). A user
# provided EXTRA_LIB is respected (with a warning) instead of being silently
# discarded; the cache entries are marked as owned so they can be cleared when
# ENABLE_MULTILIB is turned back OFF in the same build tree.
set(_multilib_extra_lib
    "${_multilib_root}/10bit/${_multilib_archive_subdir}${_multilib_archive_name};${_multilib_root}/12bit/${_multilib_archive_subdir}${_multilib_archive_name}")
# Migrate our own cache from the earlier layout without per-config subdirs.
# Only an explicitly owned value may be replaced; a user-provided EXTRA_LIB
# must still take the warning path below and remain untouched.
set(_multilib_legacy_extra_lib
    "${_multilib_root}/10bit/${_multilib_archive_name};${_multilib_root}/12bit/${_multilib_archive_name}")
if(X265_MULTILIB_CACHE_OWNED AND "${EXTRA_LIB}" STREQUAL "${_multilib_legacy_extra_lib}")
    set(EXTRA_LIB "")
endif()
if(EXTRA_LIB AND NOT "${EXTRA_LIB}" STREQUAL "${_multilib_extra_lib}")
    message(WARNING "ENABLE_MULTILIB is enabled but EXTRA_LIB is already set; the multilib archives will not be linked automatically")
    if(X265_MULTILIB_CACHE_OWNED)
        # A previous ENABLE_MULTILIB configuration force-set these entries;
        # the user's own EXTRA_LIB takes over now, so restore the LINKED_*
        # defaults (they referred to the multilib archives, which are no
        # longer linked) and release the ownership so that turning
        # ENABLE_MULTILIB back OFF will not delete the user's value
        unset(LINKED_10BIT CACHE)
        unset(LINKED_12BIT CACHE)
        unset(X265_MULTILIB_CACHE_OWNED CACHE)
    endif()
else()
    set(EXTRA_LIB "${_multilib_extra_lib}"
        CACHE STRING "Extra libraries to link against" FORCE)
    set(LINKED_10BIT ON CACHE BOOL "10bit libx265 is being linked with this library" FORCE)
    set(LINKED_12BIT ON CACHE BOOL "12bit libx265 is being linked with this library" FORCE)
    set(X265_MULTILIB_CACHE_OWNED ON CACHE INTERNAL "x265 multilib force-set EXTRA_LIB/LINKED_10BIT/LINKED_12BIT")
endif()
