project(cmnd)

add_library(${PROJECT_NAME}
    ../XC.h
    ../XCAssert.h
    ../XCReport.h
    ../XCType.h
    Base.h
    BasicCommands.h
    DoneDeleter.h
    Listener.h
    Scalable.cpp Scalable.h
    ScopedMacro.h
    ScopedUndoSuspender.h
    SignalNotifier.h
    SleepableObject.h
    Stable.h
    Stack.cpp Stack.h
    UndoneDeleter.h
    Vector.h
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
