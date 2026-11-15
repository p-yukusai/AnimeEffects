#ifndef GUI_DESTINATIONPICKER_H
#define GUI_DESTINATIONPICKER_H

#include <QDialog>
#include <QList>
#include <QString>
#include "ctrl/ExportTarget.h"

class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace gui {

class GUIResources;

// The destination chooser: a search field and a TREE of platforms, in a modal.
//
// WHY A MODAL AND NOT A MENU. There are fifty-five destinations. As a menu that is
// four levels deep (File -> Quickly export as -> a platform -> the destination), and
// at any level the full list is forty rows that wrap into two columns and run off
// the bottom of the screen. A menu cannot be typed into, and typing is exactly what
// makes fifty-five destinations navigable: three letters of "telegram", or "sticker"
// to see every sticker destination, is faster than reading any grouping.
//
// THE PLATFORMS ARE CLOSED. The first level is the platform ("I am posting to
// Instagram"), which is the one thing a person knows before they start; its
// destinations are one level down, and a platform is COLLAPSED until you open it.
// Grouping that shows every destination at once is not a hierarchy, it is the same
// flat list with bold lines in it.
//
// TWO LEVELS EXACTLY WHEN THERE IS SOMETHING TO CHOOSE BETWEEN:
//  - a platform with more than one destination (Telegram: a sticker, a custom emoji,
//    a video) is a node you open;
//  - a platform with one destination is that destination, listed directly, because a
//    section holding a single line costs a click and asks nothing;
//  - a group whose rows are NOT variants of one platform ("general": a video for a
//    chat app, a website, just a file, a master copy) is listed directly too, which
//    is also what keeps the safe default one Enter away on a fresh install.
// Which groups nest is DATA (see `variants` in the <groups> block of the table).
//
// WHAT ENTER DOES, in one sentence: on a platform it opens or closes it (never a
// guess about which destination was meant), and on a destination it is the answer.
// Searching selects a destination, never a platform, so "telegram" then Enter is two
// keystrokes and no reading.
//
// The escape hatch for a platform the tree does not know is a button, not a
// destination, so it cannot be filtered away.
class DestinationPicker : public QDialog {
    // Without this the dialog has no metaobject of its own, tr() resolves through
    // QDialog, and every string here is looked up in the "QDialog" context: the
    // catalog's gui::DestinationPicker entries are read by nobody (measured: the
    // whole dialog stayed English with six catalogs loaded).
    Q_OBJECT
public:
    // aCurrentId preselects a destination (empty = none). The section holding it is
    // opened, so the preselection is on screen and not merely current.
    //
    // The two constructors differ only in where the rows come from. The rows and
    // their sections are the SAME data file, so the convenience overload is the one
    // the application uses: a call site that loaded the rows from one path and the
    // sections from another could only ever be a mistake.
    DestinationPicker(GUIResources& aResources, const QString& aCurrentId, QWidget* aParent);
    DestinationPicker(const QList<ctrl::ExportTarget>& aTargets,
                      const QList<ctrl::ExportGroup>& aGroups, GUIResources& aResources,
                      const QString& aCurrentId, QWidget* aParent);

    // The chosen destination id, or empty for "my platform isn't listed".
    QString chosenId() const { return mChosenId; }

    // Destinations currently on screen (a platform node is not one, and a hidden row
    // is not on screen), for tests and probes.
    int visibleRowCount() const;

private:
    void applyFilter(const QString& aQuery);
    void chooseCurrent();
    // Open or close a platform node. The one thing a platform can do, whatever route
    // asked: Enter, a click, or the dialog's own default button.
    void toggleSection(QTreeWidgetItem* aSection);

    bool eventFilter(QObject* aWatched, QEvent* aEvent) override;

    QLineEdit* mSearch = nullptr;
    QTreeWidget* mTree = nullptr;
    QPushButton* mManualButton = nullptr;
    QString mChosenId;
};

} // namespace gui

#endif // GUI_DESTINATIONPICKER_H
