using System.Diagnostics;
using System.Drawing;
using System.Windows;
using System.Windows.Interop;
using Microsoft.Win32;
using Forms = System.Windows.Forms;

namespace Wsddm;

public partial class App : Application
{
    private const int HotkeyId = 0x5753; // "WS"
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

    private Mutex? _mutex;
    private Config? _cfg;
    private Hotkey? _hotkey;
    private Forms.NotifyIcon? _tray;
    private bool _locked;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        ShutdownMode = ShutdownMode.OnExplicitShutdown;

        _mutex = new Mutex(true, "WSDDM_SingleInstance", out bool first);
        if (!first)
        {
            Shutdown();
            return;
        }

        _cfg = Config.Load();
        ApplyAutoStart();

        SetupTray();
        SetupMessageWindow();
    }

    private void SetupMessageWindow()
    {
        var msg = new System.Windows.Window
        {
            Width = 0,
            Height = 0,
            ShowInTaskbar = false,
            WindowStyle = WindowStyle.None,
            Left = -32000,
            Top = -32000
        };

        msg.SourceInitialized += (_, _) =>
        {
            _hotkey = Hotkey.Register(msg, HotkeyId, _cfg!.Hotkey, out var err);
            if (_hotkey is null)
            {
                Notify(err!);
            }
            else
            {
                _hotkey.Pressed += () => _ = LockNow();
            }
        };

        msg.Show();
        msg.Hide();
        _ = msg; // keep alive
    }

    private void SetupTray()
    {
        _tray = new Forms.NotifyIcon
        {
            Icon = BuildTrayIcon(),
            Text = "WSDDM - an SDDM-style lock screen for Windows",
            Visible = true
        };

        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("Lock now", null, (_, _) => _ = LockNow());
        menu.Items.Add("Edit config", null, (_, _) =>
            Process.Start(new ProcessStartInfo("notepad.exe", Config.ConfigPath) { UseShellExecute = true }));
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("Quit", null, (_, _) => Shutdown());
        _tray.ContextMenuStrip = menu;

        _tray.MouseClick += (_, e) =>
        {
            if (e.Button == Forms.MouseButtons.Left) _ = LockNow();
        };
    }

    private async System.Threading.Tasks.Task LockNow()
    {
        if (_locked) return;
        _locked = true;
        try
        {
            var path = Wallpaper.GetPath();
            var src = Wallpaper.Load(path);
            var accent = src is null
                ? System.Windows.Media.Color.FromRgb(0x2A, 0xAA, 0xFF)
                : Wallpaper.AverageColor(src);
            var blur = src is null ? null : Wallpaper.Downscaled(src);

            var win = new LockWindow(_cfg!, blur, accent);
            win.Closed += (_, _) => _locked = false;
            win.Show();
            win.Activate();
            await System.Threading.Tasks.Task.CompletedTask;
        }
        catch (Exception ex)
        {
            _locked = false;
            Notify($"Failed to show lock screen: {ex.Message}");
        }
    }

    private void ApplyAutoStart()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKey, writable: true);
            if (key is null) return;
            if (_cfg!.AutoStart)
                key.SetValue("WSDDM", Environment.ProcessPath!);
            else if (key.GetValue("WSDDM") is not null)
                key.DeleteValue("WSDDM", false);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"WSDDM: autostart error: {ex.Message}");
        }
    }

    private void Notify(string message)
    {
        if (_tray is null) return;
        _tray.BalloonTipTitle = "WSDDM";
        _tray.BalloonTipText = message;
        _tray.ShowBalloonTip(4000);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _hotkey?.Unregister();
        _tray?.Dispose();
        base.OnExit(e);
    }

    private static System.Drawing.Icon BuildTrayIcon()
    {
        using var bmp = new Bitmap(16, 16);
        using (var g = Graphics.FromImage(bmp))
        {
            g.Clear(Color.Transparent);
            using var ring = new SolidBrush(Color.FromArgb(0x2A, 0xAA, 0xFF));
            g.FillEllipse(ring, 0, 0, 16, 16);
            using var dot = new SolidBrush(Color.White);
            g.FillEllipse(dot, 5, 5, 6, 6);
        }
        return System.Drawing.Icon.FromHandle(bmp.GetHicon());
    }
}