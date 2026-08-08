#ifndef NETCHANGE_WATCH_H
#define NETCHANGE_WATCH_H

#include <functional>

bool StartNetworkChangeWatch(std::function<void()> onChange);

void StopNetworkChangeWatch();

#endif
