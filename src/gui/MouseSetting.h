#ifndef GUI_MOUSESETTING_H
#define GUI_MOUSESETTING_H

namespace gui {

class MouseSetting {
public:
    MouseSetting();

    bool operator==(const MouseSetting& aRhs) const;
    bool operator!=(const MouseSetting& aRhs) const { return !(*this == aRhs); }

    void load();
    void save();
    // ONE switch for every wheel-zoom surface (viewport, timeline, graph page): a wheel is one
    // habit, and having the three disagree is a trap, not an option. false = the convention (wheel
    // up zooms IN, as in Photoshop, Krita and a browser); true reverses it.
    bool invertZoom;
    bool middleMouseMoveCanvas;
};

} // namespace gui

#endif // GUI_MOUSESETTING_H
