#ifndef GUI_QML_MENUCOMMAND_H
#define GUI_QML_MENUCOMMAND_H

#include <QObject>
#include <QString>

#include <functional>

namespace gui {

// One command in the menu bar, as the QML `Action` objects see it.
//
// This is NOT a QAction and it does not draw anything. QML's own `Action` type is
// the menu's action object (it is what Menu/MenuItem are built around, and what the
// Control's delegate receives); this class is the application-side half: the
// translated words, the live enabled/checked state, and the callback that performs
// the command. A HeaderAction.qml instance binds its Action properties to this
// object and calls trigger(), so the scene and the window agree on one source of
// truth without a legacy command object in the menu.
//
// CHECKABLE COMMANDS DO NOT TOGGLE THEMSELVES. QML's Action already toggles its own
// checked state before it emits triggered; the command's `checked` is the
// AUTHORITATIVE state the window derives from the docks/project, and the scene
// re-reads it after the command returns (HeaderCheckableAction.qml). A command that
// flipped its own flag would be a second state that can disagree with the window.
class MenuCommand: public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text CONSTANT)
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool checkable READ isCheckable CONSTANT)
    Q_PROPERTY(bool checked READ isChecked NOTIFY checkedChanged)
public:
    MenuCommand(const QString& aText,
                std::function<void()> aCallback,
                bool aCheckable = false,
                QObject* aParent = nullptr);

    QString text() const { return mText; }
    bool isEnabled() const { return mEnabled; }
    bool isCheckable() const { return mCheckable; }
    bool isChecked() const { return mChecked; }

    void setEnabled(bool aEnabled);
    void setChecked(bool aChecked);

    // The one route a command runs by. An accessibility press, a menu row click and
    // the QML Action's own trigger all end here, so no route can bypass the enabled
    // check or the callback.
    Q_INVOKABLE void trigger();

signals:
    void enabledChanged();
    void checkedChanged();
    void triggered();

private:
    QString mText;
    std::function<void()> mCallback;
    bool mCheckable = false;
    bool mEnabled = true;
    bool mChecked = false;
};

} // namespace gui

#endif // GUI_QML_MENUCOMMAND_H
