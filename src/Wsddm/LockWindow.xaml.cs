using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Win32;

namespace Wsddm;

public partial class LockWindow : Window
{
    private const uint ES_CONTINUOUS = 0x80000000;
    private const uint ES_SYSTEM_REQUIRED = 0x00000001;
    private const uint ES_DISPLAY_REQUIRED = 0x00000002;

    [DllImport("kernel32.dll")]
    private static extern uint SetThreadExecutionState(uint esFlags);

    private readonly Config _cfg;
    private readonly DispatcherTimer _tick;
    private bool _closed;

    public LockWindow(Config cfg, BitmapSource? blurBackground, Color accent)
    {
        _cfg = cfg;
        InitializeComponent();

        Left = SystemParameters.VirtualScreenLeft;
        Top = SystemParameters.VirtualScreenTop;
        Width = SystemParameters.VirtualScreenWidth;
        Height = SystemParameters.VirtualScreenHeight;
        WindowStartupLocation = WindowStartupLocation.Manual;

        if (blurBackground is null)
            BlurBg.Visibility = Visibility.Collapsed;
        else
            BlurBg.Source = blurBackground;

        BuildPalette(accent);
        BuildAvatar(accent);
        ConfigureUnlockMode();

        _tick = new DispatcherTimer { Interval = TimeSpan.FromSeconds(0.5) };
        _tick.Tick += (_, _) => UpdateClock();
        _tick.Start();
        UpdateClock();
    }

    private void BuildPalette(Color accent)
    {
        var dark = Wallpaper.Luminance(accent) < 128;
        Color fg = dark
            ? Color.FromRgb(0xF2, 0xF3, 0xF5)
            : Color.FromRgb(0x18, 0x1A, 0x20);

        byte compAlpha = (byte)Math.Clamp(255 * _cfg.MainCardComponentsOpacity, 0, 255);
        var textBrush = new SolidColorBrush(Color.FromArgb(compAlpha, fg.R, fg.G, fg.B));

        BgTint.Fill = new SolidColorBrush(dark
            ? Color.FromArgb(180, 8, 9, 14)
            : Color.FromArgb(120, 235, 236, 243));

        var cardColor = dark
            ? Color.FromArgb((byte)Math.Clamp(240 * _cfg.MainCardColorOpacity, 0, 255), 26, 28, 38)
            : Color.FromArgb((byte)Math.Clamp(245 * _cfg.MainCardColorOpacity, 0, 255), 250, 250, 253);
        Card.Background = new SolidColorBrush(cardColor);

        var pillColor = dark
            ? Color.FromArgb((byte)Math.Clamp(200 * _cfg.WelcomeColorOpacity, 0, 255), 26, 28, 38)
            : Color.FromArgb((byte)Math.Clamp(205 * _cfg.WelcomeColorOpacity, 0, 255), 250, 250, 253);
        WelcomePill.Background = new SolidColorBrush(pillColor);

        var pillText = new SolidColorBrush(Color.FromArgb((byte)Math.Clamp(255 * _cfg.WelcomeColorOpacity, 0, 255), fg.R, fg.G, fg.B));
        WelcomeTitle.Foreground = pillText;
        WelcomeMsg.Foreground = pillText;

        ClockText.Foreground = textBrush;
        DateText.Foreground = textBrush;
        UserText.Foreground = textBrush;
        HintText.Foreground = textBrush;
        PassBox.Foreground = textBrush;
        PassBox.Background = new SolidColorBrush(Color.FromArgb(60, fg.R, fg.G, fg.B));
        ErrorText.Foreground = new SolidColorBrush(Color.FromRgb(0xFF, 0x8A, 0x70));
    }

    private void BuildAvatar(Color accent)
    {
        var sid = WindowsIdentity.GetCurrent().User?.Value;
        var imgPath = sid is null ? null : Registry.GetValue(
            $@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\AccountPicture\Users\{sid}",
            "Image192", null) as string;

        if (imgPath is not null && File.Exists(imgPath))
        {
            try
            {
                var image = new BitmapImage(new Uri(imgPath!));
                Avatar.Fill = new ImageBrush(image) { Stretch = Stretch.UniformToFill };
                AvatarInitial.Visibility = Visibility.Collapsed;
                return;
            }
            catch
            {
            }
        }

        Avatar.Fill = new SolidColorBrush(
            Color.FromArgb((byte)Math.Clamp(255 * _cfg.MainCardColorOpacity, 0, 255), accent.R, accent.G, accent.B));
        AvatarInitial.Text = Environment.UserName.Length > 0
            ? Environment.UserName[..1].ToUpperInvariant()
            : "?";
    }

    private void ConfigureUnlockMode()
    {
        UserText.Text = Environment.UserName;
        WelcomeMsg.Text = Greeting(DateTime.Now);
        WelcomePill.Visibility = _cfg.EnableWelcomeMessage ? Visibility.Visible : Visibility.Collapsed;

        if (_cfg.AuthMode != "dismiss")
        {
            PassBox.Visibility = Visibility.Visible;
            _ = PassBox.Focus();
        }
        else
        {
            PassBox.Visibility = Visibility.Collapsed;
            HintText.Text = "Press any key to unlock";
            HintText.Visibility = Visibility.Visible;
        }
    }

    private static string Greeting(DateTime now) => now.Hour switch
    {
        < 12 => "Good morning",
        < 18 => "Good afternoon",
        _ => "Good evening"
    };

    private void UpdateClock()
    {
        var now = DateTime.Now;
        ClockText.Text = _cfg.Ap ? now.ToString("h:mm tt") : now.ToString("HH:mm");
        DateText.Text = now.ToString("dddd, MMMM d");
    }

    private void OnKeyDown(object sender, KeyEventArgs e)
    {
        if (_closed) return;

        if (_cfg.AuthMode == "dismiss")
        {
            UnlockNow();
            return;
        }

        if (e.Key == Key.Escape)
        {
            e.Handled = true;
            ErrorText.Text = "Enter your Windows password to unlock.";
            ErrorText.Visibility = Visibility.Visible;
        }
    }

    private void OnPassKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            e.Handled = true;
            AttemptPasswordUnlock();
        }
    }

    private void AttemptPasswordUnlock()
    {
        var password = PassBox.Password;
        if (password.Length == 0)
        {
            ErrorText.Text = "Password required.";
            ErrorText.Visibility = Visibility.Visible;
            PassBox.Focus();
            return;
        }

        if (Unlock.Validate(password))
        {
            UnlockNow();
            return;
        }

        ErrorText.Text = "Incorrect password. Try again.";
        ErrorText.Visibility = Visibility.Visible;
        PassBox.Clear();
        PassBox.Focus();
    }

    private void UnlockNow()
    {
        _closed = true;
        Close();
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
        Activate();
        FocusPassword();
    }

    private void OnClosed(object sender, EventArgs e)
    {
        if (!_closed) _closed = true;
        SetThreadExecutionState(ES_CONTINUOUS);
        _tick.Stop();
    }

    private void OnDeactivated(object sender, EventArgs e)
    {
        if (_closed) return;
        Dispatcher.BeginInvoke(() =>
        {
            if (!_closed && IsVisible)
            {
                Activate();
                FocusPassword();
            }
        });
    }

    public void FocusPassword()
    {
        if (_cfg.AuthMode != "dismiss")
        {
            _ = PassBox.Focus();
            Keyboard.Focus(PassBox);
        }
    }
}