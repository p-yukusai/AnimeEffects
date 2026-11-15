#include <QFileInfo>
#include <QUndoCommand>
#include "XC.h"
#include "core/Project.h"

namespace {
const int kParaThreadCount = 4;
const int kStandardFps = 24;
const int kDefaultFrameCount = 60 * 10;
} // namespace

namespace core {
//-------------------------------------------------------------------------------------------------
Project::Attribute::Attribute(): mImageSize(), mFrameRange(1, kDefaultFrameCount), mFps(kStandardFps), mLoop(false) {}

//-------------------------------------------------------------------------------------------------
Project::Project(QString aFileName, Animator& aAnimator, Hook* aHookGrabbed):
    mLifeLink(),
    mFileName(aFileName),
    mAttribute(),
    mId(QUuid::createUuid()),
    mParalleler(new thr::Paralleler(kParaThreadCount)),
    mResourceHolder(),
    mCommandStack(),
    mObjectTree(),
    mAnimator(aAnimator),
    mHook(aHookGrabbed) {
    if (!aFileName.isEmpty()) {
        setFileName(aFileName);
    }
    QFileInfo aFile = QFileInfo(aFileName);
    if(aFile.exists() && QDir(aFile.absoluteDir()).exists() && QFileInfo::exists(aFile.absoluteDir().absolutePath() + '/' + aFile.baseName() + ".aemus")){
        QFile aemus = QFile(aFile.absoluteFilePath().remove(".anie").append(".aemus"));
        if(aemus.open(QIODevice::ReadOnly)){
            QJsonDocument doc = QJsonDocument::fromJson(aemus.readAll());
            if(!doc.isEmpty() && AudioPlaybackWidget::deserialize(doc.object(), pConf)){
                mediaRefresh = true;
                uiRefresh = true;
            }
            else {
                pConf->clear();
                pConf->emplace_back();
            }
        }
    }
    onTimeLineModified.connect([=](TimeLineEvent& aEvent, bool) { aEvent.setProject(*this); });
    onTimeLineModified.connect(&mObjectTree, &ObjectTree::onTimeLineModified);
    onTreeRestructured.connect(&mObjectTree, &ObjectTree::onTreeRestructured);
    onResourceModified.connect(&mObjectTree, &ObjectTree::onResourceModified);
    onProjectAttributeModified.connect(&mObjectTree, &ObjectTree::onProjectAttributeModified);

#ifdef UNUSE_PARALLEL
#else
    mParalleler->start();
#endif
}

Project::~Project() {
    // clear all command firstly
    mCommandStack.clear();
    if(mediaPlayer && mediaPlayer->playing){
        for(auto player : mediaPlayer->players){
            if (player) { player->stop(); }
        }
        mediaPlayer->playing = false;
    }
}

void Project::setFileName(const QString& aFileName) {
    mFileName = aFileName;
    mResourceHolder.setRootPath(QFileInfo(aFileName).path());
}

TimeInfo Project::currentTimeInfo() const {
    TimeInfo time;
    time.frameRange = mAttribute.frameRange();
    time.fps = mAttribute.fps();
    time.loop = mAttribute.loop();
    time.frame = mAnimator.currentFrame();
    return time;
}

} // namespace core
