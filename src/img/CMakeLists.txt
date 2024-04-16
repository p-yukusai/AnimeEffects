project(img)

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
    PSDFormat.h
    PSDReader.cpp PSDReader.h
    PSDUtil.cpp PSDUtil.h
    PSDWriter.cpp PSDWriter.h
    PixelPos.h
    Quad.h
    ResourceData.cpp ResourceData.h
    ResourceHandle.cpp ResourceHandle.h
    ResourceNode.cpp ResourceNode.h
    Util.cpp Util.h
)
target_include_directories(${PROJECT_NAME} PUBLIC
    ..
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    AE_MAJOR_VERSION=1
    AE_MICRO_VERSION=0
    AE_MINOR_VERSION=6
    AE_PROJECT_FORMAT_MAJOR_VERSION=0
    AE_PROJECT_FORMAT_MINOR_VERSION=7
    AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION=0
    AE_PROJECT_FORMAT_OLDEST_MINOR_VERSION=4
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
)
