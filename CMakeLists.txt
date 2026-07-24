cmake_minimum_required(VERSION 3.16)

project(M5CardputerZeroCapCC1101SubGChat VERSION 0.1.2 LANGUAGES C CXX ASM)

option(CC1101_CHAT_USE_SDL "Use LVGL's SDL backend and mock radio" ON)
set(CC1101_CHAT_BIN_NAME "M5CardputerZero-Cap-CC1101-SubG-Chat" CACHE STRING "Output executable name")
set(CC1101_CHAT_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/dependencies" CACHE PATH "Third-party source directory")
set(CC1101_CHAT_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/dist" CACHE PATH "Runtime output directory")

if(APPLE AND NOT CC1101_CHAT_USE_SDL)
    message(FATAL_ERROR "macOS builds only support CC1101_CHAT_USE_SDL=ON.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CC1101_CHAT_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(CC1101_CHAT_SRC_DIR "${CC1101_CHAT_ROOT}/src")
set(CC1101_CHAT_LVGL_DIR "${CC1101_CHAT_DEPS_DIR}/lvgl")
set(CC1101_CHAT_SPDLOG_DIR "${CC1101_CHAT_DEPS_DIR}/spdlog")
set(CC1101_CHAT_SMOOTH_UI_DIR "${CC1101_CHAT_DEPS_DIR}/smooth_ui_toolkit")
set(CC1101_CHAT_SYSTEM_LIBS ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    list(APPEND CC1101_CHAT_SYSTEM_LIBS m)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

foreach(required_dir IN LISTS CC1101_CHAT_LVGL_DIR CC1101_CHAT_SPDLOG_DIR CC1101_CHAT_SMOOTH_UI_DIR)
    if(NOT EXISTS "${required_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Missing dependency: ${required_dir}. Run ./bootstrap.sh first or set CC1101_CHAT_DEPS_DIR.")
    endif()
endforeach()

find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

set(LV_BUILD_CONF_PATH "${CC1101_CHAT_ROOT}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF CACHE BOOL "" FORCE)

add_subdirectory("${CC1101_CHAT_LVGL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/lvgl" EXCLUDE_FROM_ALL)
add_subdirectory("${CC1101_CHAT_SPDLOG_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/spdlog" EXCLUDE_FROM_ALL)
add_subdirectory("${CC1101_CHAT_SMOOTH_UI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/smooth_ui_toolkit"
                 EXCLUDE_FROM_ALL)

function(cc1101_chat_add_sdl2_parent_include target imported_target)
    if(NOT TARGET "${imported_target}")
        return()
    endif()

    get_target_property(sdl2_include_dirs "${imported_target}" INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT sdl2_include_dirs)
        return()
    endif()

    foreach(sdl2_include_dir IN LISTS sdl2_include_dirs)
        if(sdl2_include_dir MATCHES "/SDL2$")
            get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
            target_include_directories("${target}" SYSTEM PRIVATE "${sdl2_parent_include_dir}")
        endif()
    endforeach()
endfunction()

set(CC1101_CHAT_LVGL_POSIX_PATH "./")
if(NOT CC1101_CHAT_USE_SDL)
    set(CC1101_CHAT_LVGL_POSIX_PATH "/usr/share/Cap-CC1101-SubG-Chat/")
endif()

target_compile_definitions(lvgl PUBLIC
    CC1101_CHAT_LVGL_USE_SDL=$<IF:$<BOOL:${CC1101_CHAT_USE_SDL}>,1,0>
    CC1101_CHAT_LVGL_FS_POSIX_PATH="${CC1101_CHAT_LVGL_POSIX_PATH}"
)

if(CC1101_CHAT_USE_SDL)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()

    if(SDL2_FOUND)
        target_link_libraries(lvgl PRIVATE PkgConfig::SDL2)
        cc1101_chat_add_sdl2_parent_include(lvgl PkgConfig::SDL2)
        set(CC1101_CHAT_SDL_LIBS PkgConfig::SDL2)
    else()
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            target_link_libraries(lvgl PRIVATE SDL2::SDL2)
            cc1101_chat_add_sdl2_parent_include(lvgl SDL2::SDL2)
            set(CC1101_CHAT_SDL_LIBS SDL2::SDL2)
        else()
            find_package(SDL2 REQUIRED)
            target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
            foreach(sdl2_include_dir IN LISTS SDL2_INCLUDE_DIRS)
                if(sdl2_include_dir MATCHES "/SDL2$")
                    get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
                    target_include_directories(lvgl SYSTEM PRIVATE "${sdl2_parent_include_dir}")
                endif()
            endforeach()
            set(CC1101_CHAT_SDL_LIBS ${SDL2_LIBRARIES})
        endif()
    endif()
endif()

file(GLOB CC1101_CHAT_APP_CPP CONFIGURE_DEPENDS
    "${CC1101_CHAT_SRC_DIR}/main.cpp"
    "${CC1101_CHAT_SRC_DIR}/core/*.cpp"
    "${CC1101_CHAT_SRC_DIR}/input/*.cpp"
    "${CC1101_CHAT_SRC_DIR}/models/*.cpp"
    "${CC1101_CHAT_SRC_DIR}/view_models/*.cpp"
    "${CC1101_CHAT_SRC_DIR}/views/*.cpp"
)

set(CC1101_CHAT_RADIO_CPP
    "${CC1101_CHAT_SRC_DIR}/radio/radio_backend.cpp"
    "${CC1101_CHAT_SRC_DIR}/radio/radio_worker.cpp"
)
set(CC1101_CHAT_HAL_CPP "${CC1101_CHAT_SRC_DIR}/hal/cc1101_chat_lvgl_hal.cpp")

if(CC1101_CHAT_USE_SDL)
    list(APPEND CC1101_CHAT_RADIO_CPP "${CC1101_CHAT_SRC_DIR}/radio/mock_radio_backend.cpp")
else()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The real CC1101 backend requires Linux.")
    endif()

    if(PkgConfig_FOUND)
        pkg_check_modules(GPIOD QUIET IMPORTED_TARGET libgpiod)
    endif()

    if(GPIOD_FOUND)
        set(CC1101_CHAT_GPIOD_TARGET PkgConfig::GPIOD)
    else()
        find_path(GPIOD_INCLUDE_DIR NAMES gpiod.h)
        find_library(GPIOD_LIBRARY NAMES gpiod libgpiod.so.3.1.1 libgpiod.so.3 libgpiod.so.2)
        if(NOT GPIOD_INCLUDE_DIR OR NOT GPIOD_LIBRARY)
            message(FATAL_ERROR
                    "libgpiod development files were not found. Install libgpiod-dev or provide them in the target sysroot.")
        endif()

        add_library(cc1101_chat_gpiod UNKNOWN IMPORTED)
        set_target_properties(cc1101_chat_gpiod PROPERTIES
            IMPORTED_LOCATION "${GPIOD_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GPIOD_INCLUDE_DIR}"
        )
        set(CC1101_CHAT_GPIOD_TARGET cc1101_chat_gpiod)
        set(GPIOD_INCLUDE_DIRS "${GPIOD_INCLUDE_DIR}")
    endif()

    set(CC1101_CHAT_GPIOD_V2 OFF)
    if(GPIOD_VERSION VERSION_GREATER_EQUAL "2.0")
        set(CC1101_CHAT_GPIOD_V2 ON)
    else()
        foreach(gpiod_include_dir IN LISTS GPIOD_INCLUDE_DIRS)
            if(EXISTS "${gpiod_include_dir}/gpiod.h")
                file(STRINGS "${gpiod_include_dir}/gpiod.h" GPIOD_V2_DECLARATION
                     REGEX "gpiod_line_settings_new" LIMIT_COUNT 1)
                if(GPIOD_V2_DECLARATION)
                    set(CC1101_CHAT_GPIOD_V2 ON)
                endif()
                break()
            endif()
        endforeach()
    endif()

    list(APPEND CC1101_CHAT_RADIO_CPP
        "${CC1101_CHAT_SRC_DIR}/radio/linux_cc1101_backend.cpp"
        "${CC1101_CHAT_SRC_DIR}/radio/driver/cc1101.cpp"
        "${CC1101_CHAT_SRC_DIR}/radio/driver/gpio_line.cpp"
        "${CC1101_CHAT_SRC_DIR}/radio/driver/spi_device.cpp"
    )
    list(APPEND CC1101_CHAT_HAL_CPP
        "${CC1101_CHAT_SRC_DIR}/hal/cap_spi_overlay.cpp"
        "${CC1101_CHAT_SRC_DIR}/hal/cardputerzero_cc1101_power.cpp"
    )
endif()

add_executable(${CC1101_CHAT_BIN_NAME}
    ${CC1101_CHAT_APP_CPP}
    ${CC1101_CHAT_HAL_CPP}
    ${CC1101_CHAT_RADIO_CPP}
)

target_include_directories(${CC1101_CHAT_BIN_NAME} PRIVATE "${CC1101_CHAT_SRC_DIR}")
target_compile_definitions(${CC1101_CHAT_BIN_NAME} PRIVATE
    CC1101_CHAT_USE_MOCK_RADIO=$<IF:$<BOOL:${CC1101_CHAT_USE_SDL}>,1,0>
    CC1101_CHAT_ENABLE_LINUX_RADIO=$<IF:$<BOOL:${CC1101_CHAT_USE_SDL}>,0,1>
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${CC1101_CHAT_BIN_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT CC1101_CHAT_USE_SDL AND CC1101_CHAT_GPIOD_V2)
    target_compile_definitions(${CC1101_CHAT_BIN_NAME} PRIVATE GPIOD_V2=1)
endif()

target_link_libraries(${CC1101_CHAT_BIN_NAME} PRIVATE
    lvgl::lvgl
    smooth_ui_toolkit
    spdlog::spdlog
    Threads::Threads
    ${CC1101_CHAT_SDL_LIBS}
    ${CC1101_CHAT_SYSTEM_LIBS}
)
if(NOT CC1101_CHAT_USE_SDL)
    target_link_libraries(${CC1101_CHAT_BIN_NAME} PRIVATE ${CC1101_CHAT_GPIOD_TARGET})
endif()

set_target_properties(${CC1101_CHAT_BIN_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CC1101_CHAT_OUTPUT_DIR}"
)
