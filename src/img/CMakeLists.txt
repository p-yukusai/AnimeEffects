project(img)
ae_enable_warnings()

add_library(${PROJECT_NAME}
    ../XC.h
    ../XCAssert.h
    ../XCReport.h
    ../XCType.h
    BlendMode.cpp BlendMode.h
    BlendModeName.cpp BlendModeName.h
    Buffer.cpp Buffer.h
    ColorRGBA.h
    Format.h
    GridMeshCreator.cpp GridMeshCreator.h
    MeshContentLevel.cpp MeshContentLevel.h
    PSDFormat.h
    PSDReader.cpp PSDReader.h
    PSDUtil.cpp PSDUtil.h
    PSDWriter.cpp PSDWriter.h
    PixelPos.h
    Premultiply.h
    Quad.h
    ResourceData.cpp ResourceData.h
    ResourceHandle.cpp ResourceHandle.h
    ResourceNode.cpp ResourceNode.h
    Util.cpp Util.h
        ORAReader.h
        oraParser.cpp
        oraParser.h
)
target_include_directories(${PROJECT_NAME} PUBLIC
    ..
)

# Vendored third-party headers (src/deps) are SYSTEM headers, so they are passed
# with -isystem and their warnings are not reported as ours.
#
# zip_file.h is Thomas Fussell's MIT-licensed single-header library bundling
# miniz 1.15: 5692 lines exposing ~199 public API functions, of which we use a
# handful. Under -Wall that is 24 "unused function" warnings per translation unit
# that includes it, ~2976 across the project. The honest answer is neither to
# edit a vendored file to placate our linter nor to suppress the diagnostic, but
# to say plainly that this is not our code. Verified both ways: the same header
# warns 24 times through a plain -I and zero times through -isystem.
#
# PUBLIC, because gui and ctrl link img and include the same header.
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/src/deps
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    AE_MAJOR_VERSION=1
    AE_MICRO_VERSION=1
    AE_MINOR_VERSION=7
    USE_GL_CORE_PROFILE
)

target_link_libraries(${PROJECT_NAME} PUBLIC
        Qt6::Core
        Qt6::Core5Compat
        Qt6::Widgets
        Qt6::Gui
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Xml
        util
        pugixml
)
