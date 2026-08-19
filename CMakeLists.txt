cmake_minimum_required(VERSION 3.16)
project(Wobblin VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Prevent Qt from marking its includes as "system" (/external:I), which
# breaks Qt's relative internal includes on this toolchain.
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Network)

set(APP_ICON_RESOURCE "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icon.ico")
    set(APP_ICON_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_ICON_RESOURCE}" "1 ICON DISCARDABLE \"${CMAKE_CURRENT_SOURCE_DIR}/icon.ico\"\n")
endif()

add_executable(Wobblin WIN32 
    app.cpp
    ${APP_ICON_RESOURCE}
)

# MSBuild/AUTOMOC: use /FS so parallel compiles of app.cpp + mocs_compilation
# don't collide on the shared vc143.pdb.
if(MSVC)
    target_compile_options(Wobblin PRIVATE /FS)
endif()
# toolchain /utf-8 corrupts the moc-generated qtmochelpers.h parse, so opt out.
set_target_properties(Wobblin PROPERTIES QT_NO_UTF8_SOURCE ON)

# Qt6::Core's INTERFACE_COMPILE_OPTIONS appends /utf-8 via a generator
# expression keyed on the QT_NO_UTF8_SOURCE target property. On this toolchain
# /utf-8 corrupts the moc-generated qtmochelpers.h parse (C2059 'namespace'),
# so strip /utf-8 directly from the imported target's interface options.
get_target_property(_qt_iface_opts Qt6::Core INTERFACE_COMPILE_OPTIONS)
if(_qt_iface_opts)
    set(_qt_iface_filtered "")
    foreach(_opt ${_qt_iface_opts})
        if(_opt MATCHES "/utf-8")
            # drop /utf-8
        else()
            list(APPEND _qt_iface_filtered "${_opt}")
        endif()
    endforeach()
    set_target_properties(Qt6::Core PROPERTIES INTERFACE_COMPILE_OPTIONS "${_qt_iface_filtered}")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(Wobblin PRIVATE "-static-libgcc" "-static-libstdc++")
endif()

target_link_libraries(Wobblin PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Widgets 
    Qt6::Svg
    Qt6::Network
)

if(WIN32)
    target_link_libraries(Wobblin PRIVATE
        d3d11
        dxgi
        dcomp
        d3dcompiler
        dwmapi
        ole32
        shell32
        user32
        gdi32
    )

    find_program(WINDEPLOYQT_EXECUTABLE windeployqt6 HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Wobblin POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                "$<TARGET_FILE:Wobblin>"
                --no-translations
            COMMENT "Deploying Qt runtime for Wobblin.exe"
        )
    endif()
endif()

# Qt6's MSVC toolchain injects /utf-8 and /external:I into the compile
# command (via Qt6::Core's INTERFACE_COMPILE_OPTIONS, applied when we link
# above). On this toolchain /utf-8 corrupts the moc-generated qtmochelpers.h
# parse (C2059 'namespace'), and /external:I breaks Qt's relative internal
# includes. Strip both so cl uses the default codepage + plain /I includes.
if(MSVC)
    get_target_property(_copts Wobblin COMPILE_OPTIONS)
    set(_filtered "")
    if(_copts)
        foreach(_opt ${_copts})
            if(_opt MATCHES "/utf-8" OR _opt MATCHES "/external:I")
                # drop Qt's problematic flags
            else()
                list(APPEND _filtered "${_opt}")
            endif()
        endforeach()
    endif()
    set_target_properties(Wobblin PROPERTIES COMPILE_OPTIONS "${_filtered}")
endif()
