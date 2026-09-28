using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;

namespace Wsddm;

/// <summary>
/// Reads the current per-user wallpaper (HKCU\Control Panel\Desktop\WallPaper)
/// and derives the pieces caelestia-locklike needs: a blurred backdrop and a
/// wallpaper-driven accent color.
/// </summary>
public static class Wallpaper
{
    public static string? GetPath()
    {
        var value = Registry.GetValue(
            @"HKEY_CURRENT_USER\Control Panel\Desktop", "WallPaper", "") as string;
        return string.IsNullOrWhiteSpace(value) ? null : value;
    }

    public static BitmapSource? Load(string? path)
    {
        if (string.IsNullOrEmpty(path) || !File.Exists(path)) return null;
        try
        {
            return new BitmapImage(new Uri(path!));
        }
        catch
        {
            return null;
        }
    }

    /// <summary>
    /// Cheap, instant "blur": downscale the wallpaper ~8x and let WPF's linear
    /// scaling smooth it back up. Looks right for a blurred lock backdrop and
    /// costs nothing at runtime (no per-frame BlurEffect).
    /// </summary>
    public static BitmapSource Downscaled(BitmapSource src, double factor = 8.0)
    {
        var w = Math.Max(1, (int)(src.PixelWidth / factor));
        var h = Math.Max(1, (int)(src.PixelHeight / factor));
        var rtb = new RenderTargetBitmap(w, h, 96, 96, PixelFormats.Pbgra32);
        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen())
            dc.DrawImage(src, new Rect(0, 0, w, h));
        rtb.Render(dv);
        rtb.Freeze();
        return rtb;
    }

    /// <summary>Average color of the wallpaper, used for the accent + card tint.</summary>
    public static Color AverageColor(BitmapSource src)
    {
        const int w = 32, h = 18;
        var rtb = new RenderTargetBitmap(w, h, 96, 96, PixelFormats.Pbgra32);
        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen())
            dc.DrawImage(src, new Rect(0, 0, w, h));
        rtb.Render(dv);

        var pixels = new byte[w * h * 4];
        rtb.CopyPixels(pixels, w * 4, 0);

        long r = 0, g = 0, b = 0;
        for (int i = 0; i < pixels.Length; i += 4)
        {
            b += pixels[i];
            g += pixels[i + 1];
            r += pixels[i + 2];
        }
        int n = w * h;
        return Color.FromRgb((byte)(r / n), (byte)(g / n), (byte)(b / n));
    }

    public static double Luminance(Color c) =>
        0.299 * c.R + 0.587 * c.G + 0.114 * c.B;
}