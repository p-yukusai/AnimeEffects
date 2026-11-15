#ifndef CORE_TIMEINFO_H
#define CORE_TIMEINFO_H

#include "core/Frame.h"
#include "util/Range.h"

namespace core {

class TimeInfo {
public:
    TimeInfo(): fps(0), frameRange(), loop(false), frame() {}

    int fps;
    util::Range frameRange; // the project's frame range (inclusive)
    bool loop;
    Frame frame;
};

} // namespace core

#endif // CORE_TIMEINFO_H
