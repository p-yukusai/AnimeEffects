project(util)

add_library(${PROJECT_NAME}
    ../XC.h
    ../XCAssert.h
    ../XCReport.h
    ../XCType.h
    ArrayBlock.h
    ArrayBuffer.h
    BinarySpacePartition2D.h
    ByteBuffer.cpp ByteBuffer.h
    Circle.h
    CollDetect.cpp CollDetect.h
    DealtList.h
    Dir4.cpp Dir4.h
    Easing.cpp Easing.h
    EasingName.cpp EasingName.h
    FergusonCoonsSpline.h
    Finally.h
    FixedObject.h
    IDAssigner.h
    IDSolver.h
    IEasyIterator.h
    IProgressReporter.h
    ITreeSeeker.h
    IndexTable.cpp IndexTable.h
    LifeLink.h
    LinkPointer.h
    MathUtil.cpp MathUtil.h
    NetworkUtil.cpp NetworkUtil.h
    NonCopyable.h
    PackBits.cpp PackBits.h
    PlacePointer.h
    Range.h
    Segment2D.h
    SelectArgs.h
    Signaler.h
    SlotId.h
    StreamReader.h
    StreamWriter.h
    TextUtil.cpp TextUtil.h
    TreeIterator.h
    TreeNodeBase.h
    TreePos.cpp TreePos.h
    TreeSeekIterator.h
    TreeUtil.cpp TreeUtil.h
    Triangle2D.cpp Triangle2D.h
    Triangle2DPos.cpp Triangle2DPos.h
    TriangleRasterizer.cpp TriangleRasterizer.h
)
target_include_directories(${PROJECT_NAME} PUBLIC
    ..
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=6
    AE_MICRO_VERSION=0
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
)
