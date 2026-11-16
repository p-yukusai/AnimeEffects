#ifndef GUI_KEYCOMMANDMAP_H
#define GUI_KEYCOMMANDMAP_H

#include <functional>
#include <QMap>
#include <QList>
#include <QShortcut>
#include <QSettings>
#include "ctrl/KeyBinding.h"

namespace gui {

class KeyCommandMap: public QObject {
    Q_OBJECT
public:
    struct KeyCommand {
        KeyCommand();
        KeyCommand(const QString& aKey, const QString& aGroup, const QString& aLabel, const ctrl::KeyBinding& aBinding);
        QString key;
        QString group;
        QString label;
        ctrl::KeyBinding binding;
        // The binding the command ships with, before any user override is
        // loaded. Kept so a "reset to defaults" can restore it live (and
        // persist it on save) instead of requiring an app restart.
        ctrl::KeyBinding defaultBinding;
        // Listeners fire on the binding's press/release, in registration
        // order. Several widgets legitimately react to one key (MoveCanvas
        // pans the viewport AND the timeline), so a command holds a list —
        // a single invoker slot would silently drop the second listener.
        QList<std::function<void()>> invokers;
        QList<std::function<void()>> releasers;
        void addListener(std::function<void()> aInvoker, std::function<void()> aReleaser = {}) {
            invokers.push_back(aInvoker);
            if (aReleaser)
                releasers.push_back(aReleaser);
        }
    };

    typedef QMap<QString, KeyCommand*> MapType;
    typedef QList<KeyCommand*> ListType;

    KeyCommandMap(QWidget& aParent);
    ~KeyCommandMap();

    void readFrom(const QSettings& aSrc);
    void writeTo(QSettings& aDest);

    KeyCommand* get(const QString& aIdentifier) { return mSearchMap[aIdentifier]; }
    const KeyCommand* get(const QString& aIdentifier) const { return mSearchMap[aIdentifier]; }

    const ListType& commands() const { return mCommands; }
    const ListType& subKeyCommands() const { return mSubKeyCommands; }

private:
    void addNewKey(const QString& aKey, const QString& aGroup, const QString& aName, const ctrl::KeyBinding& aBinding);
    void readValue(const QSettings& aSrc, KeyCommand& aCommand);
    void resetSubKeyCommands();

    ListType mCommands;
    ListType mSubKeyCommands;
    MapType mSearchMap;
    QWidget& mParent;
};

} // namespace gui

#endif // GUI_KEYCOMMANDMAP_H
