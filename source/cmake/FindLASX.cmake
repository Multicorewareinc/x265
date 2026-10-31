include(FindPackageHandleStandardArgs)

if(LOONGARCH64)

    set(CHECK_LASX_CODE "
        #if defined(__loongarch_asx)
            int main(){return 0;}
        #else
            #error No LASX detected!
        #endif")

    check_cxx_source_compiles("${CHECK_LASX_CODE}" SUPPORTS_LASX)

endif()
