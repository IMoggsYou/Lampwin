// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include "host/widgets/widget_draw.h"

namespace widgets {

// Processor, memory, Windows drive and battery for the System widget.
class SystemSampler {
public:
    // The processor load is the average since the previous call.
    SystemStats Sample();

private:
    ULONGLONG idle_ = 0, total_ = 0;
    int cpu_ = 0;
};

}  // namespace widgets
