using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Wsddm;

/// <summary>
/// WSDDM configuration. Mirrors the caelestia-sddm "locklike" theme.conf
/// keys so the theme language stays portable to a future Qt/QML port.
/// Stored at ~/.config/wsddm/config.json
/// </summary>
public sealed class Config
{
    public string Hotkey { get; set; } = "Win+Shift+L";

    // "password" validates against the OS account via LogonUser.
    // "dismiss" closes the overlay on any key / Esc.
    public string AuthMode { get; set; } = "password";

    public bool AutoStart { get; set; } = true;

    // -- caelestia-locklike theme.conf keys --
    public bool EnableWelcomeMessage { get; set; } = true;

    // caelestia: ap=true means 12-hour clock
    public bool Ap { get; set; } = false;

    public double WelcomeBgBlurAmount { get; set; } = 0.7;
    public double WelcomeColorOpacity { get; set; } = 0.7;
    public double MainCardBlurAmount { get; set; } = 0.9;
    public double MainCardColorOpacity { get; set; } = 0.9;
    public double MainCardComponentsOpacity { get; set; } = 0.9;
    public bool WelcomeBgBlur { get; set; } = true;
    public bool MainCardBgBlur { get; set; } = true;

    public static string ConfigPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config", "wsddm", "config.json");

    public static Config Load()
    {
        try
        {
            if (File.Exists(ConfigPath))
            {
                var cfg = JsonSerializer.Deserialize<Config>(
                    File.ReadAllText(ConfigPath),
                    new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
                if (cfg is not null) return cfg;
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"WSDDM: failed to read {ConfigPath}: {ex.Message}");
        }

        var fresh = new Config();
        Save(fresh);
        return fresh;
    }

    public static void Save(Config cfg)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(ConfigPath)!);
            File.WriteAllText(ConfigPath,
                JsonSerializer.Serialize(cfg, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"WSDDM: failed to write {ConfigPath}: {ex.Message}");
        }
    }
}