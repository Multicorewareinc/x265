# vim: syntax=cmake

option(TESTBENCH_USE_COUNTER "Use raw counter ticks for benchmark timing" OFF)
if(TESTBENCH_USE_COUNTER AND
   (NOT (X86 OR ARM64 OR CROSS_COMPILE_ARM64 OR RISCV64 OR CROSS_COMPILE_RISCV64 OR LOONGARCH64 OR CROSS_COMPILE_LOONGARCH64) OR
    (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT (MSVC AND X86))))
    message(WARNING "Counter timing unavailable for this target or compiler; using std::chrono::steady_clock")
    set(TESTBENCH_USE_COUNTER OFF)
endif()

# add X86 assembly files
if(X86)
enable_language(ASM_NASM)

if(MSVC_IDE)
    set(NASM_SRC checkasm-a.obj)
    add_custom_command(
        OUTPUT checkasm-a.obj
        COMMAND ${NASM_EXECUTABLE}
        ARGS ${NASM_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/checkasm-a.asm -o checkasm-a.obj
        DEPENDS checkasm-a.asm)
else()
    set(NASM_SRC checkasm-a.asm)
endif()
endif(X86)

# add ARM assembly files
if(ARM OR CROSS_COMPILE_ARM)
    enable_language(ASM)
    set(NASM_SRC checkasm-arm.S)
    add_custom_command(
        OUTPUT checkasm-arm.obj
        COMMAND ${CMAKE_CXX_COMPILER}
        ARGS ${NASM_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/checkasm-arm.S -o checkasm-arm.obj
        DEPENDS checkasm-arm.S)
endif(ARM OR CROSS_COMPILE_ARM)

# add PowerPC assembly files
if(POWER)
    set(NASM_SRC)
endif(POWER)

# add LoongArch64 assembly files
if(LOONGARCH64)
    enable_language(ASM)
    set(NASM_SRC checkasm-loongarch64.S)
    add_custom_command(
        OUTPUT checkasm-loongarch64.obj
        COMMAND ${CMAKE_CXX_COMPILER}
        ARGS ${NASM_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/checkasm-loongarch64.S -o checkasm-loongarch64.obj
        DEPENDS checkasm-loongarch64.S)
endif(LOONGARCH64)

add_executable(TestBench ${NASM_SRC}
    testbench.cpp testharness.h
    pixelharness.cpp pixelharness.h
    mbdstharness.cpp mbdstharness.h
    ipfilterharness.cpp ipfilterharness.h
    intrapredharness.cpp intrapredharness.h
    mcstfharness.cpp mcstfharness.h)

if(TESTBENCH_USE_COUNTER)
    target_compile_definitions(TestBench PRIVATE X265_TESTBENCH_USE_COUNTER=1)
endif()

target_link_libraries(TestBench x265-static ${PLATFORM_LIBS})
if(LINKER_OPTIONS)
    if(EXTRA_LIB)
        list(APPEND LINKER_OPTIONS "-L..")
    endif(EXTRA_LIB)
    string(REPLACE ";" " " LINKER_OPTION_STR "${LINKER_OPTIONS}")
    set_target_properties(TestBench PROPERTIES LINK_FLAGS "${LINKER_OPTION_STR}")
endif()
