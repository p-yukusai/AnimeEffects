#include "gui/prop/prop_Backboard.h"

#include <QFrame>
#include <QTimer>

namespace gui {
namespace prop {

    Backboard::Backboard(ViaPoint& aViaPoint, QWidget* aParent, GUIResources* aGUIResources):
        QWidget(aParent),
        mViaPoint(aViaPoint),
        mProject(),
        mLayout(),
        mGUIResources(aGUIResources),
        mConstantPanel(),
        mDefaultKeyPanel(),
        mCurrentKeyPanel() {
        // the dock's single flat surface (see propertywidget.ssa)
        this->setObjectName("backboard");
        resetLayout();
    }

    void Backboard::resetLayout() {
        if (mLayout)
            delete mLayout;
        mLayout = new QVBoxLayout();
        mLayout->setSpacing(1);
        // edge to edge, with 5px of dock-top breathing on top of the
        // panels' 3px top padding (the internal hairline gaps stay 4px);
        // contentsMargins order is left, top, right, bottom
        mLayout->setContentsMargins(0, 5, 0, 0);
        this->setLayout(mLayout);
    }

    bool Backboard::event(QEvent* aEvent) {
        if (aEvent->type() == QEvent::LayoutRequest) {
            // The scroll area (PropertyWidget) sizes this widget to its
            // sizeHint only once; later content changes (group expand/
            // collapse, rows rebuilt) would otherwise leave the board too
            // short and compress the field rows below their sizeHint.
            // Resize synchronously: the old deferred-timer version kept the
            // old height for one paint, clipping expanded content for a
            // frame (collapse only left empty space, so it looked fine).
            const int natural = qMax(sizeHint().height(), parentWidget() ? parentWidget()->height() : 0);
            if (height() != natural) {
                resize(width(), natural);
            }
        }
        return QWidget::event(aEvent);
    }

    void Backboard::setProject(core::Project* aProject) {
        mConstantPanel.reset();
        mDefaultKeyPanel.reset();
        mCurrentKeyPanel.reset();
        mSeparator0.reset();
        mSeparator1.reset();

        resetLayout();

        mProject = aProject;

        if (mProject) {
            // hairline between the major sections (not above the first one)
            auto addSeparator = [this]() {
                auto* sep = new QFrame(this);
                sep->setObjectName("separator");
                sep->setFixedHeight(1);
                // hidden until a layer is selected (setTarget shows them)
                sep->setVisible(false);
                mLayout->addWidget(sep);
                mLayout->setAlignment(sep, Qt::AlignTop);
                return sep;
            };

            mConstantPanel.reset(new ConstantPanel(mViaPoint, *mProject, this, mGUIResources));
            mLayout->addWidget(mConstantPanel.data());
            mLayout->setAlignment(mConstantPanel.data(), Qt::AlignTop);

            mSeparator0.reset(addSeparator());

            mDefaultKeyPanel.reset(new DefaultKeyPanel(mViaPoint, *mProject, this, mGUIResources));
            mLayout->addWidget(mDefaultKeyPanel.data());
            mLayout->setAlignment(mDefaultKeyPanel.data(), Qt::AlignTop);

            mSeparator1.reset(addSeparator());

            mCurrentKeyPanel.reset(new CurrentKeyPanel(mViaPoint, *mProject, this, mGUIResources));
            mLayout->addWidget(mCurrentKeyPanel.data());
            mLayout->setAlignment(mCurrentKeyPanel.data(), Qt::AlignTop);
        }
        mLayout->addStretch();
    }

    void Backboard::setTarget(core::ObjectNode* aNode) {
        if (mConstantPanel) {
            mConstantPanel->setTarget(aNode);
        }
        if (mDefaultKeyPanel) {
            mDefaultKeyPanel->setTarget(aNode);
        }
        if (mCurrentKeyPanel) {
            mCurrentKeyPanel->setTarget(aNode);
        }
        // the panels hide themselves without a target; the separators must
        // follow, or two hairlines float on the empty board
        if (mSeparator0) {
            mSeparator0->setVisible(aNode != nullptr);
        }
        if (mSeparator1) {
            mSeparator1->setVisible(aNode != nullptr);
        }
    }

    void Backboard::setPlayBackActivity(bool aIsActive) {
        this->setEnabled(!aIsActive);

        if (mConstantPanel) {
            mConstantPanel->setPlayBackActivity(aIsActive);
        }
        if (mDefaultKeyPanel) {
            mDefaultKeyPanel->setPlayBackActivity(aIsActive);
        }
        if (mCurrentKeyPanel) {
            mCurrentKeyPanel->setPlayBackActivity(aIsActive);
        }
    }

    void Backboard::updateAttribute() {
        if (mConstantPanel) {
            mConstantPanel->updateAttribute();
        }
    }

    void Backboard::updateKey(bool aUpdateKey, bool aUpdateDefaultKey) {
        if (aUpdateDefaultKey && mDefaultKeyPanel) {
            mDefaultKeyPanel->updateKey();
        }
        if (aUpdateKey && mCurrentKeyPanel) {
            mCurrentKeyPanel->updateKey();
        }
    }

    void Backboard::updateFrame() {
        if (mCurrentKeyPanel) {
            mCurrentKeyPanel->updateFrame();
        }
    }

} // namespace prop
} // namespace gui
