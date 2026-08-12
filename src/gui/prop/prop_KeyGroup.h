#ifndef GUI_PROP_KEYGROUP_H
#define GUI_PROP_KEYGROUP_H

#include <QFrame>
#include <QFormLayout>
#include "gui/prop/prop_HeaderButton.h"
#include "gui/prop/prop_ItemBase.h"

namespace gui {

class GUIResources;

namespace prop {

    class KeyGroup: public QFrame {
        Q_OBJECT
    public:
        KeyGroup(const QString& aTitle, int aLabelWidth, GUIResources* aGUIResources);
        virtual ~KeyGroup();
        void addItem(const QString& aLabel, ItemBase* aItem);
        // shows/hides one row (label + item); the choice persists across collapse/expand
        void setItemVisible(ItemBase* aItem, bool aVisible);
        void makeSureExpand();

    signals:
        // emitted when the section header toggles (parents use it to keep
        // their own geometry in sync)
        void expansionChanged(bool aExpanded);

    protected:
        void resizeEvent(QResizeEvent* aEvent) override;

    private:
        void setExpansion(bool aChecked);

        HeaderButton* mHeader;
        QWidget* mBody;
        QVector<QWidget*> mLabels;
        QVector<ItemBase*> mItems;
        QVector<bool> mItemVisibles;
        QFormLayout* mLayout;
        int mLabelWidth;
    };

} // namespace prop
} // namespace gui

#endif // GUI_PROP_KEYGROUP_H
