// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#pragma once
#include <windows.h>
#include "common/ipc.h"
#include "host/render/genie_effect.h"

class Renderer;
class SettingsStore;
class TaskbarLocator;
struct GenieJob;

// Handles AnimRequests from the DLLs (WM_COPYDATA on the host's main thread). Only
// cheap work happens here - handle duplication, the snapshot upload, geometry - so the
// requesting app thread (blocked in SendMessageTimeout) is released within ~1-3 ms.
class IpcServer {
public:
    IpcServer(Renderer& r, SettingsStore& s, TaskbarLocator& l) : renderer_(r), settings_(s), locator_(l) {}
    LRESULT Handle(const COPYDATASTRUCT* cds);

private:
    uint32_t HandleAnim(const AnimRequest& req);
    bool UploadSnapshot(HANDLE hSection, GenieJob* job, int rawW, int rawH);
    void Dispatch(GenieJob* job, const DockTarget& dock);

    Renderer& renderer_;
    SettingsStore& settings_;
    TaskbarLocator& locator_;
};
