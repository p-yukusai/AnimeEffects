project(thr)

add_library(${PROJECT_NAME}
    ../XC.h
    ../XCAssert.h
    ../XCReport.h
    ../XCType.h
    Paralleler.cpp Paralleler.h
    Task.cpp Task.h
    TaskQueue.cpp TaskQueue.h
    Worker.cpp Worker.h
)
target_include_directories(${PROJECT_NAME} PUBLIC
    ..
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
)
