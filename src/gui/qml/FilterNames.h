#ifndef GUI_QML_FILTERNAMES_H
#define GUI_QML_FILTERNAMES_H

#include <QCoreApplication>
#include <QString>

#include "core/TimeKeyType.h"

namespace gui {

// A FILTER LANE'S DISPLAY NAME, in ONE place.
//
// Three surfaces name the same lanes: the filter stack's cards, its catalog popup, and
// the mask tool's target chooser (a brush that writes a filter's mask has to say which
// filter). Two spellings of one name is the class of defect this tree keeps recording -
// a lane called "Wave" in one chooser and "Wave Deform" in the one beside it reads as
// two different filters.
//
// The context is the properties panel's own (`gui::prop::FilterStackPanel`), which is
// where these three strings were first extracted; keeping it is what keeps the five
// catalogs' translations attached to them.
inline QString filterDisplayName(core::TimeKeyType aType) {
    switch (aType) {
    case core::TimeKeyType_HSV:
        return QCoreApplication::translate("gui::prop::FilterStackPanel", "HSV");
    case core::TimeKeyType_Blur:
        return QCoreApplication::translate("gui::prop::FilterStackPanel", "Blur");
    case core::TimeKeyType_Wave:
        return QCoreApplication::translate("gui::prop::FilterStackPanel", "Wave Deform");
    default:
        return QString();
    }
}

} // namespace gui

#endif // GUI_QML_FILTERNAMES_H
