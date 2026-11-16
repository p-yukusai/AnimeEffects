#ifndef CTRL_PROJECTLOADER_H
#define CTRL_PROJECTLOADER_H

#include <QStringList>
#include <QVersionNumber>
#include "util/StreamReader.h"
#include "util/IProgressReporter.h"
#include "gl/DeviceInfo.h"
#include "core/Project.h"

namespace ctrl {

class ProjectLoader {
public:
    ProjectLoader();

    bool load(
        const QString& aPath,
        core::Project& aProject,
        const gl::DeviceInfo& aGLDeviceInfo,
        util::IProgressReporter& aReporter
    );

    const QStringList& log() const { return mLog; }

    // Limitations the project carries INTO the session: conditions that did
    // not fail the load but change what the user will see. An .anie is
    // reproduced exactly; the one runtime limit is the skeleton bone cap.
    // Empty means nothing to say.
    const QStringList& warnings() const { return mWarnings; }

private:
    bool readHeader(util::LEStreamReader& aReader);
    bool readGlobalBlock(util::LEStreamReader& aReader, core::Project& aProject);
    QStringList mLog;
    QStringList mWarnings;
    QVersionNumber mVersion;
};

} // namespace ctrl

#endif // CTRL_PROJECTLOADER_H
