# Compute the NASM command line flags for x86 assembly.
#
# include() this file; it runs in the caller's scope and sets:
#   X265_NASM_FORMAT_ARGS  object format args (-f win64, -f elf64, ...)
#   X265_NASM_DEFINES      include path and -D defines
#   NASM_FLAGS             both of the above, for custom build rules
#
# Callers set NASM_FLAGS here instead of receiving it through PARENT_SCOPE
# from enable_language(ASM_NASM): with CMake 4.5 that value no longer reaches
# the top-level directory, so the Visual Studio rules ran nasm with no flags.

set(X265_NASM_DEFINES ${ASM_FLAGS})

if(X64)
    list(APPEND X265_NASM_DEFINES -DARCH_X86_64=1 -I ${CMAKE_CURRENT_LIST_DIR}/../common/x86/)
    if(ENABLE_PIC)
        list(APPEND X265_NASM_DEFINES -DPIC)
    endif()
    if(APPLE)
        set(X265_NASM_FORMAT_ARGS -f macho64 -DPREFIX)
    elseif(UNIX AND NOT CYGWIN)
        set(X265_NASM_FORMAT_ARGS -f elf64)
    else()
        set(X265_NASM_FORMAT_ARGS -f win64)
    endif()
else()
    list(APPEND X265_NASM_DEFINES -DARCH_X86_64=0 -I ${CMAKE_CURRENT_LIST_DIR}/../common/x86/)
    if(APPLE)
        set(X265_NASM_FORMAT_ARGS -f macho32 -DPREFIX)
    elseif(UNIX AND NOT CYGWIN)
        set(X265_NASM_FORMAT_ARGS -f elf32)
    else()
        set(X265_NASM_FORMAT_ARGS -f win32 -DPREFIX)
    endif()
endif()

if(GCC)
    list(APPEND X265_NASM_DEFINES -DHAVE_ALIGNED_STACK=1)
else()
    list(APPEND X265_NASM_DEFINES -DHAVE_ALIGNED_STACK=0)
endif()

if(HIGH_BIT_DEPTH)
    if(MAIN12)
        list(APPEND X265_NASM_DEFINES -DHIGH_BIT_DEPTH=1 -DBIT_DEPTH=12 -DX265_NS=${X265_NS})
    else()
        list(APPEND X265_NASM_DEFINES -DHIGH_BIT_DEPTH=1 -DBIT_DEPTH=10 -DX265_NS=${X265_NS})
    endif()
else()
    list(APPEND X265_NASM_DEFINES -DHIGH_BIT_DEPTH=0 -DBIT_DEPTH=8 -DX265_NS=${X265_NS})
endif()

list(APPEND X265_NASM_DEFINES ${CMAKE_ASM_NASM_FLAGS})

if(CMAKE_BUILD_TYPE MATCHES Release)
    list(APPEND X265_NASM_DEFINES ${CMAKE_ASM_NASM_FLAGS_RELEASE})
elseif(CMAKE_BUILD_TYPE MATCHES Debug)
    list(APPEND X265_NASM_DEFINES ${CMAKE_ASM_NASM_FLAGS_DEBUG})
elseif(CMAKE_BUILD_TYPE MATCHES MinSizeRel)
    list(APPEND X265_NASM_DEFINES ${CMAKE_ASM_NASM_FLAGS_MINSIZEREL})
elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    list(APPEND X265_NASM_DEFINES ${CMAKE_ASM_NASM_FLAGS_RELWITHDEBINFO})
endif()

set(NASM_FLAGS ${X265_NASM_FORMAT_ARGS} ${X265_NASM_DEFINES})
