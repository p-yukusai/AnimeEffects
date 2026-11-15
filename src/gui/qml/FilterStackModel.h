#ifndef GUI_QML_FILTERSTACKMODEL_H
#define GUI_QML_FILTERSTACKMODEL_H

#include <QColor>
#include <QCoreApplication>
#include <QObject>
#include <QVariantList>
#include <QVector>

#include "core/TimeKeyType.h"
#include "core/WaveKey.h"
#include "core/TimeLine.h"

namespace gui {

class InspectorViewModel;

class FilterRowModel: public QObject {
    Q_OBJECT
    Q_PROPERTY(int type READ type CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool expanded READ expanded NOTIFY changed)
    Q_PROPERTY(QString keyState READ keyState NOTIFY changed)
    Q_PROPERTY(QColor keyColor READ keyColor NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantList summarySegments READ summarySegments NOTIFY changed)
    Q_PROPERTY(QString hueLabel READ hueLabel CONSTANT)
    Q_PROPERTY(QString saturationLabel READ saturationLabel CONSTANT)
    Q_PROPERTY(QString valueLabel READ valueLabel CONSTANT)
    Q_PROPERTY(QString colorLabel READ colorLabel CONSTANT)
    Q_PROPERTY(QString absoluteLabel READ absoluteLabel CONSTANT)
    Q_PROPERTY(QString amountLabel READ amountLabel CONSTANT)
    Q_PROPERTY(QString modeLabel READ modeLabel CONSTANT)
    Q_PROPERTY(QString directionalLabel READ directionalLabel CONSTANT)
    Q_PROPERTY(QString blurXLabel READ blurXLabel CONSTANT)
    Q_PROPERTY(QString blurYLabel READ blurYLabel CONSTANT)
    Q_PROPERTY(QString angleLabel READ angleLabel CONSTANT)
    Q_PROPERTY(QString strengthLabel READ strengthLabel CONSTANT)
    Q_PROPERTY(QString wavelengthLabel READ wavelengthLabel CONSTANT)
    Q_PROPERTY(QString directionLabel READ directionLabel CONSTANT)
    Q_PROPERTY(QString phaseLabel READ phaseLabel CONSTANT)
    Q_PROPERTY(QString mixLabel READ mixLabel CONSTANT)
    Q_PROPERTY(QString speedLabel READ speedLabel CONSTANT)
    Q_PROPERTY(QString crestLabel READ crestLabel CONSTANT)
    Q_PROPERTY(QString dualAngleLabel READ dualAngleLabel CONSTANT)
    Q_PROPERTY(QString dualScaleLabel READ dualScaleLabel CONSTANT)
    Q_PROPERTY(QString deformLabel READ deformLabel CONSTANT)
    Q_PROPERTY(QString rippleLabel READ rippleLabel CONSTANT)
    Q_PROPERTY(QString swayLabel READ swayLabel CONSTANT)

    Q_PROPERTY(int hue READ hue NOTIFY changed)
    Q_PROPERTY(int saturation READ saturation NOTIFY changed)
    Q_PROPERTY(int value READ value NOTIFY changed)
    Q_PROPERTY(bool absolute READ absolute NOTIFY changed)
    Q_PROPERTY(double amount READ amount NOTIFY changed)
    Q_PROPERTY(bool directional READ directional NOTIFY changed)
    Q_PROPERTY(double blurX READ blurX NOTIFY changed)
    Q_PROPERTY(double blurY READ blurY NOTIFY changed)
    Q_PROPERTY(double blurAngle READ blurAngle NOTIFY changed)

    Q_PROPERTY(double waveStrength READ waveStrength NOTIFY changed)
    Q_PROPERTY(double waveWavelength READ waveWavelength NOTIFY changed)
    Q_PROPERTY(double waveDirection READ waveDirection NOTIFY changed)
    Q_PROPERTY(double wavePhase READ wavePhase NOTIFY changed)
    Q_PROPERTY(double waveMix READ waveMix NOTIFY changed)
    Q_PROPERTY(double waveSpeed READ waveSpeed NOTIFY changed)
    Q_PROPERTY(double waveCrest READ waveCrest NOTIFY changed)
    Q_PROPERTY(double waveDualAngle READ waveDualAngle NOTIFY changed)
    Q_PROPERTY(double waveDualScale READ waveDualScale NOTIFY changed)
    Q_PROPERTY(bool waveRipple READ waveRipple NOTIFY changed)
    // the painted mask's state, per channel: a row offers Paint always and Clear only
    // when a field exists (a fresh field is neutral and renders like none)
    Q_PROPERTY(bool hasAmountMask READ hasAmountMask NOTIFY changed)
    Q_PROPERTY(bool hasTimeMask READ hasTimeMask NOTIFY changed)
    // The Mask tool is already painting THIS field of THIS node: the card's Paint
    // icon reports the state instead of re-arming it.
    Q_PROPERTY(bool amountPaintActive READ amountPaintActive NOTIFY changed)
    Q_PROPERTY(bool timePaintActive READ timePaintActive NOTIFY changed)
    // The mask rows' texts. They are Q_PROPERTYs like every other label here: a plain
    // method is not readable from QML, and a binding that reads one evaluates to
    // `undefined` - which is how these rows were MISSING from the card while the C++
    // model answered correctly (measured on the running dialog).
    Q_PROPERTY(QString maskLabel READ maskLabel CONSTANT)
    Q_PROPERTY(QString timeMaskLabel READ timeMaskLabel CONSTANT)
    Q_PROPERTY(QString paintMaskLabel READ paintMaskLabel CONSTANT)
    Q_PROPERTY(QString clearMaskLabel READ clearMaskLabel CONSTANT)
    Q_PROPERTY(QString paintTimeMaskLabel READ paintTimeMaskLabel CONSTANT)
    Q_PROPERTY(QString clearTimeMaskLabel READ clearTimeMaskLabel CONSTANT)

public:
    // `aExpanded` has to be a construction argument, not a follow-up call: a row
    // is built while the stack rebuilds (add, remove, reorder), and the QML
    // delegate reads `expanded` as soon as rowsChanged arrives. Constructing it
    // already open keeps the card open on that same frame; setting the flag one
    // statement later leaves a delegate that loaded collapsed.
    FilterRowModel(core::TimeKeyType aType, InspectorViewModel& aOwner, bool aExpanded, QObject* aParent = nullptr);

    int type() const { return int(mType); }
    QString kind() const;
    QString name() const;
    bool enabled() const { return mEnabled; }
    bool expanded() const { return mExpanded; }
    QString keyState() const { return mKeyState; }
    QColor keyColor() const { return mKeyColor; }
    QString summary() const { return mSummary; }
    QVariantList summarySegments() const { return mSummarySegments; }
    QString hueLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Hue"); }
    QString saturationLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Saturation"); }
    QString valueLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Value"); }
    QString colorLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Color"); }
    QString absoluteLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Absolute"); }
    QString amountLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Amount"); }
    QString modeLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Mode"); }
    QString directionalLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Directional"); }
    QString blurXLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Blur X"); }
    QString blurYLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Blur Y"); }
    QString angleLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Angle"); }
    QString strengthLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Strength"); }
    QString wavelengthLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Wavelength"); }
    QString directionLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Direction"); }
    QString phaseLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Phase"); }
    QString mixLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Dual wave"); }
    QString speedLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Speed"); }
    QString crestLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Crest"); }
    QString dualAngleLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Dual direction"); }
    QString dualScaleLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Dual scale"); }
    QString deformLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Deform"); }
    QString rippleLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Ripple"); }
    QString swayLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Sway"); }
    QString maskLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Mask"); }
    QString timeMaskLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Time mask"); }
    QString paintMaskLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Paint mask"); }
    QString clearMaskLabel() const { return QCoreApplication::translate("gui::prop::FilterStackPanel", "Clear mask"); }
    QString paintTimeMaskLabel() const {
        return QCoreApplication::translate("gui::prop::FilterStackPanel", "Paint time offset mask");
    }
    QString clearTimeMaskLabel() const {
        return QCoreApplication::translate("gui::prop::FilterStackPanel", "Clear time offset mask");
    }

    int hue() const { return mHue; }
    int saturation() const { return mSaturation; }
    int value() const { return mValue; }
    bool absolute() const { return mAbsolute; }
    double amount() const { return mAmount; }
    bool directional() const { return mDirectional; }
    double blurX() const { return mBlurX; }
    double blurY() const { return mBlurY; }
    double blurAngle() const { return mBlurAngle; }

    double waveStrength() const { return mWave.strength(); }
    double waveWavelength() const { return mWave.wavelength(); }
    double waveDirection() const { return mWave.angleDeg(); }
    double wavePhase() const { return mWave.phase(); }
    double waveMix() const { return mWave.mix(); }
    double waveSpeed() const { return mWave.speed(); }
    double waveCrest() const { return mWave.crest(); }
    double waveDualAngle() const { return mWave.dualAngleDeg(); }
    double waveDualScale() const { return mWave.dualScale(); }
    bool waveRipple() const { return mWave.deform() == core::WaveKey::Deform_Ripple; }
    bool hasAmountMask() const { return mHasAmountMask; }
    bool hasTimeMask() const { return mHasTimeMask; }
    bool amountPaintActive() const { return mAmountPaintActive; }
    bool timePaintActive() const { return mTimePaintActive; }

    void refresh();
    void refreshTheme();
    // The tool's paint target changed. Cheap: it only re-reads the two flags on
    // this row and notifies when one of them moved.
    void refreshMaskPaintState();
    void setExpanded(bool aExpanded);

    Q_INVOKABLE void toggleExpanded();
    Q_INVOKABLE void toggleKey();
    // PER-FIELD KEYING, the same contract InspectorRowModel offers: the card's
    // property dot keys every channel/state of the filter; one numeric field's
    // menu keys only its own channel.
    Q_INVOKABLE bool channelKeyExists(int aComponent) const;
    Q_INVOKABLE QString channelKeyAction(int aComponent) const;
    Q_INVOKABLE void toggleChannelKey(int aComponent);
    Q_INVOKABLE void toggleEnabled();
    Q_INVOKABLE void remove();

    Q_INVOKABLE void setHue(int aValue);
    Q_INVOKABLE void setSaturation(int aValue);
    Q_INVOKABLE void setValue(int aValue);
    Q_INVOKABLE void setAbsolute(bool aValue);
    Q_INVOKABLE void setAmount(double aValue);
    Q_INVOKABLE void setDirectional(bool aValue);
    Q_INVOKABLE void setBlurX(double aValue);
    Q_INVOKABLE void setBlurY(double aValue);
    Q_INVOKABLE void setBlurAngle(double aValue);

    Q_INVOKABLE void setWaveStrength(double aValue);
    Q_INVOKABLE void setWaveWavelength(double aValue);
    Q_INVOKABLE void setWaveDirection(double aValue);
    Q_INVOKABLE void setWavePhase(double aValue);
    Q_INVOKABLE void setWaveMix(double aValue);
    Q_INVOKABLE void setWaveSpeed(double aValue);
    Q_INVOKABLE void setWaveCrest(double aValue);
    Q_INVOKABLE void setWaveDualAngle(double aValue);
    Q_INVOKABLE void setWaveDualScale(double aValue);
    Q_INVOKABLE void setWaveRipple(bool aValue);

    // The mask actions hand the lane and channel to the Mask tool through the owner's
    // signal (the toolbox tool is where painting happens), or clear the field here.
    Q_INVOKABLE void paintMask(int aChannel);
    Q_INVOKABLE void clearMask(int aChannel);

signals:
    void changed();

private:
    bool isHSV() const { return mType == core::TimeKeyType_HSV; }
    bool isBlur() const { return mType == core::TimeKeyType_Blur; }
    bool isWave() const { return mType == core::TimeKeyType_Wave; }
    void writeBlurEllipse(int aComponent);
    void writeWave(int aComponent);
    float channelValue(int aComponent) const;
    QString channelName(int aComponent) const;
    // Writes the row's whole wave data, so every field the user did not touch
    // keeps the value the document had at this frame.

    InspectorViewModel& mOwner;
    core::TimeKeyType mType;
    bool mEnabled = true;
    bool mExpanded = false;
    QString mKeyState;
    QColor mKeyColor;
    QString mSummary;
    QVariantList mSummarySegments;

    int mHue = 0;
    int mSaturation = 100;
    int mValue = 100;
    bool mAbsolute = false;
    double mAmount = 0.0;
    bool mDirectional = false;
    double mBlurX = 0.0;
    double mBlurY = 0.0;
    double mBlurAngle = 0.0;
    core::WaveKey::Data mWave;
    bool mHasAmountMask = false;
    bool mHasTimeMask = false;
    bool mAmountPaintActive = false;
    bool mTimePaintActive = false;
};

class FilterStackModel: public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList catalog READ catalog CONSTANT)
    Q_PROPERTY(QString noFiltersText READ noFiltersText CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString addLabel READ addLabel CONSTANT)
    Q_PROPERTY(QString searchLabel READ searchLabel CONSTANT)
    Q_PROPERTY(QString noMatchLabel READ noMatchLabel CONSTANT)
    Q_PROPERTY(QString addedLabel READ addedLabel CONSTANT)
    Q_PROPERTY(QString comingSoonLabel READ comingSoonLabel CONSTANT)

public:
    explicit FilterStackModel(InspectorViewModel& aOwner, QObject* aParent = nullptr);

    QVariantList rows() const;
    QVariantList catalog() const;

    // How well the catalog entry at `aIndex` answers `aQuery`, or -1 for no match: the
    // ONE rule the picker's visibility, its highlight walk and its Return all read, so a
    // row cannot be visible under one rule and unchoosable under another.
    //
    // It is gui::fuzzy's score, not a substring test: `notes.md`'s report was that a
    // TYPO left the list empty ("blurr" matched nothing, because every rule needs the
    // token to be a subsequence of a word). Q_INVOKABLE rather than a property because
    // the answer depends on the query, which the CALLER passes - a QML binding that calls
    // this must also read `search.text` itself, exactly as `queryMatches` documents.
    Q_INVOKABLE int catalogScore(int aIndex, const QString& aQuery) const;

    // The height of the catalog row at `aIndex`: 34 for a filter you can add, 38 for one
    // that is only announced (it carries a second, "coming soon" line). The CATALOG owns
    // the number so the picker's popup can size itself before its delegates exist - the
    // delegates read it too, so the two cannot disagree.
    Q_INVOKABLE int catalogRowHeight(int aIndex) const;
    QString noFiltersText() const;
    QString title() const;
    QString addLabel() const;
    QString searchLabel() const;
    QString noMatchLabel() const;
    QString addedLabel() const;
    QString comingSoonLabel() const;

    void setProject(core::Project* aProject);
    void setTarget(core::ObjectNode* aTarget);
    void refresh();
    void refreshTheme();
    // The tool's paint target changed. Cheap: it only re-reads the two flags on
    // this row and notifies when one of them moved.
    void refreshMaskPaintState();

    Q_INVOKABLE bool hasFilter(int aType) const;
    Q_INVOKABLE void addFilter(int aType);
    Q_INVOKABLE void removeFilter(int aType);
    Q_INVOKABLE void moveFilter(int aType, int aTargetIndex);
    Q_INVOKABLE void setFilterEnabled(int aType, bool aEnabled);
    Q_INVOKABLE void toggleExpanded(int aType);

    FilterRowModel* rowAt(int aIndex) const {
        return (0 <= aIndex && aIndex < mRows.size()) ? mRows.at(aIndex) : nullptr;
    }

signals:
    void rowsChanged();

private:
    friend class FilterRowModel;
    FilterRowModel* rowFor(core::TimeKeyType aType) const;
    void rebuildRows(const QVector<core::FilterEntry>& aEntries);

    InspectorViewModel& mOwner;
    core::Project* mProject = nullptr;
    core::ObjectNode* mTarget = nullptr;
    QVector<FilterRowModel*> mRows;
    // Which filters are open, keyed by TYPE. It cannot be one slot or a row's
    // own flag: rebuildRows destroys every row for add, remove and reorder, so
    // an index or a pointer would be stale by the time the next toggle reads
    // it, and a single slot made a second card collapse the first. A row's
    // `expanded` is only the projection of this set that QML reads.
    QVector<core::TimeKeyType> mExpanded;
};

} // namespace gui

#endif // GUI_QML_FILTERSTACKMODEL_H
