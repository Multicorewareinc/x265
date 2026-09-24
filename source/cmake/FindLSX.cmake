include(FindPackageHandleStandardArgs)

if(LOONGARCH64)

    set(CHECK_LSX_CODE "
        #if defined(__loongarch_sx)
            int main(){return 0;}
        #else
            #error No LSX detected!
        #endif")

    check_cxx_source_compiles("${CHECK_LSX_CODE}" SUPPORTS_LSX)

endif()
