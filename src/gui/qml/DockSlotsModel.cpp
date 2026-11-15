#include "gui/qml/DockSlotsModel.h"

#include <QVariant>

namespace gui {

//-------------------------------------------------------------------------------------------------
DockSlotItem::DockSlotItem(Qt::DockWidgetArea aArea,
                           const QString& aGlyph,
                           const QString& aAccessibleName,
                           QObject* aParent):
    QObject(aParent),
    mArea(aArea),
    mGlyph(aGlyph),
    mAccessibleName(aAccessibleName) {
}

void DockSlotItem::setAccessibleName(const QString& aName) {
    if (mAccessibleName == aName) return;
    mAccessibleName = aName;
    emit changed();
}

void DockSlotItem::setTip(const QString& aTip) {
    if (mTip == aTip) return;
    mTip = aTip;
    emit changed();
}

void DockSlotItem::setChecked(bool aChecked) {
    if (mChecked == aChecked) return;
    mChecked = aChecked;
    emit changed();
}

void DockSlotItem::setAvailable(bool aAvailable) {
    if (mAvailable == aAvailable) return;
    mAvailable = aAvailable;
    emit changed();
}

//-------------------------------------------------------------------------------------------------
DockSlotsModel::DockSlotsModel(QObject* aParent):
    QObject(aParent) {
}

void DockSlotsModel::setSlots(const QVector<DockSlotSpec>& aSlots) {
    qDeleteAll(mSlots);
    mSlots.clear();
    mSlots.reserve(aSlots.size());
    for (const DockSlotSpec& spec : aSlots) {
        auto* item = new DockSlotItem(spec.area, spec.glyph, spec.accessibleName, this);
        item->setAvailable(false);
        item->setChecked(false);
        mSlots.push_back(item);
    }
    emit togglesChanged();
}

void DockSlotsModel::setSlotState(Qt::DockWidgetArea aArea,
                                  bool aChecked,
                                  bool aAvailable,
                                  const QString& aAccessibleName,
                                  const QString& aTip) {
    for (DockSlotItem* item : mSlots) {
        if (item->area() != aArea) continue;
        item->setAccessibleName(aAccessibleName);
        item->setTip(aTip);
        // Checked before available is not important; each setter is a value comparison
        // with its own notification, so an unchanged layout costs no QML work.
        item->setChecked(aChecked);
        item->setAvailable(aAvailable);
        return;
    }
}

QVariantList DockSlotsModel::toggles() const {
    QVariantList list;
    list.reserve(mSlots.size());
    for (DockSlotItem* item : mSlots)
        list.push_back(QVariant::fromValue(static_cast<QObject*>(item)));
    return list;
}

void DockSlotsModel::toggle(int aIndex) {
    if (aIndex < 0 || aIndex >= mSlots.size()) return;
    emit toggleRequested(mSlots.at(aIndex)->area());
}

} // namespace gui
