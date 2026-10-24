// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

namespace MacWin.Settings;

/// <summary>A .lnk shortcut through the shell's own IShellLink: its target, arguments and
/// icon, read and written exactly as Explorer does.</summary>
internal sealed class ShellLink : IDisposable
{
    private readonly IShellLinkW _link;
    private readonly string _path;

    private ShellLink(IShellLinkW link, string path)
    {
        _link = link;
        _path = path;
    }

    /// <summary>`write` for a shortcut whose icon will be changed (else read only: shortcuts
    /// of all users can be read without administrator rights).</summary>
    public static ShellLink Open(string path, bool write = false)
    {
        var link = (IShellLinkW)new CShellLink();
        ((IPersistFile)link).Load(path, write ? 0x00000002 /* STGM_READWRITE */ : 0 /* STGM_READ */);
        return new ShellLink(link, path);
    }

    public string Target
    {
        get
        {
            var sb = new StringBuilder(1024);
            _link.GetPath(sb, sb.Capacity, IntPtr.Zero, 0);
            return sb.ToString();
        }
    }

    public string Arguments
    {
        get
        {
            var sb = new StringBuilder(4096);
            _link.GetArguments(sb, sb.Capacity);
            return sb.ToString();
        }
    }

    /// <summary>"path,index", as the shortcut's properties show it; "" for the target's own.</summary>
    public string IconLocation
    {
        get
        {
            var sb = new StringBuilder(1024);
            _link.GetIconLocation(sb, sb.Capacity, out int index);
            return sb.Length == 0 ? "" : $"{sb},{index}";
        }
        set
        {
            int comma = value.LastIndexOf(',');
            string file = comma > 0 ? value[..comma] : value;
            int index = comma > 0 && int.TryParse(value[(comma + 1)..], out int i) ? i : 0;
            _link.SetIconLocation(file, index);
        }
    }

    public void Save() => ((IPersistFile)_link).Save(_path, true);

    public void Dispose() => Marshal.ReleaseComObject(_link);

    [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
    private class CShellLink
    {
    }

    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("000214F9-0000-0000-C000-000000000046")]
    private interface IShellLinkW
    {
        void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder file, int max, IntPtr findData, uint flags);
        void GetIDList(out IntPtr pidl);
        void SetIDList(IntPtr pidl);
        void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder name, int max);
        void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
        void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder dir, int max);
        void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string dir);
        void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder args, int max);
        void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string args);
        void GetHotkey(out short hotkey);
        void SetHotkey(short hotkey);
        void GetShowCmd(out int showCmd);
        void SetShowCmd(int showCmd);
        void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder path, int max, out int index);
        void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string path, int index);
        void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, uint reserved);
        void Resolve(IntPtr hwnd, uint flags);
        void SetPath([MarshalAs(UnmanagedType.LPWStr)] string file);
    }
}
