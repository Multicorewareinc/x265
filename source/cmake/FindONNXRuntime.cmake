include(FindPackageHandleStandardArgs)

if(UNIX)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so")
else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
endif()

set(ONNXRUNTIME_DIR "" CACHE PATH "Path to ONNX Runtime installation")

find_path(ONNX_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS "${ONNXRUNTIME_DIR}/include"
          "${ONNXRUNTIME_DIR}/include/onnxruntime"
    NO_DEFAULT_PATH
    DOC "ONNX Runtime include directory"
)

find_library(ONNX_LIBRARY
    NAMES onnxruntime
    PATHS "${ONNXRUNTIME_DIR}/lib"
          "${ONNXRUNTIME_DIR}/lib64"
    NO_DEFAULT_PATH
    DOC "ONNX Runtime library"
)

mark_as_advanced(ONNX_LIBRARY ONNX_INCLUDE_DIR)
find_package_handle_standard_args(ONNXRuntime REQUIRED_VARS ONNX_LIBRARY ONNX_INCLUDE_DIR)
