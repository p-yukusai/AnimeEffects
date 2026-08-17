#ifndef GUI_TOOL_TOOLSLIDER_H
#define GUI_TOOL_TOOLSLIDER_H

#include <QSlider>

namespace gui {
namespace tool {

    // Custom-painted horizontal slider for the tool dock option rows.
    //
    // QSS cannot draw a clean circular handle on a thin track: Qt's
    // subcontrol rendering caps border-radius below half the box, so any
    // radius >= half draws a plain rectangle instead. It also cannot be
    // trusted for the input side here: standard.ssa's QSlider rules leak
    // through the stylesheet cascade into this widget, which corrupts
    // QStyle's subControlRect (a 7px-wide groove), and QSlider's built-in
    // click mapping then disagrees with any sane drawing. So the slider is
    // fully self-contained: one value<->pixel mapping (the head center
    // travels the track minus its own diameter, i.e. the standard slider
    // feel) is used by both paintEvent and the mouse handlers, so the head
    // always sits exactly at its value and clicks always land where the
    // head is drawn.
    class ToolSlider : public QSlider {
    public:
        explicit ToolSlider(QWidget* aParent = nullptr);

        // Track thickness (px), per the design.
        static constexpr int kTrackHeight = 4;
        // Head diameter (px).
        static constexpr int kHeadDiameter = 16;

    protected:
        void paintEvent(QPaintEvent* aEvent) override;
        void mousePressEvent(QMouseEvent* aEvent) override;
        void mouseMoveEvent(QMouseEvent* aEvent) override;
        void mouseReleaseEvent(QMouseEvent* aEvent) override;
        void changeEvent(QEvent* aEvent) override;

    private:
        // Value of the click at pixel x (inverse of headCenter()).
        int valueFromPixel(int aX) const;
        // Horizontal center of the head for the current value.
        qreal headCenter() const;
    };

} // namespace tool
} // namespace gui

#endif // GUI_TOOL_TOOLSLIDER_H
