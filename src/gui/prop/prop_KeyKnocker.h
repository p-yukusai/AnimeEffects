#ifndef GUI_PROP_KEYKNOCKER_H
#define GUI_PROP_KEYKNOCKER_H

#include <functional>
#include <QWidget>

class QLabel;
class QToolButton;
class QHBoxLayout;

namespace gui {

class GUIResources;

namespace prop {

    // The add-key row. It swaps with its KeyGroup (visible when no key
    // exists), so it is built as the section header's stand-in: the same
    // full-row geometry, the text at the section-title column, and a
    // right-aligned plus as the add affordance (16px inner padding). The
    // whole row is the click target.
    class KeyKnocker: public QWidget {
    public:
        KeyKnocker(const QString& aLabel, GUIResources* aGUIResources);
        void set(const std::function<void()>& aKnocker);

    protected:
        bool eventFilter(QObject* aWatched, QEvent* aEvent) override;
        void enterEvent(QEnterEvent* aEvent) override;
        void leaveEvent(QEvent* aEvent) override;
        void mouseReleaseEvent(QMouseEvent* aEvent) override;

    private:
        QLabel* mLabel;
        QToolButton* mPlus;
        QHBoxLayout* mLayout;
        std::function<void()> mKnocker;
        QIcon mPlusIcon;     // bright plus (hover)
        QIcon mPlusDim;      // resting plus, caret-style dim
    };

} // namespace prop
} // namespace gui

#endif // GUI_PROP_KEYKNOCKER_H
