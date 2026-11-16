// Tests the animation-settings ViewModel's session undo/redo: a discrete edit
// records one step, a continuous scrub (beginGesture/endGesture) coalesces into
// one step, undo/redo step the draft, and reset() drops the history per open.
// This is the "popup owns its undo; the model gets one step on close" contract.
//
// Run: QT_QPA_PLATFORM=offscreen ./anim_settings_undo
#include <QCoreApplication>
#include <cstdio>
#include "gui/qml/AnimationSettingViewModel.h"

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                                   \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (false)

namespace {

int run() {
    gui::AnimationSettingViewModel vm(1, 100, 24, false);

    // ---- discrete edit records one step; undo/redo walk it ----
    vm.setFrameStart(50);
    CHECK(vm.frameStart() == 50);
    vm.undo();
    CHECK(vm.frameStart() == 1);   // back to the pre-edit state
    vm.redo();
    CHECK(vm.frameStart() == 50);

    // ---- continuous scrub coalesces into ONE step ----
    vm.reset(1, 100, 24, false);
    vm.beginGesture();   // press
    vm.setFrameStart(10);
    vm.setFrameStart(20);
    vm.setFrameStart(30);
    vm.endGesture();     // release
    CHECK(vm.frameStart() == 30);
    vm.undo();
    CHECK(vm.frameStart() == 1);   // whole drag reverted to pre-drag state
    vm.undo();                     // nothing left -> no-op
    CHECK(vm.frameStart() == 1);
    vm.redo();
    CHECK(vm.frameStart() == 30);

    // ---- a press-without-move records nothing ----
    vm.reset(1, 100, 24, false);
    vm.beginGesture();
    vm.endGesture();               // no state change
    vm.undo();                     // no-op
    CHECK(vm.frameStart() == 1);

    // ---- reset() drops the history (a fresh open starts clean) ----
    vm.reset(1, 100, 24, false);
    vm.setFps(30);
    vm.reset(1, 100, 24, false);   // reopen at a new baseline
    vm.undo();                     // history was cleared -> no-op
    CHECK(vm.fps() == 24);
    CHECK(vm.frameStart() == 1);

    // ---- undo/redo emit the change signals so a live owner can follow ----
    int liveFires = 0;
    vm.reset(1, 100, 24, false);
    QObject::connect(&vm, &gui::AnimationSettingViewModel::frameStartChanged, [&]() { ++liveFires; });
    vm.setFrameStart(7);
    const int afterSet = liveFires;
    vm.undo();
    CHECK(vm.frameStart() == 1);
    CHECK(liveFires > afterSet);   // undo re-notifies (live re-sync follows the draft)

    std::printf("OK: animation-settings session undo passes\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return run();
}
