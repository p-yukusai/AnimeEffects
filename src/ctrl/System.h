#ifndef CTRL_SYSTEM_H
#define CTRL_SYSTEM_H

#include <QString>
#include "util/NonCopyable.h"
#include "util/IProgressReporter.h"
#include "gl/DeviceInfo.h"
#include "core/Project.h"

namespace ctrl {

class System: private util::NonCopyable {
public:
    struct SaveResult {
        bool success;
        QString message;
        SaveResult();
        SaveResult(bool, const QString&);
        explicit operator bool() const { return success; }
    };

    struct LoadResult {
        core::Project* project;
        // Failure: the lines to show in the error box, innermost first.
        // Success: the importer's warnings -- conditions the user should know
        // about (a PSD layer kind AnimeEffects cannot reproduce, ignored layer
        // masks) that did not stop the project from opening. Empty means the
        // import was exact and nothing should be shown.
        QStringList message;
        LoadResult();
        LoadResult(core::Project*, const QString&);
        LoadResult(core::Project*, const QStringList&);
        QString messages() const;
        explicit operator bool() const { return project; }

        // The title and one-line summary of the popup shown for a successful
        // import whose warnings are non-empty. They live beside the list, in a
        // context the translation catalogs already carry, so the GUI only has to
        // hand the lines over.
        static QString warningTitle();
        static QString warningSummary();
    };

    System(const QString& aCacheDir);
    ~System();

    void setAnimator(core::Animator& aAnimator);

    LoadResult newProject(
        const QString& aFileName,
        const core::Project::Attribute& aAttr,
        core::Project::Hook* aHookGrabbed,
        util::IProgressReporter& aReporter,
        bool aSpecifiesCanvasSize
    );

    LoadResult
    openProject(const QString& aFileName, core::Project::Hook* aHookGrabbed, util::IProgressReporter& aReporter, bool aRecordRecent = true);

    SaveResult saveProject(core::Project& aProject, bool aInteractive = true);
    // Writes a crash-recovery copy of an unsaved (nameless) project into the
    // app cache. Does NOT touch the project's fileName or dirty flag: the
    // project stays unsaved and Save As still prompts.
    SaveResult saveRecoveryCopy(core::Project& aProject, QString& aOutFilePath);
    void clearRecoveryCopy(core::Project& aProject);
    // Stale autosave copies of never-saved projects, newest first — offered
    // to the user at startup for crash recovery.
    QStringList recoveryCopyPaths() const;
    void removeRecoveryCopy(const QString& aFilePath);
    // Removes leftover `AnimeEffects_unsaved_*.anie.tmp` files: incomplete
    // recovery writes from a crash between saver.save and safeRename. Only
    // ever called at startup, before any autosave can create a fresh one.
    void sweepStaleRecoveryTmp();

    bool closeProject(core::Project& aProject);

    void closeAllProjects();

    bool hasProject() const { return !mProjects.isEmpty(); }
    int projectCount() const { return mProjects.count(); }
    bool hasModifiedProject() const;

    core::Project* project(int aIndex);
    const core::Project* project(int aIndex) const;

private:
    static bool makeSureCacheDirectory(const QString& aCacheDir);
    static bool safeRename(const QString& aSrc, const QString& aDst);

    const QString mCacheDir;
    QVector<core::Project*> mProjects;
    core::Animator* mAnimator;
};

} // namespace ctrl

#endif // CTRL_SYSTEM_H
