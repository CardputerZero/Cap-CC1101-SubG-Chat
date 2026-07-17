set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

if(CMAKE_SYSROOT)
    set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

    list(PREPEND CMAKE_PREFIX_PATH
        "${CMAKE_SYSROOT}/usr"
        "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/cmake"
        "${CMAKE_SYSROOT}/usr/local"
    )

    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
    set(ENV{PKG_CONFIG_LIBDIR}
        "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "")

    file(GLOB CC1101_CHAT_GCC_RUNTIME_DIRS LIST_DIRECTORIES true
         "${CMAKE_SYSROOT}/usr/lib/gcc/aarch64-linux-gnu/*")
    if(CC1101_CHAT_GCC_RUNTIME_DIRS AND NOT CC1101_CHAT_SYSROOT_LINK_FLAGS_INITIALIZED)
        set(CC1101_CHAT_GCC_RUNTIME_DIR "")
        set(CC1101_CHAT_GCC_RUNTIME_VERSION "")
        foreach(candidate_dir IN LISTS CC1101_CHAT_GCC_RUNTIME_DIRS)
            get_filename_component(candidate_version "${candidate_dir}" NAME)
            if(NOT CC1101_CHAT_GCC_RUNTIME_DIR OR
               candidate_version VERSION_GREATER CC1101_CHAT_GCC_RUNTIME_VERSION)
                set(CC1101_CHAT_GCC_RUNTIME_DIR "${candidate_dir}")
                set(CC1101_CHAT_GCC_RUNTIME_VERSION "${candidate_version}")
            endif()
        endforeach()
        set(CC1101_CHAT_SYSROOT_LIBRARY_DIR "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu")
        set(CC1101_CHAT_SYSROOT_LINK_FLAGS
            "-B${CC1101_CHAT_SYSROOT_LIBRARY_DIR}/ -B${CC1101_CHAT_GCC_RUNTIME_DIR}/ -Wl,-rpath-link,${CC1101_CHAT_SYSROOT_LIBRARY_DIR} -L${CC1101_CHAT_SYSROOT_LIBRARY_DIR}")
        string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${CC1101_CHAT_SYSROOT_LINK_FLAGS}")
        string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " ${CC1101_CHAT_SYSROOT_LINK_FLAGS}")
        set(CC1101_CHAT_SYSROOT_LINK_FLAGS_INITIALIZED TRUE CACHE INTERNAL
            "Whether CardputerZero sysroot linker flags were initialized")
    endif()
endif()
