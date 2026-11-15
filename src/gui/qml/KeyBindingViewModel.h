#ifndef GUI_QML_KEYBINDINGVIEWMODEL_H
#define GUI_QML_KEYBINDINGVIEWMODEL_H

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "ctrl/KeyBinding.h"

namespace gui {

// The key-bindings form, widget-free: one row per key command, and the rules a key
// press applies to one of them.
//
// This is the port of the tree gui/KeyBindingDialog.cpp used to build (a QTabWidget
// of QFormLayouts, one KeyEdit — a guarded QLineEdit — per command). The rules are
// the SAME lines, moved off the QLineEdit subclass and onto the rows:
//
//   * Delete or Backspace clears the chord;
//   * a valid, modifier-free key after an existing chord with no sub-key becomes
//     that chord's SUB-KEY (two keys together fire one command);
//   * otherwise a valid key REPLACES the chord;
//   * a focus loss drops the sub-key permission, so the next key replaces instead
//     of extending a chord the user has left alone;
//   * an auto-repeating key is ignored (holding a key must not walk the chord);
//   * two rows whose chords conflict are BOTH labelled with the other's group and
//     label, so neither is a silent winner.
//
// The model is deliberately widget-free and Qt6::Core only. It must NOT include
// gui/KeyCommandMap.h: that header pulls in <QShortcut> (QtWidgets) and a QObject
// base, which would make the headless test that constructs this class link the GUI.
// The shell snapshots the map into these plain rows and writes them back on accept;
// nothing here names the map. ctrl/KeyBinding.h is the one project type it does use,
// and ctrl is where the chord's own text()/conflictsWith() live.
//
// It is set as the QML context property "keyBindingModel", NOT "model": a Repeater
// and a ListView both carry a `model` property of their own, so a scene binding would
// resolve the right-hand side to that shadowing property (exportModel's own note).
class KeyBindingViewModel : public QAbstractListModel {
    Q_OBJECT

    // The tab titles, in the order their commands were added. The scene shows every
    // row in ONE list and collapses the ones whose group is not the current tab, so
    // it needs the titles plus the current one, not a list per group.
    Q_PROPERTY(QStringList groups READ groups NOTIFY groupsChanged)
    // The largest group's row count. The scene HUGS its content (implicitHeight is
    // the tab row plus this many rows plus the footer) and the shell sizes the
    // window to that, so "how tall is the tallest tab" has ONE source here rather
    // than a guess in the QML.
    Q_PROPERTY(int maxGroupRows READ maxGroupRows NOTIFY groupsChanged)

public:
    enum Roles {
        GroupRole = Qt::UserRole + 1,
        LabelRole,
        TextRole,
        ConflictRole,
    };

    explicit KeyBindingViewModel(QObject* aParent = nullptr);

    int rowCount(const QModelIndex& aParent = QModelIndex()) const override;
    QVariant data(const QModelIndex& aIndex, int aRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList groups() const { return mGroups; }
    int maxGroupRows() const { return mMaxGroupRows; }
    int count() const { return mRows.size(); }

    // Seeding. The shell reads these off KeyCommandMap::commands() — key, group,
    // label, the current chord and the shipped default — and hands over plain values,
    // so the model never sees the map (or its QWidget parent).
    void addRow(
        const QString& aKey, const QString& aGroup, const QString& aLabel, const ctrl::KeyBinding& aBinding,
        const ctrl::KeyBinding& aDefaultBinding
    );

    // Read-back on accept: the shell writes these into the map, then persists.
    QString keyAt(int aRow) const;
    ctrl::KeyBinding bindingAt(int aRow) const;
    // The chord a resetAll() restores — also what the map's writeTo() compares
    // against to decide whether an entry is a user override at all.
    ctrl::KeyBinding defaultBindingAt(int aRow) const;

    // Read accessors, the same values the delegate sees through the roles.
    QString groupAt(int aRow) const;
    QString labelAt(int aRow) const;
    QString textAt(int aRow) const;
    QString conflictAt(int aRow) const;
    int rowForLabel(const QString& aLabel) const;

    // The scene's verbs. press() is one key event applied to one row; focusOut()
    // drops the sub-key permission when the row's field loses focus; resetAll()
    // restores every default; accept()/cancel() are the footer.
    Q_INVOKABLE void press(int aRow, int aKey, int aModifiers, bool aAutoRepeat);
    Q_INVOKABLE void focusOut(int aRow);
    Q_INVOKABLE void resetAll();
    // "Reset keybinds" ASKS rather than acts: the confirmation is a QMessageBox, a
    // WIDGET dialog the shell owns (the same split NewProjectDialog uses for its file
    // chooser). The scene cannot show it and must not pretend to.
    Q_INVOKABLE void requestReset();
    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();

signals:
    void groupsChanged();
    void resetRequested();
    void acceptRequested();
    void cancelRequested();

private:
    struct Row {
        QString key;
        QString group;
        QString label;
        ctrl::KeyBinding binding;
        ctrl::KeyBinding defaultBinding;
        QString text;
        QString conflict;
        // Whether the NEXT valid, modifier-free key extends this chord with a sub-key
        // instead of replacing it. Set when a chord is replaced, cleared by a focus
        // loss (the widget KeyEdit::mAllowsSubKey, verbatim).
        bool allowsSubKey = false;
    };

    // Recompute every row's chord text and conflict label from the bindings. The
    // conflict rule is the widget one: for each row the FIRST EARLIER row it conflicts
    // with labels both, and that scan stops at the first hit — so a row labels at most
    // one other, and the later of two identical chords wins the pair.
    void refreshTexts();
    void refreshAll();
    void recomputeGroups();
    bool validRow(int aRow) const { return 0 <= aRow && aRow < mRows.size(); }

    QList<Row> mRows;
    QStringList mGroups;
    int mMaxGroupRows = 0;
};

} // namespace gui

#endif // GUI_QML_KEYBINDINGVIEWMODEL_H
