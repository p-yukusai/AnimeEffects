#ifndef CTRL_IMAGEFILELOADER_H
#define CTRL_IMAGEFILELOADER_H

#define NEW_ORA_PARSER

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QScopedPointer>
#include "util/IProgressReporter.h"
#include "gl/DeviceInfo.h"
#include "core/Project.h"
#include "core/ObjectTree.h"
#include "img/Util.h"
#include "core/FolderNode.h"
#ifdef OLD_ORA_PARSER
#include "img/ORAReader.h"
#endif

namespace ctrl {

class ImageFileLoader {
public:
    explicit ImageFileLoader(gl::DeviceInfo  aDeviceInfo);

    void setCanvasSize(const QSize& aSize, bool aForce);

    bool load(const QString& aPath, core::Project& aProject, util::IProgressReporter& aReporter);

    // How to import an .ora: Ask shows the interactive layered/merged prompt (the default,
    // the app behavior); Layered/Merged skip the dialog entirely (headless callers such as
    // the test harness have no user to answer it).
    enum class OraImportMode { Ask, Layered, Merged };
    void setOraImportMode(OraImportMode aMode) { mOraImportMode = aMode; }

    [[nodiscard]] const QString& log() const { return mLog; }

    // What the import had to accept in order to open the file at all: a layer
    // kind this renderer cannot reproduce (the layer is then empty, or is kept
    // as a flattened raster), and the layer masks that were parsed and then
    // ignored. Deliberately separate from log(): log() is the one failure
    // message, and a headless caller (verify_blur, the exporters) treats a
    // non-failing load as silent. An empty list means the file imported exactly
    // as authored.
    [[nodiscard]] const QStringList& warnings() const { return mWarnings; }

private:
    bool createEmptyCanvas(core::Project& aProject, const QString& aTopName, const QSize& aCanvasSize);

    bool loadPsd(core::Project& aProject, util::IProgressReporter& aReporter);

    bool loadOra(core::Project& aProject, util::IProgressReporter& aReporter);

    bool loadImage(core::Project& aProject, util::IProgressReporter& aReporter);

    static QRect calculateBoundingRectFromChildren(const core::ObjectNode& aNode);
    static void setDefaultPosturesFromInitialRects(core::ObjectNode& aNode);
    bool checkTextureSizeError(uint32 aWidth, uint32 aHeight);

    QString mLog;
    QStringList mWarnings;
    QFileInfo mFileInfo;
    gl::DeviceInfo mGLDeviceInfo;
    QSize mCanvasSize;
    bool mForceCanvasSize;
    OraImportMode mOraImportMode = OraImportMode::Ask;

    // This class is not a QObject, so it has no metaobject to resolve tr() through and
    // Qt's tr() does not exist here: a free function in this file provided one, in a
    // context ("image_file_loader") that no catalog has. This declares tr() in the
    // context the catalogs and lupdate agree on, which is the class name. It has to
    // stay at the END of the class: the macro ends with `private:`.
public:
    Q_DECLARE_TR_FUNCTIONS(ImageFileLoader)
private:
    static void parseOraLayer(layer &lyr, core::FolderNode* current, img::ResourceNode* resCurrent, const float* globalDepth, const float* parentDepth, core::Project* aProject);
    static void parseOraStack(stack &stk, std::vector<core::FolderNode*>& treeStack, std::vector<img::ResourceNode*>& resStack, float* globalDepth, QRect rect, core::Project* aProject, int* progress, util::IProgressReporter& aReporter);
};

} // namespace ctrl

#endif // CTRL_IMAGEFILELOADER_H
