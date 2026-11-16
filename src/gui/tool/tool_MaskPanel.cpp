#include <algorithm>
#include "gui/tool/tool_MaskPanel.h"
#include "gui/tool/tool_ItemTable.h"

namespace {
const int kButtonSize = 24;
const int kButtonSpace = kButtonSize;
// the lanes the mask tool can write, in the order the SingleOutItem shows them
const core::TimeKeyType kLanes[] = {core::TimeKeyType_HSV, core::TimeKeyType_Blur, core::TimeKeyType_Wave};
const int kLaneCount = 3;
// The FOURTH target is the node's own matte, which is not a filter lane: it lives in
// a core::MaskKey and its neutral is reveal-all. It sits in the same chooser because
// the brush, the value and the eyedropper are the same for both kinds.
const int kLayerMaskChoice = kLaneCount;
const int kTargetCount = kLaneCount + 1;
} // namespace

namespace gui {
namespace tool {

    MaskPanel::MaskPanel(QWidget* aParent, GUIResources& aResources):
        QGroupBox(aParent), mResources(aResources), mParam(), mLaneGroup(), mChannelGroup(), mValue(), mBrush(),
        mClear() {
        this->setTitle(tr("Filter mask"));   // see setLayerMask: the title follows the target
        mResources.onThemeChanged.connect(this, &MaskPanel::onThemeUpdated);
        createItems();
    }

    void MaskPanel::applyIcons() {
        // the lane row reuses the filter icons the stack already uses
        // one icon per TARGET, including the node's own matte: a choice with no glyph
        // reads as a disabled button (measured: the fourth slot was a bare rectangle).
        // `circle-half` is the alpha/mask glyph the channel row already uses, and that
        // row is hidden while the matte is the target, so the two cannot collide.
        mLaneGroup->setIcons(
            QVector<QIcon>() << mResources.icon("faders-horizontal") << mResources.icon("ffd")
                             << mResources.icon("paint-brush") << mResources.icon("circle-half"),
            QSize(18, 18)
        );
        mChannelGroup->setIcons(
            QVector<QIcon>() << mResources.icon("circle-half") << mResources.icon("arrow-clockwise"),
            QSize(18, 18)
        );

    }

    void MaskPanel::onThemeUpdated(theme::Theme&) { applyIcons(); }

    void MaskPanel::createItems() {
        // WHICH filter lane: the mask belongs to a lane, because that is the thing
        // the renderer consults (see core/TimeLine::filterMask)
        mLaneGroup.reset(new SingleOutItem(kTargetCount, QSize(kButtonSpace, kButtonSpace), this));
        mLaneGroup->setChoice(2); // Wave
        mLaneGroup->setToolTips(
            QStringList() << tr("Mask for HSV") << tr("Mask for blur")
                          << tr("Mask for wave deform") << tr("The layer's own mask")
        );
        mParam.lane = core::TimeKeyType_Wave;
        mLaneGroup->connect([=](int aIndex) {
            if (aIndex == kLayerMaskChoice) {
                this->mParam.setKind(ctrl::MaskTargetKind_LayerMask);
                this->setTitle(tr("Layer mask"));
            } else {
                this->mParam.setKind(ctrl::MaskTargetKind_FilterInput);
                this->mParam.lane = kLanes[aIndex];
                this->setTitle(tr("Filter mask"));
            }
            // a filter input has an amount/time choice; a matte does not
            this->mChannelGroup->setVisible(!this->mParam.isLayerMask());
            this->mValue->setAttribute(util::Range(0, 100), int(this->mParam.value * 100.0f), 10);
            this->onParamUpdated(true);
        });

        // WHICH input of that filter: the amount it scales, or the time it delays
        mChannelGroup.reset(new SingleOutItem(core::FilterMaskChannel_TERM, QSize(kButtonSpace, kButtonSpace), this));
        mChannelGroup->setChoice(int(mParam.channel));
        mChannelGroup->setToolTips(
            QStringList() << tr("How much the filter applies") << tr("How much later the filter applies")
        );
        mChannelGroup->connect([=](int aIndex) {
            this->mParam.setChannel((core::FilterMaskChannel)aIndex);
            this->mValue->setAttribute(util::Range(0, 100), int(this->mParam.value * 100.0f), 10);
            this->onParamUpdated(true);
        });

        // WHAT the brush paints: the field's value moves TOWARD this, so one slider
        // covers both reducing an effect (0) and restoring or adding it (1). The
        // temporary eyedropper (Alt + click) sets it from what is already painted.
        // "Value" is what the brush paints TOWARD; Alt + click on the canvas samples
        // it from the field (the temporary eyedropper).
        mValue.reset(new SliderItem(tr("Value"), this->palette(), this));
        mValue->setAttribute(util::Range(0, 100), int(mParam.value * 100.0f), 10);
        mValue->connectOnChanged([=](int aValue) {
            this->mParam.value = float(aValue) / 100.0f;
            this->onParamUpdated(false);
        });

        applyIcons();

        // the shared brush rows; Opacity is hidden because the mask has VALUE, not an
        // alpha to accumulate - a stamp converges on the value
        mBrush.reset(new BrushPanel(
            this, mParam.brush,
            BrushPanel::Row_Size | BrushPanel::Row_Hardness | BrushPanel::Row_Flow | BrushPanel::Row_Pressure
        ));
        mBrush->onParamUpdated.connect([=](bool aDirect) { this->onParamUpdated(aDirect); });

        mClear.reset(new QPushButton(tr("Clear mask"), this));
        mClear->setToolTip(tr("Clear the field being painted (one undo step)"));
        QObject::connect(mClear.data(), &QPushButton::clicked, this, [this]() { this->onClearRequested(); });
    }

    void MaskPanel::refresh() { mBrush->refresh(); }

    void MaskPanel::setLayerMask() {
        mParam.setKind(ctrl::MaskTargetKind_LayerMask);
        // the title says WHAT you are painting: a matte is not a filter's input, and a
        // panel headed "Filter mask" while editing a layer's visibility names the wrong
        // thing
        this->setTitle(tr("Layer mask"));
        mLaneGroup->setChoice(kLayerMaskChoice);
        mChannelGroup->setVisible(false);
        mValue->setAttribute(util::Range(0, 100), int(mParam.value * 100.0f), 10);
        onParamUpdated(true);
    }

    void MaskPanel::setLaneAndChannel(core::TimeKeyType aLane, core::FilterMaskChannel aChannel) {
        this->setTitle(tr("Filter mask"));
        mChannelGroup->setVisible(true);
        setLane(aLane);
        mParam.setChannel(aChannel);
        mChannelGroup->setChoice(int(aChannel));
        mValue->setAttribute(util::Range(0, 100), int(mParam.value * 100.0f), 10);
        onParamUpdated(true);
    }

    void MaskPanel::setValue(float aValue) {
        mParam.value = std::min(std::max(aValue, 0.0f), 1.0f);
        mValue->setAttribute(util::Range(0, 100), int(mParam.value * 100.0f), 10);
        onParamUpdated(false);
    }

    void MaskPanel::setLane(core::TimeKeyType aLane) {
        for (int i = 0; i < kLaneCount; ++i) {
            if (kLanes[i] == aLane) {
                mParam.lane = aLane;
                mLaneGroup->setChoice(i);
                onParamUpdated(false);
                return;
            }
        }
    }

    int MaskPanel::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kItemLeft = 8;
        const int kItemTop = this->contentsMargins().top();

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        curPos.setY(mLaneGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        curPos.setY(mChannelGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        curPos.setY(mValue->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        // mBrush is a BrushPanel, not a row item: it answers an ABSOLUTE bottom
        // edge, so it is chained with a bare gap and adding curPos.y() would
        // double-count it.
        curPos.setY(mBrush->updateGeometry(curPos, itemWidth) + 5);

        // Clear is the last row and the only hand-placed widget here, so its row
        // height comes from the height it is given below, never from a read-back
        // geometry: on the first pass that is QPushButton's default 100x30 and on
        // later passes the 24 we set, which shifted the card by 6px between passes
        // and left the first frame with a floating button. QSS gives this button no
        // height, so the constant is ours (kButtonSize, the same 24 as the rows
        // above).
        mClear->setGeometry(kItemLeft, curPos.y(), itemWidth, kButtonSize);
        curPos.setY(curPos.y() + kButtonSize + 5);

        const int b = this->contentsMargins().bottom();
        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y() + b);
        return aPos.y() + curPos.y() + b;
    }

} // namespace tool
} // namespace gui
