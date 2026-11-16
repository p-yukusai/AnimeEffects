#ifndef CORE_KEYID_H
#define CORE_KEYID_H

namespace core {

// Stable key identity shared by TimeKey and ChannelKey. Ids start at 1 so 0 can
// keep meaning "no bound" in a proportional state; they saturate at INT_MAX
// instead of overflowing into negative ids.
int takeKeyId();
void reserveKeyId(int aValue);

} // namespace core

#endif // CORE_KEYID_H
