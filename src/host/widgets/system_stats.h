// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include "host/widgets/widget_draw.h"

namespace widgets {

// Processor, memory, Windows drive and battery for the System and Battery widgets, and
// the network's speed for the Network widget.
class SystemSampler {
public:
    // The processor load and the network speed are the averages since the previous call.
    SystemStats Sample();

private:
    void SampleNetwork(SystemStats* s);

    ULONGLONG idle_ = 0, total_ = 0;
    int cpu_ = 0;
    ULONG interface_ = 0;   // the adapter the counters below belong to
    ULONGLONG received_ = 0, sent_ = 0, networkAt_ = 0;
    float downHistory_[kNetworkHistory] = {};
    float upHistory_[kNetworkHistory] = {};
};

}  // namespace widgets
