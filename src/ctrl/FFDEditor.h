#ifndef CTRL_FFDEDITOR_H
#define CTRL_FFDEDITOR_H

#include <QScopedPointer>
#include "util/Circle.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/RenderInfo.h"
#include "core/CameraInfo.h"
#include "core/AbstractCursor.h"
#include "core/LayerMesh.h"
#include "ctrl/IEditor.h"
#include "ctrl/DriverResources.h"
#include "ctrl/FFDParam.h"
#include "ctrl/UILogger.h"
#include "ctrl/ffd/ffd_Target.h"
#include "ctrl/ffd/ffd_IMode.h"

namespace ctrl {

class FFDEditor: public IEditor {
public:
    FFDEditor(core::Project& aProject, DriverResources& aDriverResources, UILogger& aUILogger);
    ~FFDEditor();

    bool setTarget(core::ObjectNode* aTarget) override;

    void updateParam(const FFDParam& aParam);
    bool updateCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor) override;
    void updateEvent(EventType) override;

    void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter) override;
    void flushPendingWork() override;

private:
    core::LayerMesh* getCurrentAreaMesh(core::ObjectNode& aNode) const;
    bool resetCurrentTarget(QString* aMessage = nullptr);
    void updateTargetsKeys();
    void createMode();
    void finalize();

    core::Project& mProject;
    DriverResources& mDriverResources;
    UILogger& mUILogger;
    FFDParam mParam;
    QScopedPointer<ffd::IMode> mCurrent;

    // target info
    core::ObjectNode* mRootTarget;
    ffd::Targets mTargets;
};

} // namespace ctrl

#endif // CTRL_FFDEDITOR_H
