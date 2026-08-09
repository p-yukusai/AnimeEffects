#include "gui/tool/tool_MeshPanel.h"
#include "gui/tool/tool_ItemTable.h"

namespace {
int kButtonSize = 20;
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
        , QSize(16, 16));
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
        static const int kItemTop = 26;

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        // type
        curPos.setY(mTypeGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);

        // myself
        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y());

        return aPos.y() + curPos.y();
    }

} // namespace tool
} // namespace gui
