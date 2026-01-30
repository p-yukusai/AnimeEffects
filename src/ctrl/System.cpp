#include <QDir>
#include <QFile>
#include <memory>
#include <qstandardpaths.h>
#include "gl/Global.h"
#include "gl/DeviceInfo.h"
#include "ctrl/System.h"

#include "core/BoneKey.h"
#include "core/TimeKeyBlender.h"
#include "ctrl/ProjectSaver.h"
#include "ctrl/ProjectLoader.h"
#include "ctrl/ImageFileLoader.h"

using namespace core;

namespace {
static const int kStandardFps = 24;
}

namespace ctrl {
//-------------------------------------------------------------------------------------------------
System::SaveResult::SaveResult(): success(), message() {}

System::SaveResult::SaveResult(bool aSuccess, const QString& aMessage): success(aSuccess), message(aMessage) {}

//-------------------------------------------------------------------------------------------------
System::LoadResult::LoadResult(): project(), message() {}

System::LoadResult::LoadResult(core::Project* aProject, const QString& aMessage):
    project(aProject), message(aMessage) {}

System::LoadResult::LoadResult(core::Project* aProject, const QStringList& aMessage):
    project(aProject), message(aMessage) {}

QString System::LoadResult::messages() const {
    QString msgs;
    for (auto it = message.rbegin(); it != message.rend(); ++it) {
        msgs += *it + "\n";
    }
    return msgs;
}

//-------------------------------------------------------------------------------------------------
System::System(const QString& aResourceDir, const QString& aCacheDir):
    mResourceDir(aResourceDir), mCacheDir(aCacheDir), mProjects(), mAnimator() {}

System::~System() { closeAllProjects(); }

void System::setAnimator(Animator& aAnimator) { mAnimator = &aAnimator; }

System::LoadResult System::newProject(
    const QString& aFileName,
    const core::Project::Attribute& aAttr,
    core::Project::Hook* aHookGrabbed,
    util::IProgressReporter& aReporter,
    bool aSpecifiesCanvasSize
) {
    std::unique_ptr<core::Project::Hook> hookScope(aHookGrabbed);

    XC_ASSERT(mAnimator);
    XC_ASSERT(gl::DeviceInfo::validInstanceExists());

    gl::Global::makeCurrent();

    std::unique_ptr<core::Project> projectScope;
    projectScope.reset(new Project(QString(), *mAnimator, hookScope.release()));
    projectScope->attribute() = aAttr;
    projectScope->resourceHolder().setRootPath(QFileInfo(aFileName).path());

    ctrl::ImageFileLoader loader(gl::DeviceInfo::instance());
    loader.setCanvasSize(aAttr.imageSize(), aSpecifiesCanvasSize);

    if (loader.load(aFileName, *projectScope, aReporter)) {
        mProjects.push_back(projectScope.release());
        return LoadResult(mProjects.back(), "Success.");
    }

    LoadResult result;
    result.message.append(loader.log());
    result.message.append("Failed to load the image file.");
    return result;
}

System::LoadResult
System::openProject(const QString& aFileName, Project::Hook* aHookGrabbed, util::IProgressReporter& aReporter) {
    std::unique_ptr<core::Project::Hook> hookScope(aHookGrabbed);

    XC_ASSERT(mAnimator);
    XC_ASSERT(gl::DeviceInfo::validInstanceExists());

    gl::Global::makeCurrent();

    if (!aFileName.isEmpty()) {
        std::unique_ptr<core::Project> projectScope;
        projectScope.reset(new Project(aFileName, *mAnimator, hookScope.release()));

        ctrl::ProjectLoader loader;
        if (loader.load(aFileName, *projectScope, gl::DeviceInfo::instance(), aReporter)) {
            mProjects.push_back(projectScope.release());
            {
                // Due to the transition to Qt6 every cache on load is just f*cked, TODO: Fix cache loading
                const auto aOwner = mProjects.back()->objectTree().topNode();
                for (ObjectNode::Iterator itr(aOwner); itr.hasNext();) {
                    ObjectNode* node = itr.next();
                    XC_PTR_ASSERT(node);
                    // Clear deserialized bone cache
                    for (const auto bone: node->timeLine()->map(TimeKeyType_Bone)) {
                        auto* boneKey = dynamic_cast<BoneKey*>(node->timeLine()->timeKey(TimeKeyType_Bone, bone->frame()));
                        boneKey->resetCaches(*mProjects.back(), *node);
                    }
                    // Clear cache, master cache and pose palette
                    auto& curr = node->timeLine()->current();
                    curr.clearCaches();
                    curr.clearMasterCache();
                    curr.posePalette().clear();
                    auto& work = node->timeLine()->working();
                    work.clearCaches();
                    work.clearMasterCache();
                    work.posePalette().clear();
                }
            }
            // Get settings
            QSettings settings;
            QStringList recentfiles = settings.value("projectloader/recents").toStringList();

            // Check file length
            if (recentfiles.length() != 8) {
                recentfiles.append(aFileName);
            }
            // If length eight remove first
            else if (recentfiles.length() >= 8) {
                recentfiles.pop_front();
            }
            // Save
            if (!settings.value("projectloader/recents").toStringList().contains(aFileName)) {
                settings.setValue("projectloader/recents", recentfiles);
                settings.sync();
            }

            return LoadResult(mProjects.back(), "Success.");
        }

        for (auto log : loader.log()) {
            qDebug() << log;
        }

        LoadResult result;
        result.message.append(loader.log());
        result.message.append("Failed to load project.");
        return result;
    }

    return LoadResult(nullptr, "Empty file.");
}

bool isAllAscii(const std::string& str)
{
    return std::all_of(
                str.begin(),
                str.end(),
                [](const unsigned char& ch){
                    return ch <= 127;
                });
}

System::SaveResult System::saveProject(core::Project& aProject) {
    const int index = mProjects.indexOf(&aProject);

    if (index < 0 || mProjects.count() <= index) {
        return SaveResult(false, "Invalid project reference.");
    }

    auto project = mProjects.at(index);

    if (project && !project->isNameless()) {
        const QString outputPath = project->fileName();
        const QString cachePath = mCacheDir + "/lastproject.cache";

        // ensure the string "%20" is not used
        if (outputPath.contains("%20")) {
            return SaveResult(false, "Please do not use the string '%20' when naming .anie files.");
        }

        // create cache directory
        if (!makeSureCacheDirectory(mCacheDir)) {
            return SaveResult(false, "Failed to create cache directory.");
        }
        ctrl::ProjectSaver saver;

        // ensure only ascii is used
        std::string filename = QFileInfo(outputPath).completeBaseName().toStdString();
        if (!isAllAscii(filename)) {
            QMessageBox confirmation;
            confirmation.setText(QCoreApplication::translate(
                "projectSave", "Non ascii characters detected, this may cause issues.\nSave anyway?", nullptr
            ));
            confirmation.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
            confirmation.setDefaultButton(QMessageBox::Cancel);
            confirmation.setWindowTitle(
                QCoreApplication::translate("projectSave", "Unicode on filename detected", nullptr)
            );
            if (confirmation.exec() != QMessageBox::Yes) {
                return SaveResult(false, "Please only utilize ASCII characters for naming .anie files.");
            }
            saver.log().append("ASCII override.");
        }

        if (!saver.save(cachePath, *project)) {
            return SaveResult(false, "Failed to save project. (" + saver.log() + ")");
        }

        if (!safeRename(cachePath, outputPath)) {
            return SaveResult(false, "Failed to rename the project file.");
        }

        project->commandStack().resetEditingOrigin();
        qDebug() << "save the project file. " << outputPath;
        return SaveResult(true, "Success.");
    }

    return SaveResult(false, "Invalid operation.");
}

bool System::closeProject(core::Project& aProject) {
    const int index = mProjects.indexOf(&aProject);

    if (0 <= index && index < mProjects.count()) {
        auto ptr = mProjects.at(index);
        mProjects.removeAt(index);
        delete ptr;
        return true;
    }
    return false;
}

void System::closeAllProjects() {
    qDeleteAll(mProjects);
    mProjects.clear();
}

core::Project* System::project(int aIndex) {
    XC_ASSERT(0 <= aIndex && aIndex < mProjects.count());
    return mProjects[aIndex];
}

const core::Project* System::project(int aIndex) const {
    XC_ASSERT(0 <= aIndex && aIndex < mProjects.count());
    return mProjects[aIndex];
}

bool System::hasModifiedProject() const {
    for (auto project : mProjects) {
        if (project->isModified())
            return true;
    }
    return false;
}

bool System::makeSureCacheDirectory(const QString& aCacheDir) {
    if (!QDir::current().exists(aCacheDir)) {
        if (!QDir::current().mkpath(aCacheDir)) {
            qDebug() << "failed to create cache directory.";
            return false;
        }
    }
    return true;
}

bool System::safeRename(const QString& aSrc, const QString& aDst) {
    if (!QFile::exists(aSrc)) {
        qDebug() << "failed to find a project cache file.";
        return false;
    }

    if (QFile::exists(aDst)) {
        if (!QFile::remove(aDst)) {
            qDebug() << "failed to remove a project file.";
            return false;
        }
    }

    if (!QFile::rename(aSrc, aDst)) {
        qDebug() << "failed to rename a project file.";
        return false;
    }
    return true;
}

} // namespace ctrl
