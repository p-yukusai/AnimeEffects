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
    ThumbnailCache.cpp ThumbnailCache.h
    # The dock header's channel list: the panel's ONE control is a view action, so the
    # design hosts it in the header and the list is its own widget (not a QMenu: the
    # design's 22px rows and its swatch gaps are not reachable through AppStyle's menu
    # metrics). Part of the always-built set: it needs no ImGui, only the model's plain
    # row struct and the theme tokens.
    GraphChannelPopup.cpp GraphChannelPopup.h
    TimeLineHeader.cpp TimeLineHeader.h
    PlayBackWidget.cpp PlayBackWidget.h
    ProjectHook.cpp ProjectHook.h
    ProjectTabBar.cpp ProjectTabBar.h
    PropertyWidget.cpp PropertyWidget.h
    ViewportInput.cpp ViewportInput.h
    PointerSmoothing.cpp PointerSmoothing.h
    ResourceDialog.cpp ResourceDialog.h
    ResourceTreeWidget.cpp ResourceTreeWidget.h
    TabCloseButton.cpp TabCloseButton.h
    TargetWidget.cpp TargetWidget.h
    ThinSplitter.cpp ThinSplitter.h
    TimeLineEditorWidget.cpp TimeLineEditorWidget.h
    TimeLineWidget.cpp TimeLineWidget.h
    TimeRuler.cpp TimeRuler.h
    FrameRuler.cpp FrameRuler.h
    ToolWidget.cpp ToolWidget.h
    ViaPoint.cpp ViaPoint.h
    # The Properties panel's command log: one text document whose runs carry each
    # line's kind ink (a badge, not markup), so a selection can still cross
    # entries. LogEntry is the widget-free description ViaPoint hands it.
    LogEntry.h
    CommandLogView.cpp CommandLogView.h
    # The export form: a Qt Quick scene (qml/ExportDialog.qml) in a QDialog shell,
    # with everything it decides in the widget-free view model beside it.
    ExportDialog.cpp ExportDialog.h
    qml/ExportViewModel.cpp qml/ExportViewModel.h
    # The new-project form: the same Qt Quick scene-in-a-QDialog shell, with its
    # own widget-free view model beside it.
    qml/NewProjectViewModel.cpp qml/NewProjectViewModel.h
    # The three remaining forms ported to the same shell: project settings, key
    # bindings and general settings, each with its own widget-free view model.
    ProjectSettingDialog.cpp ProjectSettingDialog.h
    qml/ProjectSettingViewModel.cpp qml/ProjectSettingViewModel.h
    qml/KeyBindingViewModel.cpp qml/KeyBindingViewModel.h
    qml/GeneralSettingViewModel.cpp qml/GeneralSettingViewModel.h
    # Extracted into the catalogs from here (see the i18n block below): the file is
    # GENERATED from assets/export/ExportTargets.txt by
    # scripts/i18n/gen_export_targets_i18n.py, and lupdate cannot read a data file.
    ../ctrl/ExportTargetStrings.h
    ExportFacts.cpp ExportFacts.h
    menu/menu_ProgressReporter.cpp menu/menu_ProgressReporter.h
    prop/prop_ItemBase.h
    prop/prop_Items.cpp prop/prop_Items.h
    prop/prop_KeyAccessor.cpp prop/prop_KeyAccessor.h
    res/res_ImageSetter.h
    res/res_Item.cpp res/res_Item.h
    res/res_Notifier.cpp res/res_Notifier.h
    res/res_ResourceUpdater.cpp res/res_ResourceUpdater.h
    theme/Theme.cpp theme/Theme.h
    theme/Colors.h
    theme/Graph.cpp theme/Graph.h
    theme/Icons.cpp theme/Icons.h
    theme/TimeLine.cpp theme/TimeLine.h
    # The audio row's slider. It is the ONE survivor of the widget tools dock: the
    # dock's own panels are the QML scene below now, but this control's reason for
    # existing (a QSS QSlider cannot round a handle) is unchanged where it is used.
    tool/ToolSlider.cpp tool/ToolSlider.h
    ${app_icon}
    AudioPlaybackWidget.cpp
    AudioPlaybackWidget.h
    prop/EasingPickWidget.cpp
    prop/EasingPickWidget.h
        prop/bezierCurveEditor.h
        prop/bezierCurveEditor.cpp
    qml/QmlTheme.cpp qml/QmlTheme.h
    qml/AnimationSettingViewModel.cpp qml/AnimationSettingViewModel.h
    qml/DestinationPickerModel.cpp qml/DestinationPickerModel.h
    qml/QmlFormAccessibility.cpp qml/QmlFormAccessibility.h
    qml/InspectorViewModel.cpp qml/InspectorViewModel.h
    qml/ObjectTreeModel.cpp qml/ObjectTreeModel.h
    qml/InspectorRowModel.cpp qml/InspectorRowModel.h
    qml/FilterStackModel.cpp qml/FilterStackModel.h
    qml/ToolViewModel.cpp qml/ToolViewModel.h
    qml/FilterNames.h
    QmlHost.h
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
        qml/DestinationPicker.qml
        # The export form and the dialog-local controls it is built from. Sibling
        # files in this directory are implicitly importable as types, so the
        # dialog's own components live here beside the shared set rather than
        # behind a second import.
        qml/ExportDialog.qml
        qml/NewProjectDialog.qml
        qml/ProjectSettingDialog.qml
        qml/KeyBindingDialog.qml
        qml/GeneralSettingDialog.qml
        qml/AeSectionHeader.qml
        qml/AeFieldLabel.qml
        qml/AeDisclosure.qml
        qml/AeCheckBox.qml
        qml/AeComboBox.qml
        qml/AeNumberField.qml
        qml/AeFrameRangeRow.qml
        qml/AeFactRow.qml
        # Shared controls for migrated dialogs. One visual language across the
        # export flow: borderless buttons, raised fields, the theme's own icon
        # set. A dialog that needs something else adds it here rather than
        # hand-rolling a second look in its own file.
        qml/AeButton.qml
        qml/AeIconButton.qml
        qml/AeTextField.qml
        qml/AeToolTip.qml
        qml/AeScrollBar.qml
        # The Properties dock. The scene owns layout/ink; InspectorViewModel
        # owns every document decision.
        qml/PropertiesPanel.qml
        qml/InspectorRow.qml
        qml/InspectorKeyDot.qml
        qml/InspectorNumberField.qml
        qml/InspectorAssetField.qml
        qml/InspectorFilterCard.qml
        qml/InspectorFilterStack.qml
        qml/InspectorMaskCard.qml
        # The Tools dock. Same split as the Properties dock: the scene owns layout and
        # ink, gui::ToolViewModel owns every tool decision.
        qml/ToolsPanel.qml
        qml/ToolHeading.qml
        qml/ToolRow.qml
        qml/ToolIconButton.qml
        qml/ToolSliderRow.qml
        qml/ToolCheckRow.qml
        qml/ToolModeRow.qml
        qml/ToolBrushSection.qml
        qml/ToolTextAction.qml
        # The Animation dock's tree column. The scene owns the rows' ink and the pointer;
        # gui::ObjectTreeModel owns what a row means (its rows are the timeline's lanes too).
        qml/ObjectTreePanel.qml
        qml/TreeRow.qml
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
        Qt6::Quick Qt6::Qml Qt6::QuickWidgets Qt6::QuickControls2
        cmnd core ctrl gl img thr util media
)

# Win32 libraries the platform code calls directly: DwmSetWindowAttribute (the
# popup rounding in gui/PopupWindow.h) and the user32 entry points around it.
# Upstream's gui target links these too; this branch's rewrite of this file had
# dropped the block, which only shows up when linking for Windows.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE user32 dwmapi)
endif()

# ---------------------------------------------------------------------------
# The Dear ImGui units of the viewport overlay (the animation graph panel).
#
# The dependency itself is declared ONCE in the root CMakeLists: the option
# AE_ENABLE_IMGUI (default ON), the pinned FetchContent declaration and the shared
# ae_imgui library that compiles ImGui's four core sources. The tests link that
# same library, which is why neither the fetch nor the core sources live here.
#
# What each unit is: GraphPanelFrame is the panel's frame driver and its ink (the
# ImGui context, GL3 backend, atlas and per-frame body the dock's Graph tab runs,
# plus gui::drawPanel and the two GL passes); GraphPanelWidget is that dock page, the
# panel's only host; GraphPanelModel is what the frame draws, as engine-neutral
# geometry (so the panel's content is testable without GL); ImGuiInputBridge is the
# Qt->ImGuiIO translation plus the WantCapture gate the application's key routing
# obeys; ImGuiQtFontLoader makes ImGui rasterize through Qt (CJK, .ttc, CFF);
# ImGuiA11y publishes the panel to the screen-reader tree, which ImGui itself cannot
# do.
#
# ImGuiGLBackend.cpp is the GL3 backend reached through a loader shim: it includes
# gui/ImGuiGLBackend.h - the wrapper over gl::Global::functions() - ahead of the
# backend, because 1.92 no longer includes IMGUI_IMPL_OPENGL_LOADER_CUSTOM itself.
# Listing the backend as its own source too would define every
# ImGui_ImplOpenGL3_* twice. (It was called ImGuiSpikeGL while it belonged to the
# spike that measured the integration.)
# ---------------------------------------------------------------------------
# THE CANVAS PAINTER HOST: the ruler's and the body's Canvas ink. The surface
# (TimeLineEditorWidget) compiles its Canvas branch from the same definition.
target_sources(${PROJECT_NAME} PRIVATE
    FrameRulerCanvas.cpp FrameRulerCanvas.h
InkCanvas.cpp InkCanvas.h
    TimeBodyCanvas.cpp TimeBodyCanvas.h
)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::CanvasPainter)
target_compile_definitions(${PROJECT_NAME} PRIVATE AE_CANVAS_PAINTER=1)

if(AE_ENABLE_IMGUI)
    target_sources(${PROJECT_NAME} PRIVATE
        GraphPanelFrame.cpp GraphPanelFrame.h
        Ink.h InkImGui.h InkImGui.cpp InkQPainter.h InkQPainter.cpp
        GraphPanelWidget.cpp GraphPanelWidget.h
        ImGuiGLBackend.cpp ImGuiGLBackend.h
        ImGuiInputBridge.cpp ImGuiInputBridge.h
        ImGuiQtFontLoader.cpp ImGuiQtFontLoader.h
        GraphPanelModel.cpp GraphPanelModel.h
        ImGuiA11y.cpp ImGuiA11y.h
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE ae_imgui)
    # The one compile-time gate the panel's units obey.
    target_compile_definitions(${PROJECT_NAME} PRIVATE AE_ENABLE_IMGUI=1)
endif()

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
