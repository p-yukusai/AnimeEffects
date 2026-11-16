// The animation dock's tree, as a MODEL: what the rows are, what a click means, what a drag
// means, and that every action is ONE undo step.
//
// The tree used to be a QTreeWidget whose ITEMS were part of the undo record, so its
// behaviour lived in widget callbacks and could only be observed by driving a window. The
// model is derived from the document (gui/AnimationRow.h is the contract the timeline draws
// too), which is what makes this test possible - and what makes these the gates that matter:
//
//   * the ROWS: the document row first and expanded, one row per visible node, a row's height
//     equal to the lane block the timeline draws beside it (ctrl::TimeLineRow::displayHeight),
//     and a collapsed folder's summary band accounted for;
//   * the SELECTION: real list-view semantics - press / press-inside-a-selection / release /
//     ctrl / shift / the keyboard - because the widget's menu could only ever act on the row
//     it was opened on;
//   * DRAG AND DROP: the drop target the pointer computes (above / below / into), the refusal
//     to drop a node into its own subtree, and the document move it becomes - with ONE undo
//     restoring the order;
//   * the ACTIONS: rename, visibility, slim, add-base-values, duplicate, delete - each one
//     command and one event, which is what the widget could not promise (its "add default
//     keys" pushed one command per channel, its visibility toggle pushed none).
//
// Runs under Xvfb like the other layer-building gates: a core::LayerNode's constructor binds
// the mesh holder to GL (see filter_card_geometry in scripts/run_suites.sh).
#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>
#include <QJsonObject>
#include <QSet>
#include <QVector2D>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

#include "XCAssert.h"
#include "gl/Global.h"
#include "cmnd/Stack.h"
#include "core/FolderNode.h"
#include "core/LayerNode.h"
#include "core/ObjectNode.h"
#include "core/ObjectTree.h"
#include "core/Project.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/TreeActions.h"
#include "gui/AnimationRow.h"
#include "gui/qml/ObjectTreeModel.h"

namespace {

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

int failures = 0;
int checks = 0;

void check(bool aOk, const QString& aWhat) {
    ++checks;
    if (!aOk) {
        std::printf("FAIL  %s\n", qPrintable(aWhat));
        ++failures;
    }
}

class TestAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return mFrame; }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
    core::Frame mFrame;
};

// The fixture: a document ("doc") holding a folder ("group") with two layers inside, and one
// layer beside the folder, so a drag can move across parents and a range has ends.
class Fixture {
public:
    Fixture(): mAnimator(), mProject(QString(), mAnimator, nullptr) {
        auto* top = new core::FolderNode(QStringLiteral("doc"));
        mProject.objectTree().grabTopNode(top);
        mTop = top;

        mGroup = new core::FolderNode(QStringLiteral("group"));
        top->children().pushBack(mGroup);
        mInnerA = addLayer(*mGroup, QStringLiteral("innerA"));
        mInnerB = addLayer(*mGroup, QStringLiteral("innerB"));
        mLeaf = addLayer(*top, QStringLiteral("leaf"));
    }

    core::LayerNode* addLayer(core::FolderNode& aParent, const QString& aName) {
        auto* node = new core::LayerNode(aName, mProject.objectTree().shaderHolder());
        aParent.children().pushBack(node);
        return node;
    }

    core::Project& project() { return mProject; }
    core::FolderNode* top() { return mTop; }
    core::FolderNode* group() { return mGroup; }
    core::LayerNode* innerA() { return mInnerA; }
    core::LayerNode* innerB() { return mInnerB; }
    core::LayerNode* leaf() { return mLeaf; }
    cmnd::Stack& stack() { return mProject.commandStack(); }
    TestAnimator& animator() { return mAnimator; }

private:
    TestAnimator mAnimator;
    core::Project mProject;
    core::FolderNode* mTop = nullptr;
    core::FolderNode* mGroup = nullptr;
    core::LayerNode* mInnerA = nullptr;
    core::LayerNode* mInnerB = nullptr;
    core::LayerNode* mLeaf = nullptr;
};

// The row a node occupies, or -1.
int rowOf(gui::ObjectTreeModel& aModel, const core::ObjectNode* aNode) {
    const auto& rows = aModel.rows();
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i].node == aNode)
            return i;
    }
    return -1;
}

QStringList namesOf(gui::ObjectTreeModel& aModel) {
    QStringList names;
    for (int i = 0; i < aModel.rowCount(); ++i)
        names << aModel.data(aModel.index(i, 0), gui::ObjectTreeModel::NameRole).toString();
    return names;
}

QString joined(const QStringList& aNames) { return aNames.join(QLatin1Char('/')); }

bool namesAre(gui::ObjectTreeModel& aModel, std::initializer_list<const char*> aExpected) {
    QStringList expected;
    for (const char* name : aExpected)
        expected << QString::fromLatin1(name);
    const QStringList actual = namesOf(aModel);
    if (actual == expected)
        return true;
    std::printf("  rows: %s (expected %s)\n", qPrintable(joined(actual)), qPrintable(joined(expected)));
    return false;
}

QVariant role(gui::ObjectTreeModel& aModel, int aRow, int aRole) {
    if (aRow < 0 || aRow >= aModel.rowCount())
        return QVariant();
    return aModel.data(aModel.index(aRow, 0), aRole);
}

// The drop position a content-space y names, applied the way the scene applies it.
void dragTo(gui::ObjectTreeModel& aModel, int aRow, qreal aContentY) {
    aModel.beginDrag(aRow);
    aModel.updateDrag(aContentY, 0.0);
}

} // namespace

// LINKED BY NAME FROM libcore - file scope, not the anonymous namespace (see object_tree_panel.cpp)
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // A live GL context: a core::LayerNode compiles its mesh shader and binds GL in its
    // constructor (gl::Global::functions() asserts without one), which is why this gate runs
    // in the suite's GL branch under Xvfb - the same arrangement asset_remove_layers uses.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(fmt);
    QOpenGLWidget glWidget;
    glWidget.resize(4, 4);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        QTextStream(stderr) << "no GL context\n";
        return 1;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        QTextStream(stderr) << "no GL functions\n";
        return 1;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    // --- 1. the rows: the document first and expanded, then its visible children ---
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());

        check(model.rowCount() == 3, "the document row and its two children are the rows");
        check(namesAre(model, {"doc", "group", "leaf"}), "the rows are the document, the folder, the layer");
        check(role(model, 0, gui::ObjectTreeModel::IsDocumentRole).toBool(), "row 0 is the document");
        check(role(model, 0, gui::ObjectTreeModel::DepthRole).toInt() == 0, "the document is depth 0");
        check(role(model, 1, gui::ObjectTreeModel::DepthRole).toInt() == 1, "a child is depth 1");
        check(!role(model, 1, gui::ObjectTreeModel::IsFolderRole).toBool() == false, "group is a folder");
        check(!role(model, 2, gui::ObjectTreeModel::IsFolderRole).toBool(), "leaf is a layer");
        check(fixture.group()->children().size() == 2, "group really holds two layers");
        check(rowOf(model, fixture.innerA()) < 0,
              "a collapsed folder's children are not rows (its summary band stands for them)");

        // the heights, and the content offsets they produce
        const auto& rows = model.rows();
        check(rows[0].height == ctrl::TimeLineRow::kHeight, "the document row is 22px");
        check(rows[1].height == ctrl::TimeLineRow::displayHeight(*fixture.group(), false),
              "a row's height is the lane block's own height");
        check(rows[1].top == rows[0].height, "row 1 starts where row 0 ends");
        check(rows[2].top == rows[1].top + rows[1].height, "row 2 starts where row 1 ends");
        check(model.contentHeight() == rows[2].top + rows[2].height, "contentHeight is the last row's end");

        // a collapsed folder with keys below it owns a summary band; without one it keeps its
        // own lanes' height
        check(rows[1].closedFolder, "group is collapsed, so its row is closed");
        check(!rows[1].node->timeLine() || rows[1].node->timeLine()->validTypeCount() == 0,
              "group has no lanes of its own");
        std::printf("  group row: height %d closed %d\n", rows[1].height, int(rows[1].closedFolder));

        check(failures == before, "rows");
    }

    // --- 2. expansion: a folder's children become rows, and the heights follow keys ---
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());

        const int groupRow = rowOf(model, fixture.group());
        model.toggleExpanded(groupRow);
        check(namesAre(model, {"doc", "group", "innerA", "innerB", "leaf"}),
              "expanding a folder inserts its children in place");
        check(role(model, 2, gui::ObjectTreeModel::DepthRole).toInt() == 2,
              "a grandchild is depth 2");
        check(!model.rows()[rowOf(model, fixture.group())].closedFolder,
              "an expanded folder is not closed");
        check(model.rows()[rowOf(model, fixture.group())].height == 22 ||
                  model.rows()[rowOf(model, fixture.group())].height ==
                      ctrl::TimeLineRow::displayHeight(*fixture.group(), true),
              "an expanded folder's row is its own lanes alone");

        // collapsing again puts the summary band back and drops the rows
        model.toggleExpanded(rowOf(model, fixture.group()));
        check(namesAre(model, {"doc", "group", "leaf"}), "collapsing removes the children again");

        // a key adds a lane band: the row grows by kIncrease per animated channel
        ctrl::tree::addKeysAtPlayhead(fixture.project(), *fixture.innerA());
        model.toggleExpanded(rowOf(model, fixture.group()));
        const int innerRow = rowOf(model, fixture.innerA());
        check(innerRow > 0, "the keyed layer has a row again");
        const int expected = ctrl::TimeLineRow::calculateHeight(*fixture.innerA());
        check(model.rows()[innerRow].height == expected,
              "a keyed layer's row is as tall as its lane block");
        check(expected > ctrl::TimeLineRow::kHeight, "and that is taller than 22");
        check(failures == before, "expansion");
    }

    // --- 3. selection: press, press-inside, release, ctrl, shift, select all ---
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());
        model.toggleExpanded(rowOf(model, fixture.group()));

        // a plain press selects exactly one row
        model.pressRow(rowOf(model, fixture.innerA()), 0);
        check(model.selectionCount() == 1, "a plain press selects one row");
        check(model.representNode() == fixture.innerA(), "and it is the represent node");
        check(role(model, rowOf(model, fixture.innerA()), gui::ObjectTreeModel::SelectedRole).toBool(),
              "the row is painted as selected");

        // shift extends from the anchor
        model.pressRow(rowOf(model, fixture.leaf()), int(Qt::ShiftModifier));
        check(model.selectionCount() == 3, "shift extends to a range (innerA..leaf, three rows)");
        check(!model.selectedNodes().isEmpty() && model.selectedNodes().first() == fixture.innerA(),
              "the range keeps row order");
        check(model.representNode() == nullptr, "a multi-selection has no represent node");

        // a plain press INSIDE the selection does not collapse it (the drag needs it)...
        model.pressRow(rowOf(model, fixture.innerB()), 0);
        check(model.selectionCount() == 3, "a plain press inside a selection leaves it alone");
        // ...and the release, with no drag, collapses it to that row
        model.releaseRow(rowOf(model, fixture.innerB()), 0);
        check(model.selectionCount() == 1 && model.representNode() == fixture.innerB(),
              "the release collapses a plain press");

        // ctrl toggles one row in and out
        model.pressRow(rowOf(model, fixture.leaf()), int(Qt::ControlModifier));
        check(model.selectionCount() == 2, "ctrl adds a row");
        model.pressRow(rowOf(model, fixture.leaf()), int(Qt::ControlModifier));
        check(model.selectionCount() == 1, "ctrl on a selected row removes it");

        // the document row is not selectable: it is the document, not a layer. A press on it is
        // its own affordance - it opens or closes the document - and a collapse KEEPS a
        // selection that the closed row now stands for (the same rule the timeline has)
        model.pressRow(0, 0);
        check(model.rowCount() == 1, "pressing the document row COLLAPSES it");
        check(model.selectionCount() == 1, "and the selected layer is still selected");
        check(model.representNode() == nullptr, "but no row shows it, so nothing is tinted");
        model.pressRow(0, 0);
        check(model.rowCount() > 1, "pressing it again opens it");
        check(model.representNode() == fixture.innerB(), "and the layer is the represent node again");

        // select all skips the document row
        model.selectAll();
        check(model.selectionCount() == 4, "select all takes every layer, not the document");
        model.clearSelection();
        check(model.selectionCount() == 0 && model.representNode() == nullptr, "clearing empties it");
        check(failures == before, "selection");
    }

    // --- 4. the keyboard ---
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());
        model.toggleExpanded(rowOf(model, fixture.group()));

        // Down from nothing selects the first layer (the document is skipped)
        check(model.handleKey(Qt::Key_Down, 0), "Down is handled");
        check(model.representNode() == fixture.group(), "Down starts at the first row below the document");
        check(model.handleKey(Qt::Key_Down, 0) && model.representNode() == fixture.innerA(),
              "Down moves the cursor and the selection");
        check(model.handleKey(Qt::Key_Up, 0) && model.representNode() == fixture.group(), "Up moves back");

        check(model.handleKey(Qt::Key_Down, 0) && model.handleKey(Qt::Key_Down, int(Qt::ShiftModifier)),
              "shift+Down is handled");
        check(model.selectionCount() == 2,
              QStringLiteral("shift+Down extends the selection (%1 rows)").arg(model.selectionCount()));

        check(model.handleKey(Qt::Key_A, int(Qt::ControlModifier)), "ctrl+A is handled");
        check(model.selectionCount() == 4, "ctrl+A selects every layer");
        check(model.handleKey(Qt::Key_Escape, 0), "Escape is handled");
        check(model.selectionCount() == 0, "Escape clears the selection");

        // a key the tree does not act on is left to the application's own map
        check(!model.handleKey(Qt::Key_S, int(Qt::ControlModifier)), "ctrl+S is NOT consumed");
        check(!model.handleKey(Qt::Key_Delete, 0), "Delete with nothing selected is not consumed");

        model.pressRow(rowOf(model, fixture.innerA()), 0);
        check(model.handleKey(Qt::Key_F2, 0) && model.isRenaming(), "F2 starts a rename");
        model.cancelRename();
        check(!model.isRenaming(), "and Escape/release cancels it");
        check(failures == before, "keyboard");
    }

    // --- 5. rename and visibility are one command each, with an event ---
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());
        model.toggleExpanded(rowOf(model, fixture.group()));

        int attributeEvents = 0;
        const auto slot = fixture.project().onNodeAttributeModified.connect(
            [&attributeEvents](core::ObjectNode&, bool) { ++attributeEvents; }
        );

        model.beginRename(rowOf(model, fixture.innerA()));
        check(model.isRenaming(), "the row is in rename mode");
        model.commitRename(QStringLiteral("renamed"));
        check(fixture.innerA()->name() == QStringLiteral("renamed"), "the rename reached the document");
        check(role(model, rowOf(model, fixture.innerA()), gui::ObjectTreeModel::NameRole).toString() ==
                  QStringLiteral("renamed"),
              "and the row shows it");
        check(attributeEvents == 1, "a rename announces one attribute event");
        fixture.stack().undo();
        check(fixture.innerA()->name() == QStringLiteral("innerA"), "ONE undo takes the rename back");
        check(role(model, rowOf(model, fixture.innerA()), gui::ObjectTreeModel::NameRole).toString() ==
                  QStringLiteral("innerA"),
              "and the row followed the undo");

        // an empty name is refused (a layer with no name cannot be found again)
        model.beginRename(rowOf(model, fixture.innerA()));
        model.commitRename(QStringLiteral("   "));
        check(fixture.innerA()->name() == QStringLiteral("innerA"), "an empty rename is refused");

        // visibility: one command, one event, and the role flips
        const int eventBefore = attributeEvents;
        model.toggleVisibility(rowOf(model, fixture.innerA()));
        check(!fixture.innerA()->isVisible(), "the eye hid the layer");
        check(!role(model, rowOf(model, fixture.innerA()), gui::ObjectTreeModel::VisibleRole).toBool(),
              "and the row says so");
        check(attributeEvents == eventBefore + 1, "hiding announces one attribute event");
        fixture.stack().undo();
        check(fixture.innerA()->isVisible(), "ONE undo shows it again");

        fixture.project().onNodeAttributeModified.disconnect(slot);
        check(failures == before, "rename and visibility");
    }

    // --- 6. drag and drop: the target, the refusal, the move, one undo ---
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());
        model.toggleExpanded(rowOf(model, fixture.group()));

        // rows: doc / group / innerA / innerB / leaf. Drag `leaf` ABOVE `innerA` (into the group).
        const int leafRow = rowOf(model, fixture.leaf());
        const int innerARow = rowOf(model, fixture.innerA());
        const qreal aboveInnerA = model.rows()[innerARow].top + 1;
        dragTo(model, leafRow, aboveInnerA);
        check(role(model, innerARow, gui::ObjectTreeModel::DropZoneRole).toInt() ==
                  gui::ObjectTreeModel::DropAbove,
              "a drag in a row's top quarter says ABOVE");
        model.endDrag(true);
        check(fixture.group()->children().size() == 3, "the layer moved into the folder");
        check(namesAre(model, {"doc", "group", "leaf", "innerA", "innerB"}),
              "the rows follow the document's new order");
        check(model.selectionCount() == 1 && !model.selectedNodes().isEmpty() &&
                  model.selectedNodes().first() == fixture.leaf(),
              "the moved row stays selected");
        fixture.stack().undo();
        check(namesAre(model, {"doc", "group", "innerA", "innerB", "leaf"}),
              "ONE undo restores the order");
        check(fixture.leaf()->parent() == fixture.top(), "and the parent");
        fixture.stack().redo();
        check(fixture.leaf()->parent() == fixture.group(), "one redo moves it again");

        // a folder's middle is INSIDE it
        const int groupRow = rowOf(model, fixture.group());
        const qreal insideGroup = model.rows()[groupRow].top + model.rows()[groupRow].height / 2;
        dragTo(model, rowOf(model, fixture.innerA()), insideGroup);
        check(role(model, groupRow, gui::ObjectTreeModel::DropZoneRole).toInt() ==
                  gui::ObjectTreeModel::DropInto,
              "a folder's middle says INSIDE");
        model.endDrag(false);
        check(fixture.innerA()->parent() == fixture.group(), "a cancelled drag moves nothing");

        // a node cannot be dropped into its own subtree
        dragTo(model, rowOf(model, fixture.group()), insideGroup);
        model.endDrag(true);
        check(fixture.group()->parent() == fixture.top(), "a folder cannot be dropped into itself");

        // dropping BELOW the last row appends to the document
        const int last = model.rowCount() - 1;
        const qreal belowLast = model.rows()[last].top + model.rows()[last].height + 5;
        dragTo(model, rowOf(model, fixture.innerB()), belowLast);
        model.endDrag(true);
        check(!fixture.top()->children().empty() &&
                  fixture.top()->children().back() == fixture.innerB(),
              "a drop below the last row appends to the document");

        check(failures == before, "drag and drop");
    }

    // --- 7. delete, duplicate and add-base-values are each ONE step ---
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());

        // add playhead keys: one undo removes every channel it made
        ctrl::tree::addKeysAtPlayhead(fixture.project(), *fixture.leaf());
        check(fixture.leaf()->timeLine()->validTypeCount() > 1,
              "add defaults animated several channels");
        fixture.stack().undo();
        check(fixture.leaf()->timeLine()->validTypeCount() == 0,
              "ONE undo removes all of them (one command, not one per channel)");
        fixture.stack().redo();

        // duplicate a folder with keys inside: one undo takes the whole copy
        ctrl::tree::addKeysAtPlayhead(fixture.project(), *fixture.innerA());
        const int topChildren = int(fixture.top()->children().size());
        model.pressRow(rowOf(model, fixture.group()), 0);
        model.duplicateSelection();
        check(int(fixture.top()->children().size()) == topChildren + 1, "the copy landed beside it");
        const QVector<core::ObjectNode*> copies = model.selectedNodes();
        core::ObjectNode* copy = copies.size() == 1 ? copies.first() : nullptr;
        check(copy && copy->canHoldChild(), "the copy is a folder");
        check(copy && copy->children().size() == 2, "with both of its children");
        const bool keysCopied = copy && copy->children().size() == 2 &&
                                copy->children().front()->timeLine() &&
                                copy->children().front()->timeLine()->validTypeCount() > 1;
        check(keysCopied, "and their keys");
        if (copy)
            std::printf("  copy name: '%s'\n", qPrintable(copy->name()));
        check(copy && copy->name() == QStringLiteral("group (Copy)"), "and the copy's own name");
        fixture.stack().undo();
        check(int(fixture.top()->children().size()) == topChildren, "ONE undo removes the whole copy");

        // delete: one step for the whole selection
        model.selectAll();
        const int selected = model.selectionCount();
        check(selected >= 2, "several rows selected");
        model.deleteSelection();
        check(fixture.top()->children().size() == 0, "the selection was deleted");
        fixture.stack().undo();
        check(int(fixture.top()->children().size()) == topChildren, "ONE undo restores them all");
        check(failures == before, "actions");
    }

    // --- 7b. the indentation guides are the row list's own fact ------------------------
    {
        const int before = failures;
        // doc / A / A1 / B: A1 is the deepest row, and the level-1 trunk must continue through
        // it because A has a following sibling (B). Neither B nor the document row draws one.
        TestAnimator animator;
        core::Project project(QString(), animator, nullptr);
        auto* top = new core::FolderNode(QStringLiteral("doc"));
        project.objectTree().grabTopNode(top);
        auto* a = new core::FolderNode(QStringLiteral("A"));
        top->children().pushBack(a);
        auto* a1 = new core::LayerNode(QStringLiteral("A1"), project.objectTree().shaderHolder());
        a->children().pushBack(a1);
        auto* b = new core::LayerNode(QStringLiteral("B"), project.objectTree().shaderHolder());
        top->children().pushBack(b);

        gui::ObjectTreeModel model;
        model.setProject(&project);
        model.toggleExpanded(1); // A

        const auto maskOf = [&](const char* aName) {
            const QString name = QString::fromLatin1(aName);
            for (int i = 0; i < model.rowCount(); ++i) {
                if (model.data(model.index(i, 0), gui::ObjectTreeModel::NameRole).toString() == name)
                    return model.data(model.index(i, 0), gui::ObjectTreeModel::GuideMaskRole).toInt();
            }
            return -1;
        };
        check(namesAre(model, {"doc", "A", "A1", "B"}), "the guide fixture is doc/A/A1/B");
        check(maskOf("doc") == 0, "the document row draws no guide (nothing contains it)");
        check(maskOf("A") == 1,
              QStringLiteral("A draws ONE line: the level of the document it sits in (%1)")
                  .arg(maskOf("A")));
        check(maskOf("A1") == 3,
              QStringLiteral("A1 draws TWO: the document's and its own folder's (%1)")
                  .arg(maskOf("A1")));
        check(maskOf("B") == 1,
              QStringLiteral("B draws one as well - the line ends with the SUBTREE, not with a "
                             "sibling (%1)").arg(maskOf("B")));
        check(failures == before, "guides");
    }

    // --- 7c. the horizontal range covers the widest row ---------------------------------
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());
        const int narrow = model.contentWidth();
        check(narrow > 0, "a project has a content width");

        // a deep, long-named node widens it; the width follows the FONT, because it is measured
        // with the dock's own face
        auto* deep = fixture.addLayer(*fixture.group(), QStringLiteral("a very long layer name indeed"));
        model.toggleExpanded(rowOf(model, fixture.group()));
        const int wide = model.contentWidth();
        check(wide > narrow, QStringLiteral("a long name widens the range (%1 > %2)").arg(wide).arg(narrow));
        QFont big = QApplication::font();
        big.setPointSizeF(big.pointSizeF() * 2.0);
        model.setContentFont(big);
        check(model.contentWidth() > wide,
              QStringLiteral("and a bigger font widens it again (%1)").arg(model.contentWidth()));
        (void)deep;
        check(failures == before, "content width");
    }

    // --- 7d. the offset is CLAMPED here, because both directions pass through it ---------
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());
        model.setViewportHeight(40);           // the rows overflow: 3 rows, 40px of view
        const int maxOffset = model.contentHeight() - 40;
        check(maxOffset > 0, QStringLiteral("the fixture can scroll (%1px)").arg(maxOffset));
        model.setScrollOffset(5000);
        check(model.scrollOffset() == maxOffset,
              QStringLiteral("a 5000px request is clamped to %1 (%2)")
                  .arg(maxOffset).arg(model.scrollOffset()));
        model.setScrollOffset(-10);
        check(model.scrollOffset() == 0, "and a negative one to 0");
        // a viewport that grows shrinks the range, and the reported offset follows
        model.setScrollOffset(maxOffset);
        model.setViewportHeight(1000);
        check(model.scrollOffset() == 0, "a taller viewport pulls the offset back in range");
        check(failures == before, "offset clamp");
    }

    // --- 8. the rows the timeline receives are the rows the tree shows ---
    {
        const int before = failures;
        Fixture fixture;
        gui::ObjectTreeModel model;
        model.setProject(&fixture.project());
        ctrl::tree::addKeysAtPlayhead(fixture.project(), *fixture.leaf());
        model.toggleExpanded(rowOf(model, fixture.group()));

        // What TimeLineEditorWidget::setRows does with the contract: skip the document row and
        // push every other one with its CONTENT range. The editor's TimeLineRow then computes
        // its own summary/own split from that range, so the two halves agree by construction -
        // asserted here so a change to either side is caught.
        int pushed = 0;
        int expectedTop = 0;
        for (const gui::AnimationRow& row : model.rows()) {
            check(row.top == expectedTop, "every row starts where the previous one ended");
            expectedTop += row.height;
            if (row.isDocument) {
                check(row.height == ctrl::TimeLineRow::kHeight, "the document row is the header's height");
                continue;
            }
            ctrl::TimeLineRow lane(row.node, QRect(0, row.top, 1, row.height), row.closedFolder, false);
            check(lane.rect.top() == row.top && lane.rect.height() == row.height,
                  "the lane takes the row's own content range");
            check(lane.summaryHeight() == (row.closedFolder ? ctrl::TimeLineRow::kFolderSummaryHeight : 0),
                  "a closed folder's summary band is inside that range");
            check(lane.ownRect().height() + lane.summaryHeight() == row.height,
                  "and the row's own lanes take the rest");
            ++pushed;
        }
        check(pushed == model.rowCount() - 1, "every non-document row is a lane");
        check(failures == before, "row contract");
    }

    std::printf("%s: %d check(s), %d failure(s)\n", failures ? "FAILED" : "OK", checks, failures);
    return failures ? 1 : 0;
}
