#include <cmath>
#include "gui/tool/tool_BrushPanel.h"

namespace {
// the slider range for a 0..1 value shown as a percentage
const int kPercentMax = 100;
const int kItemGap = 5;
} // namespace

namespace gui {
namespace tool {

    BrushPanel::BrushPanel(QWidget* aParent, ctrl::BrushParam& aParam, int aRows):
        QWidget(aParent), mParam(aParam), mSize(), mHardness(), mFlow(), mOpacity(), mPressureSize(),
        mPressureFlow(), mRows(aRows) {
        createItems(aRows);
    }

    void BrushPanel::createItems(int aRows) {
        const QPalette& palette = this->palette();

        if (aRows & Row_Size) {
            mSize.reset(new SliderItem(tr("Size"), palette, this));
            mSize->setAttribute(util::Range(1, 1000), int(mParam.radius), 10);
            mSize->connectOnChanged([this](int aValue) {
                mParam.radius = float(aValue);
                this->onParamUpdated(true);
            });
        }
        if (aRows & Row_Hardness) {
            mHardness.reset(new SliderItem(tr("Hardness"), palette, this));
            mHardness->setAttribute(util::Range(0, kPercentMax), int(mParam.hardness * kPercentMax), 10);
            mHardness->connectOnChanged([this](int aValue) {
                mParam.hardness = float(aValue) / kPercentMax;
                this->onParamUpdated(true);
            });
        }
        if (aRows & Row_Flow) {
            mFlow.reset(new SliderItem(tr("Flow"), palette, this));
            mFlow->setAttribute(util::Range(0, kPercentMax), int(mParam.flow * kPercentMax), 10);
            mFlow->connectOnChanged([this](int aValue) {
                mParam.flow = float(aValue) / kPercentMax;
                this->onParamUpdated(true);
            });
        }
        if (aRows & Row_Opacity) {
            mOpacity.reset(new SliderItem(tr("Opacity"), palette, this));
            mOpacity->setAttribute(util::Range(1, kPercentMax), int(mParam.opacity * kPercentMax), 10);
            mOpacity->connectOnChanged([this](int aValue) {
                mParam.opacity = float(aValue) / kPercentMax;
                this->onParamUpdated(true);
            });
        }
        if (aRows & Row_Pressure) {
            mPressureSize.reset(new CheckBoxItem(tr("Pressure size"), this));
            mPressureSize->setToolTip(tr("A tablet pen's pressure scales the brush size"));
            mPressureSize->setChecked(mParam.pressureSize);
            mPressureSize->connect([this](bool aChecked) {
                mParam.pressureSize = aChecked;
                this->onParamUpdated(true);
            });
            mPressureFlow.reset(new CheckBoxItem(tr("Pressure flow"), this));
            mPressureFlow->setToolTip(tr("A tablet pen's pressure scales what a stamp deposits"));
            mPressureFlow->setChecked(mParam.pressureFlow);
            mPressureFlow->connect([this](bool aChecked) {
                mParam.pressureFlow = aChecked;
                this->onParamUpdated(true);
            });
        }
    }

    void BrushPanel::refresh() {
        // the panel writes the param, so this only resyncs the widgets after an
        // outside change (a keyboard size change, a preset)
        if (mSize)
            mSize->setAttribute(util::Range(1, 1000), int(mParam.radius), 10);
        if (mHardness)
            mHardness->setAttribute(util::Range(0, kPercentMax), int(mParam.hardness * kPercentMax), 10);
        if (mFlow)
            mFlow->setAttribute(util::Range(0, kPercentMax), int(mParam.flow * kPercentMax), 10);
        if (mOpacity)
            mOpacity->setAttribute(util::Range(1, kPercentMax), int(mParam.opacity * kPercentMax), 10);
        if (mPressureSize)
            mPressureSize->setChecked(mParam.pressureSize);
        if (mPressureFlow)
            mPressureFlow->setChecked(mParam.pressureFlow);
    }

    void BrushPanel::adjustSize(int aSteps) {
        if (!mSize || aSteps == 0)
            return;
        mParam.radius = std::min(std::max(mParam.radius * std::pow(1.1f, float(aSteps)), 1.0f), 1000.0f);
        mSize->setAttribute(util::Range(1, 1000), int(mParam.radius), 10);
        onParamUpdated(true);
    }

    void BrushPanel::adjustHardness(int aSteps) {
        if (!mHardness || aSteps == 0)
            return;
        mParam.hardness = std::min(std::max(mParam.hardness + 0.05f * float(aSteps), 0.0f), 1.0f);
        mHardness->setAttribute(util::Range(0, kPercentMax), int(mParam.hardness * kPercentMax), 10);
        onParamUpdated(true);
    }

    int BrushPanel::updateGeometry(const QPoint& aPos, int aWidth) {
        // The CALLER has already inset the column (every tool panel passes
        // (kItemLeft, y) with width-kItemLeft*2), so this widget lays its rows out
        // from its own origin - and, like every other panel here, it must SET ITS OWN
        // geometry: without that the rows are clipped to a default tiny widget, which
        // is what a malformed "three small boxes" panel was.
        const int itemWidth = aWidth;
        QPoint curPos(0, 0);

        // Two different conventions meet here, so they are named:
        // a ROW ITEM answers the EXTENT of its row (SliderItem: label+track, 32px;
        // CheckBoxItem: its sizeHint), because it lays its children out from the
        // aPos it was handed and never knows where that is in the column. The
        // PANEL therefore adds its own offset itself - `+ curPos.y()` - and only
        // then moves down by the gap; without it every row would land at roughly
        // the previous row's own height and the block would report ~22px for
        // ~190px of content, which is what stacked FFD/Filter-mask rows on top of
        // each other.
        // The value this function RETURNS is the opposite: an ABSOLUTE bottom edge
        // in the caller's coordinates, which is why the callers chain it with a
        // bare `+ 5` and must not add their own y again.
        const auto placeSlider = [&](QScopedPointer<SliderItem>& aItem) {
            if (!aItem)
                return;
            curPos.setY(aItem->updateGeometry(curPos, itemWidth) + curPos.y() + kItemGap);
        };
        const auto placeCheck = [&](QScopedPointer<CheckBoxItem>& aItem) {
            if (!aItem)
                return;
            curPos.setY(aItem->updateGeometry(curPos, itemWidth) + curPos.y() + kItemGap);
        };

        placeSlider(mSize);
        placeSlider(mHardness);
        placeSlider(mFlow);
        placeSlider(mOpacity);
        placeCheck(mPressureSize);
        placeCheck(mPressureFlow);

        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y());

        // An ABSOLUTE bottom edge (this widget's own top plus its content), so the
        // callers chain it with a bare `+ 5`, like every other panel here. The row
        // items above answer extents, not edges - see the note there.
        return aPos.y() + curPos.y();
    }

} // namespace tool
} // namespace gui
