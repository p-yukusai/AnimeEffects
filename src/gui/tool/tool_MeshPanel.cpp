#include "gui/tool/tool_MeshPanel.h"
#include "gui/tool/tool_ItemTable.h"

namespace {
int kButtonSize = 24;
int kButtonSpace = kButtonSize;
} // namespace

namespace gui {
namespace tool {

    MeshPanel::MeshPanel(QWidget* aParent, GUIResources& aResources):
        QGroupBox(aParent), mResources(aResources), mParam(), mTypeGroup() {
        this->setTitle(tr("Mesh editor"));
        mResources.onThemeChanged.connect(this, &MeshPanel::onThemeUpdated);
        createMode();
    }

    void MeshPanel::applyIcons() {
        mTypeGroup->setIcons(
            QVector<QIcon>() << mResources.icon("plus") << mResources.icon("minus") << mResources.icon("pencil")
        , QSize(18, 18));
    }

    void MeshPanel::onThemeUpdated(theme::Theme&) {
        applyIcons();
    }

    void MeshPanel::createMode() {
        if (mResources.getTheme().contains("high_dpi")) {
            kButtonSize = 32;
            kButtonSpace = kButtonSize;
        }
        // type
        mTypeGroup.reset(new SingleOutItem(3, QSize(kButtonSpace, kButtonSpace), this));
        mTypeGroup->setChoice(mParam.mode);
        mTypeGroup->setToolTips(QStringList() << tr("Add vertex") << tr("Delete vertex") << tr("Split polygon"));
        applyIcons();
        mTypeGroup->connect([=](int aIndex) {
            this->mParam.mode = aIndex;
            this->onParamUpdated(true);
        });
    }

    int MeshPanel::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kItemLeft = 8;
        // content starts at the stylesheet's content top
        const int kItemTop = this->contentsMargins().top();

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        // type
        curPos.setY(mTypeGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);

        // myself: card height = content extent + bottom padding (see ViewPanel)
        const int b = this->contentsMargins().bottom();
        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y() + b);

        return aPos.y() + curPos.y() + b;
    }

} // namespace tool
} // namespace gui
