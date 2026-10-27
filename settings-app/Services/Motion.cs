// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;

namespace MacWin.Settings;

/// <summary>Windows' "Animation effects" (Accessibility > Visual effects): with them off,
/// this window moves nothing either - pages, menus and sliders change at once.</summary>
internal static class Motion
{
    public static bool On => SystemParameters.ClientAreaAnimation;
}
