#include "gui/qml/KeyBindingViewModel.h"

namespace gui {

KeyBindingViewModel::KeyBindingViewModel(QObject* aParent) : QAbstractListModel(aParent) {}

int KeyBindingViewModel::rowCount(const QModelIndex& aParent) const {
    // A list model has no children, so only the root has rows.
    return aParent.isValid() ? 0 : mRows.size();
}

QVariant KeyBindingViewModel::data(const QModelIndex& aIndex, int aRole) const {
    if (!aIndex.isValid() || !validRow(aIndex.row()))
        return QVariant();

    const Row& row = mRows[aIndex.row()];
    switch (aRole) {
    case GroupRole:
        return row.group;
    case LabelRole:
        return row.label;
    case TextRole:
        return row.text;
    case ConflictRole:
        return row.conflict;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> KeyBindingViewModel::roleNames() const {
    return {
        {GroupRole, QByteArrayLiteral("group")},
        {LabelRole, QByteArrayLiteral("label")},
        {TextRole, QByteArrayLiteral("text")},
        {ConflictRole, QByteArrayLiteral("conflict")},
    };
}

void KeyBindingViewModel::addRow(
    const QString& aKey, const QString& aGroup, const QString& aLabel, const ctrl::KeyBinding& aBinding,
    const ctrl::KeyBinding& aDefaultBinding
) {
    const int at = mRows.size();
    beginInsertRows(QModelIndex(), at, at);
    Row row;
    row.key = aKey;
    row.group = aGroup;
    row.label = aLabel;
    row.binding = aBinding;
    row.defaultBinding = aDefaultBinding;
    mRows.push_back(row);
    endInsertRows();

    recomputeGroups();
    // The new row's text is read fresh when its delegate is built, but an earlier
    // row it conflicts with changes underneath its own delegate, so say so.
    refreshTexts();
    emit dataChanged(index(0), index(at), QList<int>{TextRole, ConflictRole});
}

QString KeyBindingViewModel::keyAt(int aRow) const {
    return validRow(aRow) ? mRows[aRow].key : QString();
}

ctrl::KeyBinding KeyBindingViewModel::bindingAt(int aRow) const {
    return validRow(aRow) ? mRows[aRow].binding : ctrl::KeyBinding();
}

ctrl::KeyBinding KeyBindingViewModel::defaultBindingAt(int aRow) const {
    return validRow(aRow) ? mRows[aRow].defaultBinding : ctrl::KeyBinding();
}

QString KeyBindingViewModel::groupAt(int aRow) const {
    return validRow(aRow) ? mRows[aRow].group : QString();
}

QString KeyBindingViewModel::labelAt(int aRow) const {
    return validRow(aRow) ? mRows[aRow].label : QString();
}

QString KeyBindingViewModel::textAt(int aRow) const {
    return validRow(aRow) ? mRows[aRow].text : QString();
}

QString KeyBindingViewModel::conflictAt(int aRow) const {
    return validRow(aRow) ? mRows[aRow].conflict : QString();
}

int KeyBindingViewModel::rowForLabel(const QString& aLabel) const {
    for (int i = 0; i < mRows.size(); ++i) {
        if (mRows[i].label == aLabel)
            return i;
    }
    return -1;
}

void KeyBindingViewModel::press(int aRow, int aKey, int aModifiers, bool aAutoRepeat) {
    // Holding a key repeats it; a chord is not built by leaning on the keyboard.
    if (!validRow(aRow) || aAutoRepeat)
        return;

    Row& row = mRows[aRow];
    const ctrl::KeyBinding pressed(aKey, Qt::KeyboardModifiers(aModifiers));

    if (aKey == Qt::Key_Delete || aKey == Qt::Key_Backspace) {
        // The clear key is a key EVERY command can reach, not a chord: it is handled
        // before validity so a row with no binding can still be cleared.
        row.binding = ctrl::KeyBinding();
    } else if (row.allowsSubKey && row.binding.isValidBinding() && !row.binding.hasSubKeyCode() &&
               pressed.isValidBinding() && !pressed.hasAnyModifiers()) {
        // A bare key pressed just after a fresh chord is the chord's SECOND key.
        row.binding.setSubKeyCode(aKey);
        row.allowsSubKey = false;
    } else if (pressed.isValidBinding()) {
        row.binding = pressed;
        row.allowsSubKey = true;
    }
    // An invalid key (a bare modifier, an unmapped key) changes nothing.

    refreshAll();
}

void KeyBindingViewModel::focusOut(int aRow) {
    if (validRow(aRow))
        mRows[aRow].allowsSubKey = false;
}

void KeyBindingViewModel::resetAll() {
    for (Row& row : mRows) {
        row.binding = row.defaultBinding;
        row.allowsSubKey = false;
    }
    refreshAll();
}

void KeyBindingViewModel::requestReset() { emit resetRequested(); }

void KeyBindingViewModel::accept() { emit acceptRequested(); }

void KeyBindingViewModel::cancel() { emit cancelRequested(); }

void KeyBindingViewModel::refreshTexts() {
    for (Row& row : mRows) {
        row.text = row.binding.text();
        row.conflict.clear();
    }

    for (int i = 0; i < mRows.size(); ++i) {
        if (!mRows[i].binding.isValidBinding())
            continue;

        for (int j = i - 1; j >= 0; --j) {
            if (mRows[i].binding.conflictsWith(mRows[j].binding)) {
                mRows[i].conflict = mRows[j].group + QLatin1Char('/') + mRows[j].label;
                mRows[j].conflict = mRows[i].group + QLatin1Char('/') + mRows[i].label;
                break;
            }
        }
    }
}

void KeyBindingViewModel::refreshAll() {
    refreshTexts();
    if (!mRows.isEmpty())
        emit dataChanged(index(0), index(mRows.size() - 1), QList<int>{TextRole, ConflictRole});
}

void KeyBindingViewModel::recomputeGroups() {
    mGroups.clear();
    for (const Row& row : mRows) {
        if (!mGroups.contains(row.group))
            mGroups.push_back(row.group);
    }

    mMaxGroupRows = 0;
    for (const QString& group : mGroups) {
        int rows = 0;
        for (const Row& row : mRows) {
            if (row.group == group)
                ++rows;
        }
        mMaxGroupRows = qMax(mMaxGroupRows, rows);
    }

    emit groupsChanged();
}

} // namespace gui
