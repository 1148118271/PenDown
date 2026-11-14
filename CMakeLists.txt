cmake_minimum_required(VERSION 3.21)

project(PenDown VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------------
# wxWidgets
#   vcpkg:   find_package(wxWidgets CONFIG REQUIRED) -> wx::core wx::base
#   系统安装: find_package(wxWidgets) 的 module 模式
# ------------------------------------------------------------------
find_package(wxWidgets CONFIG QUIET)
if(wxWidgets_FOUND)
    set(PENDOWN_WX_LIBS wx::core wx::base)
    if(TARGET wx::adv)
        list(APPEND PENDOWN_WX_LIBS wx::adv)
    endif()
    if(TARGET wx::stc)
        list(APPEND PENDOWN_WX_LIBS wx::stc)
    endif()
    message(STATUS "wxWidgets (CONFIG): ${wxWidgets_VERSION_STRING}")
else()
    find_package(wxWidgets REQUIRED COMPONENTS core base adv stc)
    include(${wxWidgets_USE_FILE})
    set(PENDOWN_WX_LIBS ${wxWidgets_LIBRARIES})
    message(STATUS "wxWidgets (MODULE): ${wxWidgets_VERSION_STRING}")
endif()

# ------------------------------------------------------------------
# 应用
# ------------------------------------------------------------------
set(PENDOWN_SOURCES
    src/app.cpp

    src/core/note_store.cpp
    src/core/text.cpp
    src/core/selftest.cpp
    src/core/git_sync.cpp
    src/core/style_model.cpp
    src/core/link_model.cpp
    src/core/note_storage.cpp

    src/ui/main_frame.cpp
    src/ui/note_tree_ctrl.cpp
    src/ui/note_window.cpp
    src/ui/note_editor.cpp
    src/ui/find_replace_dialog.cpp
    src/ui/color_popup.cpp
    src/ui/style_popup.cpp
    src/ui/name_dialog.cpp
    src/ui/rounded_panel.cpp
    src/ui/settings_dialog.cpp
    src/ui/git_config.cpp
    src/ui/window_effects.cpp
    src/ui/app_font.cpp
    src/ui/icon_library.cpp
    src/ui/icon_button.cpp
)

add_executable(PenDown WIN32 ${PENDOWN_SOURCES})

target_include_directories(PenDown PRIVATE src res)
target_link_libraries(PenDown PRIVATE ${PENDOWN_WX_LIBS})
if(WIN32)
    target_link_libraries(PenDown PRIVATE dwmapi crypt32)
endif()

target_compile_definitions(PenDown PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(PenDown PRIVATE /utf-8 /W3 /permissive- /MP)
    # 清单由资源文件提供（comctl32 v6 + Per-Monitor V2），不让链接器再生成一份
    target_link_options(PenDown PRIVATE /MANIFEST:NO)

    # 分发用：把 VC++ 运行库复制到 exe 同目录，目标机器不需要安装开发工具
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET PenDown POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
                    "$<TARGET_FILE_DIR:PenDown>"
            VERBATIM)
    endif()
endif()

if(WIN32)
    target_sources(PenDown PRIVATE res/PenDown.rc)

    # vcpkg 的 wxWidgets 默认是动态库，把运行时 DLL 拷到 exe 旁边
    set(PENDOWN_COPY_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/copy_runtime_dlls.cmake")
    file(WRITE "${PENDOWN_COPY_SCRIPT}" [[
if(FILES)
    foreach(file IN LISTS FILES)
        if(EXISTS "${file}")
            file(COPY "${file}" DESTINATION "${DEST}")
        endif()
    endforeach()
endif()
]])
    add_custom_command(TARGET PenDown POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DDEST=$<TARGET_FILE_DIR:PenDown>"
                "-DFILES=$<TARGET_RUNTIME_DLLS:PenDown>"
                -P "${PENDOWN_COPY_SCRIPT}"
        VERBATIM)

    # 图标随程序分发
    file(GLOB PENDOWN_ICON_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/*")
    if(PENDOWN_ICON_FILES)
        add_custom_command(TARGET PenDown POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:PenDown>/icons"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${PENDOWN_ICON_FILES}
                    "$<TARGET_FILE_DIR:PenDown>/icons"
            VERBATIM)
        install(FILES ${PENDOWN_ICON_FILES} DESTINATION icons)
    endif()
endif()

install(TARGETS PenDown RUNTIME DESTINATION .)
