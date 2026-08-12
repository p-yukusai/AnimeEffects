project(AnimeEffects)

set(MACOSX_BUNDLE_ICON_FILE AnimeEffects.icns)	

if(WIN32)
    set(app_icon "icon.rc")
elseif(APPLE)
    set(app_icon "../AnimeEffects.icns")
    set_source_files_properties(${app_icon} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
elseif(UNIX)
    file(COPY ${CMAKE_SOURCE_DIR}/dist/AnimeEffects.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps/)
    file(COPY ${CMAKE_SOURCE_DIR}/dist/AnimeEffects.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/)
    file(COPY ${CMAKE_SOURCE_DIR}/dist/AnimeEffects.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable)
    file(COPY ${CMAKE_SOURCE_DIR}/dist/AnimeEffects.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications/)
    file(COPY ${CMAKE_SOURCE_DIR}/dist/AnimeEffects.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications/org.anie.AnimeEffects)
    
endif()

add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE
    ../XC.h
    ../XCAssert.h
    ../XCReport.h
    ../XCType.h
    CanvasMover.cpp CanvasMover.h
    DriverHolder.cpp DriverHolder.h
    EasyDialog.cpp EasyDialog.h
    ExportDialog.cpp ExportDialog.h
    GUIResources.cpp GUIResources.h
    GeneralSettingDialog.cpp GeneralSettingDialog.h
    KeyBindingDialog.cpp KeyBindingDialog.h
    KeyCommandInvoker.cpp KeyCommandInvoker.h
    KeyCommandMap.cpp KeyCommandMap.h
    LocaleDecider.cpp LocaleDecider.h
    LocaleParam.h
    MSVCBackTracer.cpp MSVCBackTracer.h
    MSVCMemoryLeakDebugger.cpp MSVCMemoryLeakDebugger.h
    Main.cpp
    MainDisplayMode.h
    MainDisplayStyle.h
    MainDisplayWidget.cpp MainDisplayWidget.h
    MainMenuBar.cpp MainMenuBar.h
    MainViewSetting.h
    MainWindow.cpp MainWindow.h
    MouseSetting.cpp MouseSetting.h
    MouseSettingDialog.cpp MouseSettingDialog.h
    AnimationSettingDialog.cpp AnimationSettingDialog.h
    NewProjectDialog.cpp NewProjectDialog.h
    ObjectTreeWidget.cpp ObjectTreeWidget.h
    PlayBackWidget.cpp PlayBackWidget.h
    ProjectHook.cpp ProjectHook.h
    ProjectTabBar.cpp ProjectTabBar.h
    PropertyWidget.cpp PropertyWidget.h
    ResourceDialog.cpp ResourceDialog.h
    ResourceTreeWidget.cpp ResourceTreeWidget.h
    TabCloseButton.cpp TabCloseButton.h
    TargetWidget.cpp TargetWidget.h
    TimeLineEditorWidget.cpp TimeLineEditorWidget.h
    TimeLineInfoWidget.cpp TimeLineInfoWidget.h
    TimeLineWidget.cpp TimeLineWidget.h
    ToolWidget.cpp ToolWidget.h
    ViaPoint.cpp ViaPoint.h
    exportdiag.cpp exportdiag.h
    menu/menu_ProgressReporter.cpp menu/menu_ProgressReporter.h
    obj/obj_InsertItem.cpp obj/obj_InsertItem.h
    obj/obj_Item.cpp obj/obj_Item.h
    obj/obj_MoveItem.cpp obj/obj_MoveItem.h
    obj/obj_Notifiers.cpp obj/obj_Notifiers.h
    obj/obj_RemoveItem.cpp obj/obj_RemoveItem.h
    obj/obj_Util.cpp obj/obj_Util.h
    prop/prop_AttrGroup.cpp prop/prop_AttrGroup.h
    prop/prop_Backboard.cpp prop/prop_Backboard.h
    prop/prop_ConstantPanel.cpp prop/prop_ConstantPanel.h
    prop/prop_CurrentKeyPanel.cpp prop/prop_CurrentKeyPanel.h
    prop/prop_DefaultKeyPanel.cpp prop/prop_DefaultKeyPanel.h
    prop/prop_HeaderButton.cpp prop/prop_HeaderButton.h
    prop/prop_ItemBase.h
    prop/prop_Items.cpp prop/prop_Items.h
    prop/prop_KeyAccessor.cpp prop/prop_KeyAccessor.h
    prop/prop_KeyGroup.cpp prop/prop_KeyGroup.h
    prop/prop_KeyKnocker.cpp prop/prop_KeyKnocker.h
    prop/prop_Panel.cpp prop/prop_Panel.h
    prop/prop_ProjectPanel.cpp prop/prop_ProjectPanel.h
    res/res_ImageSetter.h
    res/res_Item.cpp res/res_Item.h
    res/res_Notifier.cpp res/res_Notifier.h
    res/res_ResourceUpdater.cpp res/res_ResourceUpdater.h
    theme/Theme.cpp theme/Theme.h
    theme/TimeLine.cpp theme/TimeLine.h
    tool/tool_BonePanel.cpp tool/tool_BonePanel.h
    tool/tool_FFDPanel.cpp tool/tool_FFDPanel.h
    tool/tool_FlowLayout.cpp tool/tool_FlowLayout.h
    tool/tool_ItemTable.cpp tool/tool_ItemTable.h
    tool/tool_Items.cpp tool/tool_Items.h
    tool/tool_MeshPanel.cpp tool/tool_MeshPanel.h
    tool/tool_ModePanel.cpp tool/tool_ModePanel.h
    tool/tool_PosePanel.cpp tool/tool_PosePanel.h
    tool/tool_SRTPanel.cpp tool/tool_SRTPanel.h
    tool/tool_ViewPanel.cpp tool/tool_ViewPanel.h
    ${app_icon}
    AudioPlaybackWidget.cpp
    AudioPlaybackWidget.h
    prop/splineWidget.cpp
    prop/splineWidget.h
        prop/bezierCurveEditor.h
        prop/bezierCurveEditor.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ..
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        AE_MAJOR_VERSION=1
        AE_MINOR_VERSION=7
        AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)

target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core Qt6::Core5Compat Qt6::Widgets
        Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
        Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
        cmnd core ctrl gl img thr util
)

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

message("Copying data")
# the app chdirs to its executable directory (Main.cpp: QDir::setCurrent(appDir))
# and opens ./data from there, so data must sit next to the exe. Multi-config
# generators put the exe in a per-config subdir, hence $<CONFIG>. Wipe the
# destination first so deleted source files do not linger (copy_directory only
# ever adds/updates).
if (APPLE)
    set(AE_DATA_SUBDIR AnimeEffects.app/Contents/MacOS/data)
else ()
    set(AE_DATA_SUBDIR data)
endif ()
if (CMAKE_CONFIGURATION_TYPES)
    set(AE_DATA_DEST ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${AE_DATA_SUBDIR})
else ()
    set(AE_DATA_DEST ${CMAKE_CURRENT_BINARY_DIR}/${AE_DATA_SUBDIR})
endif ()
# Generated icon dir: build-tree only, never wiped, so ninja can cache the
# tint outputs across builds. copy_data merges it into the exe's data dir.
set(AE_GEN_ICONS_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated-icons)

add_custom_target(copy_data
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${AE_DATA_DEST}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data ${AE_DATA_DEST}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${AE_GEN_ICONS_DIR} ${AE_DATA_DEST}
)
message("Data copied")
add_dependencies(${PROJECT_NAME} copy_data)

# ---------------------------------------------------------------------------
# Build-time icon tinting.
# The repo tracks one canonical monochrome set in data/icons; this regenerates
# each theme's icon dir with the theme color (and the scrollbar thumbs with
# their per-theme shades). Outputs are tracked by ninja, so tinting only
# re-runs when the canonical set or the tool changes.
# ---------------------------------------------------------------------------
add_executable(icon_tint ${CMAKE_SOURCE_DIR}/tools/icon_tint/icon_tint.cpp)
target_link_libraries(icon_tint PRIVATE Qt6::Gui Qt6::Core)
set_target_properties(icon_tint PROPERTIES EXCLUDE_FROM_ALL FALSE)

file(GLOB AE_CANONICAL_ICONS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/data/icons/*.png ${CMAKE_SOURCE_DIR}/data/icons/*.svg)
# Manifest of the canonical set: rewritten whenever the glob changes, so icon
# additions/removals dirty every theme rule (per-file deps alone cannot see a
# file disappear) and the per-theme wipe clears the stale outputs.
set(AE_CANONICAL_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/ae-canonical-icons.txt)
file(WRITE ${AE_CANONICAL_MANIFEST} "${AE_CANONICAL_ICONS}")

set(AE_ICON_THEMES
    "default#3c3c3c#b0b0b0#8f8f8f"
    "classic#3c3c3c#b0b0b0#8f8f8f"
    "breeze_dark#e0e0e0#7e7e7e#a0a0a0"
    "breeze_dark_high_dpi#e0e0e0#7e7e7e#a0a0a0"
)

set(AE_ALL_ICON_OUTPUTS "")
foreach(entry ${AE_ICON_THEMES})
    string(REPLACE "#" ";" parts "${entry}")
    list(GET parts 0 theme)
    list(GET parts 1 color)
    list(GET parts 2 thumb)
    list(GET parts 3 hover)

    set(theme_out "")
    foreach(icon ${AE_CANONICAL_ICONS})
        get_filename_component(name ${icon} NAME)
        list(APPEND theme_out ${AE_GEN_ICONS_DIR}/themes/${theme}/icon/${name})
    endforeach()
    list(APPEND AE_ALL_ICON_OUTPUTS ${theme_out})

    set(theme_cmds
        # wipe first so outputs that left the canonical set (e.g. png -> svg
        # migrations) do not linger and get merged into the exe data
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${AE_GEN_ICONS_DIR}/themes/${theme}/icon
        COMMAND $<TARGET_FILE:icon_tint> ${CMAKE_SOURCE_DIR}/data/icons ${AE_GEN_ICONS_DIR}/themes/${theme}/icon ${color}
    )
    if(thumb AND hover)
        list(APPEND theme_cmds
            COMMAND $<TARGET_FILE:icon_tint> ${CMAKE_SOURCE_DIR}/data/icons ${AE_GEN_ICONS_DIR}/themes/${theme}/icon ${thumb} scrollbar_handle.png
            COMMAND $<TARGET_FILE:icon_tint> ${CMAKE_SOURCE_DIR}/data/icons ${AE_GEN_ICONS_DIR}/themes/${theme}/icon ${hover} scrollbar_handle_hover.png
        )
    endif()
    add_custom_command(
        OUTPUT ${theme_out}
        ${theme_cmds}
        DEPENDS ${AE_CANONICAL_ICONS} ${AE_CANONICAL_MANIFEST} icon_tint
        VERBATIM
    )
endforeach()

add_custom_target(tint_icons ALL DEPENDS ${AE_ALL_ICON_OUTPUTS})
add_dependencies(copy_data tint_icons)
