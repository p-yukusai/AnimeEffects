project(AnimeEffects)
ae_enable_warnings()

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
    DestinationPicker.cpp DestinationPicker.h
    DockSash.cpp DockSash.h
    DriverHolder.cpp DriverHolder.h
    EasyDialog.cpp EasyDialog.h
    GUIResources.cpp GUIResources.h
    GeneralSettingDialog.cpp GeneralSettingDialog.h
    KeyBindingDialog.cpp KeyBindingDialog.h
    KeyCommandInvoker.cpp KeyCommandInvoker.h
    KeyCommandMap.cpp KeyCommandMap.h
    LocaleDecider.cpp LocaleDecider.h
    LocaleParam.h
    LeakSuppressions.cpp
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
    AnimationSettingDialog.cpp AnimationSettingDialog.h
    NewProjectDialog.cpp NewProjectDialog.h
    AppStyle.cpp AppStyle.h
    ObjectTreeWidget.cpp ObjectTreeWidget.h
    PlayBackWidget.cpp PlayBackWidget.h
    ProjectHook.cpp ProjectHook.h
    ProjectTabBar.cpp ProjectTabBar.h
    PropertyWidget.cpp PropertyWidget.h
    ResourceDialog.cpp ResourceDialog.h
    ResourceTreeWidget.cpp ResourceTreeWidget.h
    TabCloseButton.cpp TabCloseButton.h
    TargetWidget.cpp TargetWidget.h
    ThinSplitter.cpp ThinSplitter.h
    TimeLineEditorWidget.cpp TimeLineEditorWidget.h
    TimeLineWidget.cpp TimeLineWidget.h
    TimeRuler.cpp TimeRuler.h
    ToolWidget.cpp ToolWidget.h
    ViaPoint.cpp ViaPoint.h
    exportdiag.cpp exportdiag.h
    # Extracted into the catalogs from here (see the i18n block below): the file is
    # GENERATED from assets/export/ExportTargets.txt by
    # scripts/i18n/gen_export_targets_i18n.py, and lupdate cannot read a data file.
    ../ctrl/ExportTargetStrings.h
    ExportFacts.cpp ExportFacts.h
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
    theme/Colors.h
    theme/Icons.cpp theme/Icons.h
    theme/TimeLine.cpp theme/TimeLine.h
    tool/tool_BonePanel.cpp tool/tool_BonePanel.h
    tool/tool_FFDPanel.cpp tool/tool_FFDPanel.h
    tool/tool_FlowLayout.cpp tool/tool_FlowLayout.h
    tool/tool_ItemTable.cpp tool/tool_ItemTable.h
    tool/tool_Items.cpp tool/tool_Items.h
    tool/ToolSlider.cpp tool/ToolSlider.h
    tool/tool_MeshPanel.cpp tool/tool_MeshPanel.h
    tool/tool_ModePanel.cpp tool/tool_ModePanel.h
    tool/tool_PosePanel.cpp tool/tool_PosePanel.h
    tool/tool_SRTPanel.cpp tool/tool_SRTPanel.h
    tool/tool_ViewPanel.cpp tool/tool_ViewPanel.h
    ${app_icon}
    AudioPlaybackWidget.cpp
    AudioPlaybackWidget.h
    prop/EasingPickWidget.cpp
    prop/EasingPickWidget.h
        prop/bezierCurveEditor.h
        prop/bezierCurveEditor.cpp
    qml/QmlTheme.cpp qml/QmlTheme.h
    qml/AnimationSettingViewModel.cpp qml/AnimationSettingViewModel.h
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ..
)

# Compile the Qt Quick scenes used by migrated dialogs into the binary
# resource (so they ship in the packaged build) and validate them at build
# time via qmlcachegen.
qt_add_qml_module(${PROJECT_NAME}
    URI AnimeEffects.Dialogs
    VERSION 1.0
    QML_FILES
        qml/ScrubSpinBox.qml
        qml/AnimationSettingDialog.qml
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        AE_MAJOR_VERSION=1
        AE_MINOR_VERSION=7
        AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    $<$<CONFIG:Debug>:QT_DEBUG>
    # Path to the bundled sample project (a debug/dev fixture auto-opened under
    # QT_DEBUG); it is not shipped in any runtime data directory.
    AE_SAMPLE_PSD="${CMAKE_SOURCE_DIR}/assets/sample.psd"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core Qt6::Core5Compat Qt6::Widgets
        Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
        Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
        Qt6::Quick Qt6::Qml Qt6::QuickWidgets
        cmnd core ctrl gl img thr util media
)

# i18n (Qt LinguistTools, loaded in the root CMakeLists): regenerates the
# translation sources under assets/locale/translation_<locale>.ts (via the
# update_translations target) and builds the .qm (via release_translations).
# The .qm are embedded into the binary under :/i18n below, so the translator
# resolves them regardless of where the executable lives; translators only
# ever edit the committed .ts.
# WHICH SOURCES lupdate READS. qt_add_translations scans the sources of the target it
# is given, and that target is the executable in this directory: everything in
# src/util, src/ctrl, src/core, src/img and src/media was invisible to it. The effect
# is not a warning or a failure, it is silence: strings written there never reach a
# catalog, so they cannot be translated, and a translator has no way to notice a
# sentence that was never offered. 48 of the strings in src/ctrl/ExportTarget.cpp
# (every note and every error the export dialog states) were in that state, and the
# measured cost is that the user reads "This destination cannot be exported: no frames
# left" half in English.
#
# The library targets are declared before this one, so their source lists are known
# here; collecting them explicitly keeps ONE translation scan for the whole
# application instead of one per library (each of which would need its own .ts).
# The paths have to be ABSOLUTE. A target's SOURCES property holds them as they were
# written in that target's own CMakeLists (src/ctrl says "CmndName.cpp"), and a
# relative path handed to a project generated in src/gui is read there: lupdate was
# told to scan "src/gui/CmndName.cpp", found nothing, and reported no error, because a
# missing file in a file list is not a failure. That is how this fix first did nothing.
set(AE_I18N_LIBRARIES util thr cmnd gl img core media ctrl)
set(AE_I18N_SOURCES "")
foreach(lib ${AE_I18N_LIBRARIES})
    if(TARGET ${lib})
        get_target_property(lib_sources ${lib} SOURCES)
        foreach(src ${lib_sources})
            # Generator expressions (the QML/autogen entries) cannot be resolved at
            # configure time and carry no translatable strings anyway.
            if(src MATCHES "^\\$<")
                continue()
            endif()
            if(NOT IS_ABSOLUTE "${src}")
                get_filename_component(src "${src}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}/src/${lib}")
            endif()
            list(APPEND AE_I18N_SOURCES "${src}")
        endforeach()
    endif()
endforeach()
# lupdate follows #include, so the headers carry their inline tr() calls with them,
# but a header no .cpp includes is only found if it is listed: the generated table
# strings are exactly that case (nothing includes them; the generator's output is
# compiled into ctrl by name).
list(APPEND AE_I18N_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../ctrl/ExportTargetStrings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/exportdiag.h
)
list(REMOVE_DUPLICATES AE_I18N_SOURCES)

qt_add_translations(${PROJECT_NAME}
    TS_FILE_BASE translation
    TS_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/assets/locale
    SOURCES ${AE_I18N_SOURCES}
    QM_FILES_OUTPUT_VARIABLE qm_files
)
# Embed the build-produced .qm (they are build artifacts, not source) under
# :/i18n so the app stays self-contained for its translations.
qt_add_resources(${PROJECT_NAME} "translations"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${qm_files}
)
# rcc needs the .qm present when it runs; they are produced by lrelease.
add_dependencies(${PROJECT_NAME} release_translations)

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
