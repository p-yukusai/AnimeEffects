project(core)
ae_enable_warnings()

add_library(${PROJECT_NAME}
    ../XC.h
    ../XCAssert.h
    ../XCReport.h
    ../XCType.h
    AbstractCursor.cpp AbstractCursor.h
    Animator.h
    AutoEasing.cpp AutoEasing.h
    Bone2.cpp Bone2.h
    BoneExpans.cpp BoneExpans.h
    BoneInfluenceMap.cpp BoneInfluenceMap.h
    BoneKey.cpp BoneKey.h
    BoneKeyUpdater.cpp BoneKeyUpdater.h
    BonePolygonIndex.cpp BonePolygonIndex.h
    BoneShape.cpp BoneShape.h
    CameraInfo.cpp CameraInfo.h
    ChannelKey.cpp ChannelKey.h
    ChannelSpec.cpp ChannelSpec.h
    ChannelTrack.cpp ChannelTrack.h
    ClippingFrame.cpp ClippingFrame.h
    Constant.h
    BlurKey.h
    BlurStateKey.cpp BlurStateKey.h
    FormatVersion.h
    DepthKey.cpp DepthKey.h
    Deserializer.cpp Deserializer.h
    DestinationTexturizer.cpp DestinationTexturizer.h
    FFDKey.cpp FFDKey.h
    FFDKeyUpdater.cpp FFDKeyUpdater.h
    FilterActivity.h
    FilterFrame.cpp FilterFrame.h
    FilterMask.cpp FilterMask.h
    FolderNode.cpp FolderNode.h
    Frame.h
    GridMesh.cpp GridMesh.h
    GridWeightsRemap.cpp GridWeightsRemap.h
    HSVKey.cpp
    HeightMap.cpp HeightMap.h
    HsvKey.h
    HSVStateKey.cpp HSVStateKey.h
    ImageKey.cpp ImageKey.h
    ImagePyramid.cpp ImagePyramid.h
    MaskPyramid.cpp MaskPyramid.h
    ImageKeyUpdater.cpp ImageKeyUpdater.h
    KeyId.cpp KeyId.h
    KeyProportional.h
    LayerMesh.cpp LayerMesh.h
    LayerNode.cpp LayerNode.h
    MeshKey.cpp MeshKey.h
    MeshKeyUtil.cpp MeshKeyUtil.h
    MeshTransformer.cpp MeshTransformer.h
    MeshTransformerResource.cpp MeshTransformerResource.h
    MoveEvaluator.cpp MoveEvaluator.h
    MoveKey.cpp MoveKey.h
    MoveStateKey.cpp MoveStateKey.h
    ObjectNode.h
    ObjectNodeUtil.cpp ObjectNodeUtil.h
    ObjectTree.cpp ObjectTree.h
    ObjectTreeEvent.cpp ObjectTreeEvent.h
    ObjectTreeNotifier.cpp ObjectTreeNotifier.h
    ObjectType.h
    OpacityKey.cpp OpacityKey.h
    PoseKey.cpp PoseKey.h
    PosePalette.cpp PosePalette.h
    Project.cpp Project.h
    ProjectEvent.cpp ProjectEvent.h
    PropertyTrack.cpp PropertyTrack.h
    RenderInfo.h
    Renderer.h
    ResourceEvent.cpp ResourceEvent.h
    ResourceHolder.cpp ResourceHolder.h
    ResourceUpdatingWorkspace.cpp ResourceUpdatingWorkspace.h
    RotateKey.cpp RotateKey.h
    SRTExpans.cpp SRTExpans.h
    ScaleKey.cpp ScaleKey.h
    Serializer.cpp Serializer.h
    ShaderHolder.cpp ShaderHolder.h
    SkinWeights.cpp SkinWeights.h
    StateKey.cpp StateKey.h
    StateTrack.cpp StateTrack.h
    TimeCacheAccessor.cpp TimeCacheAccessor.h
    TimeCacheLock.cpp TimeCacheLock.h
    TimeFormat.cpp TimeFormat.h
    TimeInfo.h
    TimeKey.cpp TimeKey.h
    TimeKeyBlender.cpp TimeKeyBlender.h
    TimeKeyExpans.cpp TimeKeyExpans.h
    VirtualTexture.cpp VirtualTexture.h
    TimeKeyGatherer.cpp TimeKeyGatherer.h
    TimeKeyPos.h
    TimeKeyType.h
    TimeLine.cpp TimeLine.h
    TimeLineEvent.cpp TimeLineEvent.h
    MaskKey.cpp MaskKey.h
    WaveKey.h
    WaveStateKey.cpp WaveStateKey.h
    WavePass.h
    WorldBlurMath.h
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
    Qt::Core
    Qt::Widgets
    Qt::Gui
    Qt::OpenGL
    Qt::Xml
    cmnd
    gl
    img
    thr
    util
)
