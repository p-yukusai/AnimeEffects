#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "core/Constant.h"
#include "ctrl/CmndName.h"
#include "gui/ResourceDialog.h"
#include "gui/prop/prop_ConstantPanel.h"
#include "gui/prop/prop_Items.h"
#include <QEvent>
#include <QTimer>

namespace {
class ObjectNodeAttrNotifier: public cmnd::Listener {
public:
    ObjectNodeAttrNotifier(core::Project& aProject, core::ObjectNode& aNode): mProject(aProject), mNode(aNode) {}
    virtual void onExecuted() { mProject.onNodeAttributeModified(mNode, false); }
    virtual void onUndone() { mProject.onNodeAttributeModified(mNode, true); }
    virtual void onRedone() { mProject.onNodeAttributeModified(mNode, false); }

private:
    core::Project& mProject;
    core::ObjectNode& mNode;
};

// The theme stylesheets declare their own QComboBox min-width, which is
// re-applied during style polish and overrides any minimumWidth set in C++
// (the panel is built before the theme stylesheets are loaded). Re-assert a
// content-based floor after every polish/style/font change so the longest
// mode names ("Linear Dodge", "Lighter Color") never ellipsize in narrow
// docks. sizeHint() is recomputed at that point, so the floor tracks the
// active locale font.
class BlendComboWidthGuard: public QObject {
public:
    BlendComboWidthGuard(QComboBox* aCombo): QObject(aCombo) {
        aCombo->installEventFilter(this);
    }

    bool eventFilter(QObject* aWatched, QEvent* aEvent) override {
        if (aEvent->type() == QEvent::Polish ||
            aEvent->type() == QEvent::StyleChange ||
            aEvent->type() == QEvent::FontChange) {
            auto* combo = static_cast<QComboBox*>(aWatched);
            QTimer::singleShot(0, combo, [combo]() {
                combo->setMinimumWidth(combo->sizeHint().width() + 8);
            });
        }
        return QObject::eventFilter(aWatched, aEvent);
    }
};
} // namespace

namespace gui {
namespace prop {

    //-------------------------------------------------------------------------------------------------
    ConstantPanel::ConstantPanel(ViaPoint& aViaPoint, core::Project& aProject, const QString& aTitle, QWidget* aParent):
        Panel(aTitle, aParent),
        mViaPoint(aViaPoint),
        mProject(aProject),
        mTarget(),
        mLabelWidth(),
        mRenderingAttributes(),
        mBlendMode(),
        mClipped() {
        mLabelWidth = this->fontMetrics().boundingRect(tr("MaxTextWidth :")).width();

        build();
        this->hide();
    }

    void ConstantPanel::setTarget(core::ObjectNode* aTarget) {
        mTarget = aTarget;

        if (mTarget) {
            this->setTitle(mTarget->name() + " : " + tr("Constants"));
            this->show();
        } else {
            this->hide();
        }

        updateAttribute();
    }

    void ConstantPanel::setPlayBackActivity(bool aIsActive) { this->setEnabled(!aIsActive); }

    void ConstantPanel::build() {
        using core::Constant;

        mRenderingAttributes = new AttrGroup(tr("Rendering"), mLabelWidth);
        {
            this->addGroup(mRenderingAttributes);

            // blend mode
            // grouped like Photoshop (darken / lighten / contrast / difference /
            // color), with the CSP-only modes next to their plain equivalents;
            // groups are split by thin separator lines. Each mode carries its
            // img::BlendMode value in Qt::UserRole so the row index does not
            // need to match the enum order.
            mBlendMode = new ComboItem(mRenderingAttributes);
            {
                auto& box = mBlendMode->box();
                box.setSizeAdjustPolicy(QComboBox::AdjustToContents);
                auto addMode = [&](img::BlendMode aMode) {
                    box.addItem(img::getBlendNameFromBlendMode(aMode), (int)aMode);
                };
                auto addSeparator = [&]() { box.insertSeparator(box.count()); };

                addMode(img::BlendMode_Normal);
                addSeparator();
                addMode(img::BlendMode_Darken);
                addMode(img::BlendMode_Multiply);
                addMode(img::BlendMode_ColorBurn);
                addMode(img::BlendMode_LinearBurn);
                addMode(img::BlendMode_DarkerColor);
                addSeparator();
                addMode(img::BlendMode_Lighten);
                addMode(img::BlendMode_Screen);
                addMode(img::BlendMode_ColorDodge);
                addMode(img::BlendMode_GlowDodge);
                addMode(img::BlendMode_LinearDodge);
                addMode(img::BlendMode_AddGlow);
                addMode(img::BlendMode_LighterColor);
                addSeparator();
                addMode(img::BlendMode_Overlay);
                addMode(img::BlendMode_SoftLight);
                addMode(img::BlendMode_HardLight);
                addMode(img::BlendMode_VividLight);
                addMode(img::BlendMode_LinearLight);
                addMode(img::BlendMode_PinLight);
                addMode(img::BlendMode_HardMix);
                addSeparator();
                addMode(img::BlendMode_Difference);
                addMode(img::BlendMode_Exclusion);
                addMode(img::BlendMode_Subtract);
                addMode(img::BlendMode_Divide);
                addSeparator();
                addMode(img::BlendMode_Hue);
                addMode(img::BlendMode_Saturation);
                addMode(img::BlendMode_Color);
                addMode(img::BlendMode_Luminosity);

                new BlendComboWidthGuard(&box);

                mBlendMode->onValueUpdated = [=](int, int aNext) {
                    auto mode = (img::BlendMode)mBlendMode->box().itemData(aNext).toInt();
                    assignBlendMode(this->mProject, this->mTarget, mode);
                };
            }
            mRenderingAttributes->addItem(tr("Blend :"), mBlendMode);

            // clipped
            mClipped = new CheckItem(mRenderingAttributes);
            mClipped->onValueUpdated = [=](bool aNext) { assignClipped(this->mProject, this->mTarget, aNext); };
            mRenderingAttributes->addItem(tr("Clipped :"), mClipped);
        }

        this->addStretch();
    }

    void ConstantPanel::updateAttribute() {
        if (mTarget) {
            if (mTarget->renderer()) {
                auto& renderer = *(mTarget->renderer());
                if (renderer.hasBlendMode()) {
                    mBlendMode->setItemEnabled(true);
                    auto mode = renderer.blendMode();
                    auto row = mBlendMode->box().findData((int)mode);
                    mBlendMode->setValue(row >= 0 ? row : 0, false);
                } else {
                    mBlendMode->setItemEnabled(false);
                }

                mClipped->setItemEnabled(true);
                mClipped->setValue(renderer.isClipped(), false);
            } else {
                mBlendMode->setItemEnabled(false);
                mClipped->setItemEnabled(false);
            }
        }
    }

    void ConstantPanel::assignBlendMode(core::Project& aProject, core::ObjectNode* aTarget, img::BlendMode aValue) {
        XC_ASSERT(aTarget);
        XC_PTR_ASSERT(aTarget->renderer());
        if (!aTarget || !aTarget->renderer())
            return; // fail-safe code

        auto prev = aTarget->renderer()->blendMode();
        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("update a blending mode"));
        macro.grabListener(new ObjectNodeAttrNotifier(aProject, *aTarget));

        auto exec = [=]() { aTarget->renderer()->setBlendMode(aValue); };
        auto undo = [=]() { aTarget->renderer()->setBlendMode(prev); };
        aProject.commandStack().push(new cmnd::Delegatable(exec, undo));
    }

    void ConstantPanel::assignClipped(core::Project& aProject, core::ObjectNode* aTarget, bool aValue) {
        XC_ASSERT(aTarget);
        XC_PTR_ASSERT(aTarget->renderer());
        if (!aTarget || !aTarget->renderer())
            return; // fail-safe code

        const bool prev = aTarget->renderer()->isClipped();
        cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("update a clippping flag"));
        macro.grabListener(new ObjectNodeAttrNotifier(aProject, *aTarget));

        auto exec = [=]() { aTarget->renderer()->setClipped(aValue); };
        auto undo = [=]() { aTarget->renderer()->setClipped(prev); };
        aProject.commandStack().push(new cmnd::Delegatable(exec, undo));
    }

} // namespace prop
} // namespace gui
