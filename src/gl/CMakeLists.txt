project(gl)

add_library(${PROJECT_NAME}
    ../XC.h
    ../XCAssert.h
    ../XCReport.h
    ../XCType.h
    BufferObject.cpp BufferObject.h
    ComputeTexture1D.cpp ComputeTexture1D.h
    DeviceInfo.cpp DeviceInfo.h
    EasyShaderProgram.cpp EasyShaderProgram.h
    EasyTextureDrawer.cpp EasyTextureDrawer.h
    ExtendShader.cpp ExtendShader.h
    FontDrawer.cpp FontDrawer.h
    Framebuffer.cpp Framebuffer.h
    Global.cpp Global.h
    PrimitiveDrawer.cpp PrimitiveDrawer.h
    Root.cpp Root.h
    Task.cpp Task.h
    TextObject.cpp TextObject.h
    Texture.cpp Texture.h
    Triangulator.cpp Triangulator.h
    Util.cpp Util.h
    Vector2.h
    Vector2I.h
    Vector3.h
    Vector4.h
    Vector4I.h
    VertexArrayObject.cpp VertexArrayObject.h
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
