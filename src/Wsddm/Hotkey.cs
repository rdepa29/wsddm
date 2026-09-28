using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace Wsddm;

/// <summary>
/// Global hotkey support (the WSDDM trigger; default Win+Shift+L).
/// Win+L itself is kernel/secure-desktop handled and cannot be captured
/// by a user-mode app, hence a different configurable combo.
/// </summary>
public sealed class Hotkey
{
    public const int WM_HOTKEY = 0x0312;

    private const uint MOD_ALT = 0x0001;
    private const uint MOD_CONTROL = 0x0002;
    private const uint MOD_SHIFT = 0x0004;
    private const uint MOD_WIN = 0x0008;
    private const uint MOD_NOREPEAT = 0x4000;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    private readonly int _id;
    private readonly HwndSource? _source;
    private bool _registered;
    private readonly HwndSourceHook _hook;

    public event Action? Pressed;

    public int Id => _id;

    private Hotkey(int id, HwndSource source)
    {
        _id = id;
        _source = source;
        _hook = WndProc;
    }

    public static Hotkey? Register(Window messageWindow, int id, string spec, out string? error)
    {
        error = null;

        if (!TryParse(spec, out uint mods, out uint vk))
        {
            error = $"invalid hotkey '{spec}' (use e.g. Win+Shift+L).";
            return null;
        }

        var source = (HwndSource?)PresentationSource.FromVisual(messageWindow);
        if (source is null)
        {
            error = "message window handle not ready.";
            return null;
        }

        var hk = new Hotkey(id, source);
        source.AddHook(hk._hook);

        if (!RegisterHotKey(source.Handle, id, mods | MOD_NOREPEAT, vk))
        {
            int err = Marshal.GetLastWin32Error();
            error = err == 1409
                ? $"hotkey '{spec}' is already taken by another program."
                : $"failed to register hotkey '{spec}' (error {err}).";
            source.RemoveHook(hk._hook);
            return null;
        }

        hk._registered = true;
        return hk;
    }

    public void Unregister()
    {
        if (_registered && _source is not null)
        {
            UnregisterHotKey(_source.Handle, _id);
            _source.RemoveHook(_hook);
            _registered = false;
        }
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WM_HOTKEY && wParam == (IntPtr)_id)
        {
            Pressed?.Invoke();
            handled = true;
        }
        return IntPtr.Zero;
    }

    public static bool TryParse(string spec, out uint mods, out uint vk)
    {
        mods = 0;
        vk = 0;
        if (string.IsNullOrWhiteSpace(spec)) return false;

        foreach (var raw in spec.Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var part = raw.ToUpperInvariant();
            switch (part)
            {
                case "WIN": mods |= MOD_WIN; continue;
                case "CTRL" or "CONTROL": mods |= MOD_CONTROL; continue;
                case "SHIFT": mods |= MOD_SHIFT; continue;
                case "ALT": mods |= MOD_ALT; continue;
            }

            if (!ParseKey(part, out uint key)) return false;
            vk = key;
        }

        return vk != 0;

        static bool ParseKey(string part, out uint key)
        {
            key = 0;
            if (part.Length == 1 && char.IsLetterOrDigit(part[0]))
            {
                key = part[0];
                return true;
            }

            key = part switch
            {
                "F1" => 0x70, "F2" => 0x71, "F3" => 0x72, "F4" => 0x73,
                "F5" => 0x74, "F6" => 0x75, "F7" => 0x76, "F8" => 0x77,
                "F9" => 0x78, "F10" => 0x79, "F11" => 0x7A, "F12" => 0x7B,
                "F13" => 0x7C, "F14" => 0x7D, "F15" => 0x7E, "F16" => 0x7F,
                "F17" => 0x80, "F18" => 0x81, "F19" => 0x82, "F20" => 0x83,
                "F21" => 0x84, "F22" => 0x85, "F23" => 0x86, "F24" => 0x87,
                "SPACE" => 0x20, "ESC" or "ESCAPE" => 0x1B, "TAB" => 0x09,
                "BACK" or "BACKSPACE" => 0x08, "ENTER" or "RETURN" => 0x0D,
                "UP" => 0x26, "DOWN" => 0x28, "LEFT" => 0x25, "RIGHT" => 0x27,
                "HOME" => 0x24, "END" => 0x23, "PAGEUP" or "PGUP" => 0x21,
                "PAGEDOWN" or "PGDN" => 0x22, "DELETE" or "DEL" => 0x2E,
                "CAPS" or "CAPSLOCK" => 0x14, "INSERT" or "INS" => 0x2D,
                _ => 0
            };
            return key != 0;
        }
    }
}