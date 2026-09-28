# Find ONNX Runtime (C API)
#
# ONNXRUNTIME_DIR (cache or environment) points at the package root:
#   release archive, vcpkg, install tree: <root>/include, <root>/lib
#   source build install:                 <root>/include/onnxruntime[/core/session]
#   NuGet:                                <root>/build/native/include,
#                                         <root>/runtimes/<os>-<arch>/native
# pkg-config and system locations are searched as well.
#
#  ONNXRuntime_FOUND - ONNX Runtime was found
#  ONNX_INCLUDE_DIR  - directory containing onnxruntime_c_api.h
#  ONNX_LIBRARY      - library to link (import library on Windows)
#  ONNX_RUNTIME_DLLS - Windows: DLLs to ship with the binaries

include(FindPackageHandleStandardArgs)

set(ONNXRUNTIME_DIR "$ENV{ONNXRUNTIME_DIR}" CACHE PATH "Path to ONNX Runtime installation")
file(TO_CMAKE_PATH "${ONNXRUNTIME_DIR}" _ort_root)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_ONNXRUNTIME QUIET libonnxruntime)
endif()

# NuGet runtime identifier
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|AARCH64|arm64|ARM64)$")
    set(_ort_arch arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ort_arch x64)
else()
    set(_ort_arch x86)
endif()
if(WIN32)
    set(_ort_rid win-${_ort_arch})
elseif(APPLE)
    set(_ort_rid osx-${_ort_arch})
else()
    set(_ort_rid linux-${_ort_arch})
endif()

set(_ort_hints)
if(_ort_root)
    list(APPEND _ort_hints "${_ort_root}" "${_ort_root}/build/native" "${_ort_root}/runtimes/${_ort_rid}/native")
endif()

find_path(ONNX_INCLUDE_DIR
    NAMES onnxruntime_c_api.h
    HINTS ${_ort_hints} ${PC_ONNXRUNTIME_INCLUDE_DIRS}
    PATH_SUFFIXES include include/onnxruntime include/onnxruntime/core/session
                  onnxruntime onnxruntime/core/session
    DOC "ONNX Runtime include directory"
)

find_library(ONNX_LIBRARY
    NAMES onnxruntime
    HINTS ${_ort_hints} ${PC_ONNXRUNTIME_LIBRARY_DIRS}
    PATH_SUFFIXES lib lib64
    DOC "ONNX Runtime library"
)

# The DLL is loaded from the executable's directory or PATH
set(ONNX_RUNTIME_DLLS)
if(WIN32 AND ONNX_LIBRARY)
    get_filename_component(_ort_libdir "${ONNX_LIBRARY}" DIRECTORY)
    find_file(ONNX_RUNTIME_DLL
        NAMES onnxruntime.dll
        HINTS "${_ort_libdir}" "${_ort_libdir}/../bin" ${_ort_hints}
        PATH_SUFFIXES bin lib
        NO_DEFAULT_PATH
        DOC "ONNX Runtime DLL"
    )
    if(ONNX_RUNTIME_DLL)
        get_filename_component(_ort_dlldir "${ONNX_RUNTIME_DLL}" DIRECTORY)
        # includes onnxruntime_providers_shared.dll
        file(GLOB ONNX_RUNTIME_DLLS "${_ort_dlldir}/onnxruntime*.dll")
    else()
        message(WARNING "onnxruntime.dll not found next to ${ONNX_LIBRARY}; "
                        "it must be on PATH or next to x265.exe at runtime")
    endif()
    mark_as_advanced(ONNX_RUNTIME_DLL)
endif()

mark_as_advanced(ONNX_LIBRARY ONNX_INCLUDE_DIR)
find_package_handle_standard_args(ONNXRuntime REQUIRED_VARS ONNX_LIBRARY ONNX_INCLUDE_DIR)
