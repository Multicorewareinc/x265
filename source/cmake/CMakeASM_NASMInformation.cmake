set(ASM_DIALECT "_NASM")
set(CMAKE_ASM${ASM_DIALECT}_SOURCE_FILE_EXTENSIONS asm)

# The flags are computed in X265NasmFlags.cmake, which the top-level and test
# CMakeLists.txt also include for their Visual Studio / Xcode custom rules.
include(${CMAKE_CURRENT_LIST_DIR}/X265NasmFlags.cmake)
string(REPLACE ";" " " CMAKE_ASM_NASM_COMPILER_ARG1 "${X265_NASM_FORMAT_ARGS}")

# This section exists to override the one in CMakeASMInformation.cmake
# (the default Information file). This removes the <FLAGS>
# thing so that your C compiler flags that have been set via
# set_target_properties don't get passed to nasm and confuse it.
if(NOT CMAKE_ASM${ASM_DIALECT}_COMPILE_OBJECT)
    string(REPLACE ";" " " STR_ASM_FLAGS "${X265_NASM_DEFINES}")
    set(CMAKE_ASM${ASM_DIALECT}_COMPILE_OBJECT "<CMAKE_ASM${ASM_DIALECT}_COMPILER> ${STR_ASM_FLAGS} -o <OBJECT> <SOURCE>")
endif()

include(CMakeASMInformation)
set(ASM_DIALECT)
