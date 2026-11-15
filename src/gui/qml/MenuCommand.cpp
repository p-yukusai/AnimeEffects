#include "gui/qml/MenuCommand.h"

namespace gui {

MenuCommand::MenuCommand(const QString& aText,
                         std::function<void()> aCallback,
                         bool aCheckable,
                         QObject* aParent):
    QObject(aParent),
    mText(aText),
    mCallback(std::move(aCallback)),
    mCheckable(aCheckable) {
}

void MenuCommand::setEnabled(bool aEnabled) {
    if (mEnabled == aEnabled) return;
    mEnabled = aEnabled;
    emit enabledChanged();
}

void MenuCommand::setChecked(bool aChecked) {
    if (!mCheckable || mChecked == aChecked) return;
    mChecked = aChecked;
    emit checkedChanged();
}

void MenuCommand::trigger() {
    if (!mEnabled)
        return;
    // The signal is emitted before the callback so a host that listens to the
    // command (the panel actions do; they own the docks) has run by the time the
    // caller re-reads `checked`. The callback is the same command for every menu row.
    emit triggered();
    if (mCallback)
        mCallback();
}

} // namespace gui
