#ifndef GUI_QML_DOCKSLOTSMODEL_H
#define GUI_QML_DOCKSLOTSMODEL_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <Qt>

namespace gui {

// One layout toggle in the header: a slot (left/right/bottom), its glyph, and the
// two states the button shows.
//
// The slot model is separate from MainWindow's docking code on purpose. The header
// scene may only ask "toggle this slot"; which panels live there, and what happens
// when one is hidden, stays with the window that owns the QDockWidgets. That is also
// why the checked state is WRITTEN by the window after every visibility pass instead
// of being flipped optimistically here: a slot can contain a tab group, a dragged
// panel or nothing at all, and only the window can answer "is anything in it
// visible".
class DockSlotItem: public QObject {
    Q_OBJECT
    Q_PROPERTY(QString glyph READ glyph NOTIFY changed)
    Q_PROPERTY(QString accessibleName READ accessibleName NOTIFY changed)
    Q_PROPERTY(QString tip READ tip NOTIFY changed)
    Q_PROPERTY(bool checked READ isChecked NOTIFY changed)
    Q_PROPERTY(bool available READ isAvailable NOTIFY changed)
public:
    DockSlotItem(Qt::DockWidgetArea aArea,
                 const QString& aGlyph,
                 const QString& aAccessibleName,
                 QObject* aParent = nullptr);

    Qt::DockWidgetArea area() const { return mArea; }
    QString glyph() const { return mGlyph; }
    QString accessibleName() const { return mAccessibleName; }
    QString tip() const { return mTip; }
    bool isChecked() const { return mChecked; }
    bool isAvailable() const { return mAvailable; }

    void setAccessibleName(const QString& aName);
    void setTip(const QString& aTip);
    void setChecked(bool aChecked);
    void setAvailable(bool aAvailable);

signals:
    void changed();

private:
    Qt::DockWidgetArea mArea;
    QString mGlyph;
    QString mAccessibleName;
    QString mTip;
    bool mChecked = false;
    bool mAvailable = false;
};

// The fixed description of a slot the window owns. The labels are translated by
// MainWindow (their context is the window's, where the docking code lives), and the
// model only stores them.
struct DockSlotSpec {
    Qt::DockWidgetArea area;
    QString glyph;
    QString accessibleName;
};

// The header's three slot toggles. `setSlots` is called once by MainWindow as the
// docks are built; `setSlotState` follows every layout change.
class DockSlotsModel: public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList toggles READ toggles NOTIFY togglesChanged)
public:
    explicit DockSlotsModel(QObject* aParent = nullptr);

    void setSlots(const QVector<DockSlotSpec>& aSlots);
    void setSlotState(Qt::DockWidgetArea aArea,
                      bool aChecked,
                      bool aAvailable,
                      const QString& aAccessibleName,
                      const QString& aTip);
    QVariantList toggles() const;

    // The scene's route: a slot button asks its own model to toggle, and the model
    // names the area for the window. No QDockWidget or QWidget type crosses this
    // boundary.
    Q_INVOKABLE void toggle(int aIndex);

signals:
    void togglesChanged();
    void toggleRequested(Qt::DockWidgetArea aArea);

private:
    QVector<DockSlotItem*> mSlots;
};

} // namespace gui

#endif // GUI_QML_DOCKSLOTSMODEL_H
