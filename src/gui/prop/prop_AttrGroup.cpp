#include <QLabel>
#include <QVBoxLayout>
#include <QCoreApplication>
#include "gui/prop/prop_AttrGroup.h"

namespace gui {
namespace prop {

    AttrGroup::AttrGroup(const QString& aTitle, int aLabelWidth, GUIResources* aGUIResources):
        QFrame(nullptr),
        mHeader(nullptr),
        mBody(nullptr),
        mLayout(nullptr),
        mLabelWidth(aLabelWidth),
        mLabels(),
        mItems() {
        this->setObjectName("attrGroup");
        this->setFrameShape(QFrame::NoFrame);
        this->setFocusPolicy(Qt::NoFocus);

        // The section header is a real button spanning the full row (see
        // HeaderButton); the body holds the fields and collapses by hiding,
        // so the group's height tracks the layout with no fixed-height
        // hacks.
        mHeader = new HeaderButton(aTitle, aGUIResources, "attrGroupHeader", this, 12);
        mBody = new QWidget(this);

        // Fields share one column and grow proportionally with the panel
        // (expanding-policy widgets fill the column; multi-field rows split
        // it via their layout stretch factors).
        mLayout = new QFormLayout(mBody);
        mLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        // mLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
        mLayout->setFormAlignment(Qt::AlignLeft);
        mLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        mLayout->setVerticalSpacing(2);
        mLayout->setHorizontalSpacing(10);
        // the right margin pads the field rows away from the dock edge
        // (the header spans edge to edge, so the padding lives on the
        // body); the bottom margin is the breathing below the last field
        // row (QSS padding on the group frame does not inset the layout)
        mLayout->setContentsMargins(0, 4, 16, 12);

        auto* vbox = new QVBoxLayout(this);
        vbox->setSpacing(0);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->addWidget(mHeader);
        vbox->addWidget(mBody);

        // collapsed = body hidden; toggled fires on user clicks and on
        // programmatic setChecked alike
        mHeader->setChecked(true);
        this->connect(mHeader, &QAbstractButton::toggled, this, [this](bool aChecked) {
            mBody->setVisible(aChecked);
            emit expansionChanged(aChecked);
            for (QWidget* w = this; w != nullptr; w = w->parentWidget()) {
                if (QLayout* l = w->layout()) {
                    l->activate();
                }
            }
            // the board's resize is driven by its posted LayoutRequest;
            // deliver it now, or a paint can slip in with the old board
            // height (the expand-under-scroll flicker)
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        });
    }

    AttrGroup::~AttrGroup() {
        for (auto item : mItems) {
            delete item;
        }
    }

    void AttrGroup::addItem(const QString& aLabel, ItemBase* aItem) {
        auto label = new QLabel(aLabel);
        label->setObjectName("attrLabel");
        label->setMinimumWidth(mLabelWidth);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        mLabels.push_back(label);
        mItems.push_back(aItem);

        if (aItem->itemLayout()) {
            mLayout->addRow(label, aItem->itemLayout());
        } else if (aItem->itemWidget()) {
            mLayout->addRow(label, aItem->itemWidget());
        }
    }

    void AttrGroup::resizeEvent(QResizeEvent* aEvent) {
        QFrame::resizeEvent(aEvent);

        // Single-field rows cap at 70% of the field column so compact
        // values don't stretch edge to edge; multi-field rows (layout items)
        // keep the full column and split it via their stretch factors.
        int labelColumn = 0;
        for (auto* label : mLabels) {
            labelColumn = qMax(labelColumn, label->sizeHint().width());
        }
        const int column = contentsRect().width() - labelColumn - mLayout->horizontalSpacing();
        const int cap = qMax(80, column * 70 / 100);
        for (auto* item : mItems) {
            if (auto* field = item->itemWidget()) {
                field->setMaximumWidth(cap);
            }
        }
    }

} // namespace prop
} // namespace gui
