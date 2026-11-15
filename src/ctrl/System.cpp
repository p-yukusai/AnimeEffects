#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <memory>
#include <qstandardpaths.h>
#ifdef Q_OS_WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "gl/Global.h"
#include "gl/DeviceInfo.h"
#include "ctrl/System.h"

#include "core/TimeKeyExpans.h"
#include "core/TimeLine.h"
#include "ctrl/ProjectSaver.h"
#include "ctrl/ProjectLoader.h"
#include "ctrl/ImageFileLoader.h"

using namespace core;

namespace {

// Recovery copies of never-saved projects land in a dedicated subdir of the
// app cache (same per-user transient area as lastproject.cache). The filename
// is the project's own UUID: stable per project across repeated autosaves
// (the same file is overwritten) and unique across projects and app sessions
// — no pid/address reuse, no ASLR address in a user-visible name.
QString recoveryDirName() { return QStringLiteral("autosave"); }
QString recoveryFileName(const core::Project& aProject) {
    return QStringLiteral("AnimeEffects_unsaved_%1.anie").arg(aProject.id().toString(QUuid::WithoutBraces));
}
} // namespace

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

QString System::LoadResult::warningTitle() {
    // Shared by the PSD importer's degraded layers and the loader's skeleton
    // cap, so the title stays the generic one. New source text here would make
    // the catalogs stale; the line itself carries the specifics.
    return QCoreApplication::translate("ctrl::System", "Import Warnings");
}

QString System::LoadResult::warningSummary() {
    return QCoreApplication::translate(
        "ctrl::System", "The project opened, but some layers could not be reproduced exactly."
    );
}

//-------------------------------------------------------------------------------------------------
System::System(const QString& aCacheDir):
    mCacheDir(aCacheDir), mProjects(), mAnimator() {}

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
        // The project opened, but the importer may have had to degrade a layer it
        // cannot reproduce. Carry those notes out on the result instead of
        // dropping them: the GUI shows them on success, which is the only moment
        // the user can learn why the file looks different from its source.
        return LoadResult(mProjects.back(), loader.warnings());
    }

    LoadResult result;
    result.message.append(loader.log());
    result.message.append("Failed to load the image file.");
    return result;
}

System::LoadResult
System::openProject(
    const QString& aFileName, Project::Hook* aHookGrabbed, util::IProgressReporter& aReporter, bool aRecordRecent
) {
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
                // ProjectLoader has already rebuilt the derived bone influence
                // caches; this block only drops the timeline blend, master and
                // pose state that deserialisation filled.
                const auto aOwner = mProjects.back()->objectTree().topNode();
                for (ObjectNode::Iterator itr(aOwner); itr.hasNext();) {
                    ObjectNode* node = itr.next();
                    XC_PTR_ASSERT(node);
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
            // Recovery copies are transient (the file is deleted after a
            // successful recovery open), so they must not pollute the
            // recent-files list with a path that will not exist tomorrow.
            if (aRecordRecent) {
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
            }

            // A successful .anie load can still carry a limitation (a skeleton
            // past PosePalette::kMaxCount). The loader's warnings are the lines
            // the GUI shows once, while the user is looking at the file.
            return LoadResult(mProjects.back(), loader.warnings());
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

System::SaveResult System::saveProject(core::Project& aProject, bool aInteractive) {
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
            // The confirmation is interactive-only: an autosave must never
            // block the user. A non-ASCII project was already saved (and the
            // override accepted) once to reach this path, so re-saving it is
            // no new risk — proceed silently.
            if (!aInteractive) {
                saver.log().append("ASCII override.");
            } else {
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

System::SaveResult System::saveRecoveryCopy(core::Project& aProject, QString& aOutFilePath) {
    const int index = mProjects.indexOf(&aProject);
    if (index < 0) {
        return SaveResult(false, "Invalid project reference.");
    }

    const QString dir = mCacheDir + "/" + recoveryDirName();
    if (!makeSureCacheDirectory(dir)) {
        return SaveResult(false, "Failed to create the autosave directory.");
    }

    const QString path = QDir(dir).filePath(recoveryFileName(aProject));
    const QString tmpPath = path + ".tmp";

    ctrl::ProjectSaver saver;
    if (!saver.save(tmpPath, aProject)) {
        return SaveResult(false, "Failed to save the recovery copy. (" + saver.log() + ")");
    }
    if (!safeRename(tmpPath, path)) {
        return SaveResult(false, "Failed to finalize the recovery copy.");
    }

    aOutFilePath = path;
    return SaveResult(true, "Success.");
}

void System::clearRecoveryCopy(core::Project& aProject) {
    const QString path = QDir(mCacheDir + "/" + recoveryDirName()).filePath(recoveryFileName(aProject));
    removeRecoveryCopy(path);
}

QStringList System::recoveryCopyPaths() const {
    const QDir dir(mCacheDir + "/" + recoveryDirName());
    if (!dir.exists()) {
        return {};
    }

    QStringList paths;
    const auto names = dir.entryList({QStringLiteral("AnimeEffects_unsaved_*.anie")}, QDir::Files);
    for (const auto& name : names) {
        paths << dir.filePath(name);
    }

    // Newest first: the most recent work is offered first.
    std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).lastModified() > QFileInfo(b).lastModified();
    });
    return paths;
}

void System::removeRecoveryCopy(const QString& aFilePath) {
    QFile::remove(aFilePath);
}
void System::sweepStaleRecoveryTmp() {
    // The autosave dir is dedicated to these files: the only writer is
    // saveRecoveryCopy, whose `<name>.anie.tmp` is either renamed to
    // `<name>.anie` or left behind by a crash. Sweeping just the `.tmp`
    // pattern (never the finished `<name>.anie`) removes that litter; it can
    // never touch a real project file, which lives outside this dir.
    const QDir dir(mCacheDir + "/" + recoveryDirName());
    if (!dir.exists())
        return;
    const auto names = dir.entryList({QStringLiteral("AnimeEffects_unsaved_*.anie.tmp")}, QDir::Files);
    for (const auto& name : names) {
        QFile::remove(dir.filePath(name));
    }
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

#ifdef Q_OS_WINDOWS
    // MoveFileExW with MOVEFILE_REPLACE_EXISTING replaces the destination in
    // one step on the same volume — no delete-then-rename window where a
    // crash destroys the project file. (QFile::rename on Windows refuses to
    // overwrite; don't depend on Qt internals for that.)
    if (::MoveFileExW(reinterpret_cast<const wchar_t*>(aSrc.utf16()),
                      reinterpret_cast<const wchar_t*>(aDst.utf16()),
                      MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }
#else
    // Unix: QFile::rename maps to rename(2), which replaces an existing
    // destination atomically.
    if (QFile::rename(aSrc, aDst)) {
        return true;
    }
#endif

    // Last resort: remove + rename. The remove runs only when the destination
    // exists; the rename itself is attempted unconditionally, so a missing
    // destination (or a transient platform-branch/atomic failure) still gets
    // a plain rename instead of a spurious failure.
    if (QFile::exists(aDst)) {
        if (!QFile::remove(aDst)) {
            qDebug() << "failed to remove a project file.";
            return false;
        }
    }
    if (QFile::rename(aSrc, aDst)) {
        return true;
    }

    qDebug() << "failed to rename a project file.";
    return false;
}

} // namespace ctrl
