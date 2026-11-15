// The QML inspector's model contract: values, key states, model-2 writes, the
// filter stack, and undo. The scene itself is verified live; this gate keeps the
// document decisions from depending on whether a delegate happened to load.
//
// Run: QT_QPA_PLATFORM=offscreen ./inspector_view_model
#include <QApplication>
#include <QVariantMap>
#include <cstdio>

#include "core/Animator.h"
#include "core/BlurKey.h"
#include "core/FolderNode.h"
#include "core/HsvKey.h"
#include "core/MoveKey.h"
#include "core/OpaKey.h"
#include "core/Project.h"
#include "core/RotateKey.h"
#include "gui/qml/FilterStackModel.h"
#include "gui/qml/InspectorRowModel.h"
#include "gui/qml/InspectorViewModel.h"
#include "gui/ViaPoint.h"
#include "img/BlendMode.h"
#include "XCAssert.h"
#include "XCReport.h"

#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace gui {
// The model only calls this from the asset chooser; a headless gate never opens
// the dialog. Defining the one symbol keeps the test free of the GL/dialog stack.
img::ResourceNode* ViaPoint::requireOneResource() { return nullptr; }
} // namespace gui

namespace {

class TestAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return mFrame; }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
    void setFrame(int aFrame) { mFrame = core::Frame(aFrame); }

private:
    core::Frame mFrame;
};



#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                                   \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (false)

const core::MoveKey::Data& moveDefault(const core::TimeLine& line) {
    return static_cast<const core::MoveKey*>(line.defaultKey(core::TimeKeyType_Move))->data();
}

// The expansion state exactly as the delegate would read it, row by row. The
// gate prints this before the checks below so a regression names the cards it
// left open (or closed) instead of only the line that tripped.
QString expansionState(gui::FilterStackModel& aFilters) {
    QString text;
    const int count = aFilters.rows().size();
    for (int i = 0; i < count; ++i) {
        gui::FilterRowModel* row = aFilters.rowAt(i);
        if (!row)
            continue;
        if (!text.isEmpty())
            text += QLatin1Char(' ');
        text += row->kind() + QLatin1Char(':')
                + (row->expanded() ? QStringLiteral("open") : QStringLiteral("closed"));
    }
    return text.isEmpty() ? QStringLiteral("<no rows>") : text;
}

int run() {
    TestAnimator animator;
    animator.setFrame(10);
    core::Project project("inspector_view_model.anie", animator, nullptr);
    core::FolderNode folder(QStringLiteral("folder"));
    folder.setDefaultPosture(QVector2D(100.0f, 200.0f));
    folder.setDefaultDepth(-3.0f);
    folder.setDefaultOpacity(0.75f);
    core::TimeLine& line = *folder.timeLine();

    gui::InspectorViewModel inspector(nullptr);
    inspector.setProject(&project);
    inspector.setTarget(&folder);

    // A fresh layer: model 2 reads the defaults as its values.
    CHECK(inspector.move()->x() == 100.0);
    CHECK(inspector.move()->y() == 200.0);
    CHECK(inspector.depth()->depth() == -3.0);
    CHECK(inspector.opacity()->opacity() == 0.75);
    CHECK(inspector.move()->keyState() == QStringLiteral("static"));
    CHECK(inspector.move()->keyEnabled());

    // A static edit writes the default and creates no key.
    inspector.move()->setX(11.0);
    CHECK(line.map(core::TimeKeyType_Move).isEmpty());
    CHECK(moveDefault(line).pos() == QVector2D(11.0f, 200.0f));

    // A keyed property edits the key at the playhead, and undo restores it.
    inspector.move()->toggleKey();
    CHECK(inspector.move()->keyState() == QStringLiteral("keyed"));
    CHECK(line.hasTimeKey(core::TimeKeyType_Move, 10));
    inspector.move()->setX(11.0);
    inspector.move()->setY(33.0);
    const auto* key = static_cast<const core::MoveKey*>(line.timeKey(core::TimeKeyType_Move, 10));
    CHECK(key && key->data().pos() == QVector2D(11.0f, 33.0f));
    project.commandStack().undo();
    inspector.refresh();
    const auto* undone = static_cast<const core::MoveKey*>(line.timeKey(core::TimeKeyType_Move, 10));
    CHECK(undone && undone->data().pos() == QVector2D(11.0f, 0.0f));

    // The dot is a toggle; removing the key keeps the static base.
    inspector.move()->toggleKey();
    CHECK(inspector.move()->keyState() == QStringLiteral("static"));
    CHECK(!line.hasTimeKey(core::TimeKeyType_Move, 10));
    CHECK(moveDefault(line).pos() == QVector2D(11.0f, 200.0f));

    // Rotation is degrees in the model and radians in the document.
    inspector.rotate()->setAngle(45.0);
    CHECK(inspector.rotate()->angle() == 45.0);
    const auto* rotate = static_cast<const core::RotateKey*>(line.defaultKey(core::TimeKeyType_Rotate));
    CHECK(rotate && std::abs(rotate->data().rotate() - 0.7853982f) < 0.0001f);

    // Filters: add is static-first, the dot creates the explicit key, and the
    // row's fields edit the document.
    CHECK(inspector.filters()->rows().isEmpty());
    // THE CATALOG ANSWERS A TYPO. `notes.md`: a typo left the picker's list EMPTY, and
    // the fix is the shared fuzzy matcher, whose rule 7 allows one edit (two from seven
    // characters). These are the rows the picker offers, by catalog index, and the
    // NEGATIVES are as load-bearing as the positives: a tolerant matcher that matches
    // everything is worse than a substring test.
    {
        QStringList names;
        for (const QVariant& entry : inspector.filters()->catalog())
            names.append(entry.toMap().value(QStringLiteral("name")).toString());
        CHECK(names.size() == 4);
        const auto score = [&](const QString& query, int index) {
            return inspector.filters()->catalogScore(index, query);
        };
        const int blurRow = names.indexOf(QStringLiteral("Blur"));
        const int hsvRow = names.indexOf(QStringLiteral("HSV"));
        CHECK(blurRow >= 0 && hsvRow >= 0);
        std::printf("MEASURED catalog scores: blur=%d blurr=%d blu=%d hsb=%d zzzz=%d empty=%d\n",
                    score(QStringLiteral("blur"), blurRow),
                    score(QStringLiteral("blurr"), blurRow),
                    score(QStringLiteral("blu"), blurRow),
                    score(QStringLiteral("hsb"), hsvRow),
                    score(QStringLiteral("zzzz"), blurRow),
                    score(QString(), blurRow));
        CHECK(score(QStringLiteral("blur"), blurRow) > 0);      // the exact word
        CHECK(score(QStringLiteral("blurr"), blurRow) > 0);     // ONE LETTER TOO MANY - the report
        CHECK(score(QStringLiteral("bulr"), blurRow) > 0);      // a transposition
        CHECK(score(QStringLiteral("blu"), blurRow) > 0);       // a prefix
        CHECK(score(QStringLiteral("blurr"), hsvRow) == -1);    // and it does not match everything
        CHECK(score(QStringLiteral("zzzz"), blurRow) == -1);    // unrelated
        CHECK(score(QStringLiteral("hsb"), hsvRow) == -1);      // 3 letters: below rule 7's floor
        CHECK(score(QString(), blurRow) >= 0);                  // empty query keeps every row
        CHECK(score(QStringLiteral("deform"), names.indexOf(QStringLiteral("Wave Deform"))) > 0);
        // A typo must not OUTRANK the word it corrects: the picker's highlight starts at
        // the first row the query answers, so an inverted order would aim Return at a
        // correction while the exact word sat below it.
        CHECK(score(QStringLiteral("blur"), blurRow) > score(QStringLiteral("blurr"), blurRow));
    }

    inspector.filters()->addFilter(core::TimeKeyType_HSV);
    CHECK(inspector.filters()->rows().size() == 1);
    CHECK(inspector.filters()->hasFilter(core::TimeKeyType_HSV));
    auto* hsv = inspector.filters()->rowAt(0);
    CHECK(hsv && hsv->kind() == QStringLiteral("hsv"));
    CHECK(hsv->keyState() == QStringLiteral("static"));
    hsv->setHue(42);
    CHECK(line.map(core::TimeKeyType_HSV).isEmpty());
    CHECK(static_cast<const core::HSVKey*>(line.defaultKey(core::TimeKeyType_HSV))->data().hsv().at(0) == 42);
    hsv->toggleKey();
    CHECK(hsv->keyState() == QStringLiteral("keyed"));

    // The first add expands; collapsing it is a user state that survives a
    // frame refresh (a playback tick must not reopen a card).
    CHECK(hsv->expanded());
    inspector.filters()->toggleExpanded(core::TimeKeyType_HSV);
    CHECK(!hsv->expanded());
    inspector.refreshFrame();
    CHECK(!hsv->expanded());

    inspector.filters()->addFilter(core::TimeKeyType_Blur);
    CHECK(inspector.filters()->rows().size() == 2);
    CHECK(inspector.filters()->rowAt(1)->kind() == QStringLiteral("blur"));

    // Expansion is a SET keyed by TYPE, not one slot. The second add opens its
    // own card and leaves the first card's state alone (it was collapsed just
    // above); the old single `mSelected` could not describe this state at all,
    // because it re-derived every row from one type and so kept exactly one
    // card open by construction.
    std::printf("expanded after the second add: %s\n", qPrintable(expansionState(*inspector.filters())));
    CHECK(!inspector.filters()->rowAt(0)->expanded());
    CHECK(inspector.filters()->rowAt(1)->expanded());

    inspector.filters()->toggleExpanded(core::TimeKeyType_HSV);
    std::printf("expanded with both cards open: %s\n", qPrintable(expansionState(*inspector.filters())));
    CHECK(inspector.filters()->rowAt(0)->expanded());
    CHECK(inspector.filters()->rowAt(1)->expanded());

    // A playback tick re-projects the set; with no exclusivity in it, both stay
    // open. This is the frame-tick path that used to close all but one card.
    inspector.refreshFrame();
    std::printf("expanded after refreshFrame: %s\n", qPrintable(expansionState(*inspector.filters())));
    CHECK(inspector.filters()->rowAt(0)->expanded());
    CHECK(inspector.filters()->rowAt(1)->expanded());

    // One header click flips one type and cannot touch the other card.
    inspector.filters()->toggleExpanded(core::TimeKeyType_Blur);
    std::printf("expanded after closing blur: %s\n", qPrintable(expansionState(*inspector.filters())));
    CHECK(inspector.filters()->rowAt(0)->expanded());
    CHECK(!inspector.filters()->rowAt(1)->expanded());
    inspector.filters()->toggleExpanded(core::TimeKeyType_Blur);
    std::printf("expanded after reopening blur: %s\n", qPrintable(expansionState(*inspector.filters())));
    CHECK(inspector.filters()->rowAt(0)->expanded());
    CHECK(inspector.filters()->rowAt(1)->expanded());

    // Reorder and the eye are stack state, both undoable.
    inspector.filters()->moveFilter(core::TimeKeyType_Blur, 0);
    CHECK(inspector.filters()->rowAt(0)->kind() == QStringLiteral("blur"));
    // A reorder DESTROYS and recreates every row (that is why the state cannot
    // live on a row pointer or an index); the set carries the open cards over,
    // and the rebuilt rows are constructed already expanded.
    std::printf("expanded after moving blur to the top: %s\n", qPrintable(expansionState(*inspector.filters())));
    CHECK(inspector.filters()->rowAt(0)->expanded());
    CHECK(inspector.filters()->rowAt(1)->expanded());
    project.commandStack().undo();
    inspector.refresh();
    CHECK(inspector.filters()->rowAt(0)->kind() == QStringLiteral("hsv"));
    // The undo is another rebuild; it must not reopen or close anything either.
    std::printf("expanded after undoing the reorder: %s\n", qPrintable(expansionState(*inspector.filters())));
    CHECK(inspector.filters()->rowAt(0)->expanded());
    CHECK(inspector.filters()->rowAt(1)->expanded());
    inspector.filters()->setFilterEnabled(core::TimeKeyType_Blur, false);
    CHECK(!line.filterEnabled(core::TimeKeyType_Blur));
    project.commandStack().undo();
    inspector.refresh();
    CHECK(line.filterEnabled(core::TimeKeyType_Blur));

    // Removing a filter also removes its lane's explicit keys, in one step.
    inspector.filters()->removeFilter(core::TimeKeyType_HSV);
    CHECK(!inspector.filters()->hasFilter(core::TimeKeyType_HSV));
    CHECK(!line.hasTimeKey(core::TimeKeyType_HSV, 10));
    // The removed type leaves the set with its filter; the other card is still
    // open after the rebuild that removal forces.
    std::printf("expanded after removing hsv: %s\n", qPrintable(expansionState(*inspector.filters())));
    CHECK(inspector.filters()->rows().size() == 1);
    CHECK(inspector.filters()->rowAt(0)->kind() == QStringLiteral("blur"));
    CHECK(inspector.filters()->rowAt(0)->expanded());
    project.commandStack().undo();
    inspector.refresh();
    CHECK(inspector.filters()->hasFilter(core::TimeKeyType_HSV));
    CHECK(line.hasTimeKey(core::TimeKeyType_HSV, 10));

    // Constants are renderer state; disabled on a node without a renderer is
    // covered by the widget path, and here the folder reports both.
    CHECK(inspector.blendEnabled());
    CHECK(inspector.clippedEnabled());
    inspector.setClipped(true);
    CHECK(folder.renderer()->isClipped());
    project.commandStack().undo();
    inspector.refresh();
    CHECK(!folder.renderer()->isClipped());

    // The blend dropdown's model: 28 modes, and the widget control's five group
    // dividers (after Normal, DarkerColor, LighterColor, HardMix, Divide) carried
    // as a `separatorAfter` flag on the option map - and on nothing else. Named
    // by MODE, not by index: the flag is what AeComboBox's delegate turns into the
    // style's 9px band, so a row count or an off-by-one here would silently move
    // a rule in the popup.
    const QVariantList blendOptions = inspector.blendOptions();
    if (blendOptions.size() != 28) {
        std::printf("FAIL: blend options: %d rows, expected 28\n", int(blendOptions.size()));
        return 1;
    }
    const img::BlendMode blendGroups[] = {
        img::BlendMode_Normal,
        img::BlendMode_DarkerColor,
        img::BlendMode_LighterColor,
        img::BlendMode_HardMix,
        img::BlendMode_Divide,
    };
    for (int i = 0; i < blendOptions.size(); ++i) {
        const QVariantMap option = blendOptions.at(i).toMap();
        const int mode = option.value(QStringLiteral("value")).toInt();
        bool expectSeparator = false;
        for (img::BlendMode group : blendGroups)
            expectSeparator = expectSeparator || mode == int(group);
        const bool separator = option.value(QStringLiteral("separatorAfter")).toBool();
        if (separator != expectSeparator) {
            std::printf("FAIL: row %d (%s): separatorAfter %s, expected %s\n", i,
                        qPrintable(option.value(QStringLiteral("label")).toString()),
                        separator ? "true" : "false", expectSeparator ? "true" : "false");
            return 1;
        }
    }

    // The flag is decoration: the value <-> index round trip the panel edits
    // through must still resolve every mode in both directions.
    for (int i = 0; i < blendOptions.size(); ++i) {
        const img::BlendMode expected =
            img::BlendMode(blendOptions.at(i).toMap().value(QStringLiteral("value")).toInt());
        inspector.setBlendIndex(i);
        if (folder.renderer()->blendMode() != expected) {
            std::printf("FAIL: setBlendIndex(%d) selected mode %d, expected %d\n", i,
                        int(folder.renderer()->blendMode()), int(expected));
            return 1;
        }
        inspector.refresh();
        if (inspector.blendIndex() != i) {
            std::printf("FAIL: row %d (%s) reads back as index %d\n", i,
                        qPrintable(blendOptions.at(i).toMap().value(QStringLiteral("label")).toString()),
                        inspector.blendIndex());
            return 1;
        }
    }

    std::printf("OK: inspector view model passes\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return run();
}
