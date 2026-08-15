#ifndef GUI_PROP_ATTRGROUP_H
#define GUI_PROP_ATTRGROUP_H

#include <QFrame>
#include <QFormLayout>
#include "gui/prop/prop_HeaderButton.h"
#include "gui/prop/prop_ItemBase.h"

namespace gui {

class GUIResources;

namespace prop {

    class AttrGroup: public QFrame {
        Q_OBJECT
    public:
        AttrGroup(const QString& aTitle, int aLabelWidth, GUIResources* aGUIResources);
        virtual ~AttrGroup();
        void addItem(const QString& aLabel, ItemBase* aItem);

    signals:
        // emitted when the section header toggles (parents use it to keep
        // their own geometry in sync)
        void expansionChanged(bool aExpanded);

    protected:
        void resizeEvent(QResizeEvent* aEvent) override;

    private:
        HeaderButton* mHeader;
        QWidget* mBody;
        QFormLayout* mLayout;
        int mLabelWidth;
        QVector<QWidget*> mLabels;
        QVector<ItemBase*> mItems;
    };

} // namespace prop
} // namespace gui

#endif // GUI_PROP_ATTRGROUP_H
