#include "gui/tool/tool_FFDPanel.h"
#include "gui/tool/tool_ItemTable.h"

namespace {
const int kButtonSize = 24;
const int kButtonSpace = kButtonSize;
} // namespace

namespace gui {
namespace tool {

    FFDPanel::FFDPanel(QWidget* aParent, GUIResources& aResources):
        QGroupBox(aParent),
        mResources(aResources),
        mParam(),
        mTypeGroup(),
        mBrush(),
        mBlur() {
        this->setTitle(tr("FFD"));
        mResources.onThemeChanged.connect(this, &FFDPanel::onThemeUpdated);
        createBrush();
        updateTypeParam(mParam.type);
    }

    void FFDPanel::applyIcons() {
        mTypeGroup->setIcons(
            QVector<QIcon>() << mResources.icon("navigation-arrow") << mResources.icon("pencil-simple") << mResources.icon("eraser")
        , QSize(18, 18));
    }

    void FFDPanel::onThemeUpdated(theme::Theme&) {
        applyIcons();
    }

    void FFDPanel::createBrush() {
        // type
        mTypeGroup.reset(new SingleOutItem(3, QSize(kButtonSpace, kButtonSpace), this));
        mTypeGroup->setChoice(mParam.type);
        mTypeGroup->setToolTips(QStringList() << tr("Move vertex") << tr("Deform mesh") << tr("Erase deformations"));
        mTypeGroup->connect([=](int aIndex) {
            this->mParam.type = (ctrl::FFDParam::Type)aIndex;
            this->updateTypeParam(this->mParam.type);
            this->onParamUpdated(true);
        });

        // the shared brush rows. Size is content px, hardness/flow are percentages,
        // so this tool's numbers mean what the mask brush's numbers mean.
        mBrush.reset(new BrushPanel(this, mParam.brush, BrushPanel::Row_Size | BrushPanel::Row_Hardness |
                                                         BrushPanel::Row_Flow | BrushPanel::Row_Pressure));
        mBrush->onParamUpdated.connect([=](bool) { this->onParamUpdated(false); });

        static const int kScale = 100;

        // blur (the pencil's smoothing pass; not part of the brush model)
        mBlur.reset(new SliderItem(tr("Blur"), this->palette(), this));
        mBlur->setAttribute(util::Range(0, kScale), mParam.blur * kScale, kScale / 10);
        mBlur->connectOnAny([=](int aValue) {
            this->mParam.blur = (float)aValue / kScale;
            this->onParamUpdated(false);
        });
    }

    void FFDPanel::updateTypeParam(ctrl::FFDParam::Type aType) {
        // Drag has no brush and no blur; both painting modes share ONE parameter
        // set now, so switching between them keeps the size and hardness.
        const bool painting = (aType == ctrl::FFDParam::Type_Pencil || aType == ctrl::FFDParam::Type_Eraser);
        mBrush->setVisible(painting);
        mBlur->setVisible(aType == ctrl::FFDParam::Type_Pencil);
    }

    int FFDPanel::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kItemLeft = 8;
        // content starts at the stylesheet's content top
        const int kItemTop = this->contentsMargins().top();

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        // type
        curPos.setY(mTypeGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);

        // the brush block answers an ABSOLUTE bottom edge, the blur rows answer an
        // extent: hence the bare gap on the first and `+ curPos.y()` on the second
        if (mParam.type == ctrl::FFDParam::Type_Pencil || mParam.type == ctrl::FFDParam::Type_Eraser) {
            curPos.setY(mBrush->updateGeometry(curPos, itemWidth) + 5);
        }
        if (mParam.type == ctrl::FFDParam::Type_Pencil) {
            curPos.setY(mBlur->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        }

        // myself: card height = content extent + bottom padding (see ViewPanel)
        const int b = this->contentsMargins().bottom();
        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y() + b);

        return aPos.y() + curPos.y() + b;
    }

} // namespace tool
} // namespace gui
