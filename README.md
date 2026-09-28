# WSDDM

**W**indows **S**imple **D**esktop **D**isplay **M**anager - an SDDM-style
lock screen for Windows, inspired by
[caelestia-sddm](https://github.com/ItsABigIgloo/caelestia-sddm)'s
`locklike` theme.

## What it is (and what it isn't)

WSDDM is a user-mode, fullscreen **overlay lock**: a themed screen you summon
with a hotkey, and unlock with your Windows password.

It is **not** a replacement for the real Windows lock screen. The real one runs
on the secure desktop owned by `LogonUI.exe`, and `Win+L` / `Ctrl+Alt+Del` are
handled by the kernel - no non-admin program can intercept or replace them.
WSDDM is a **privacy lock**: it keeps casual onlookers out, and the real
secure lock (`Win+L`) still works and still beats it.

## Features

- caelestia-`locklike` look: blurred wallpaper backdrop, welcome message pill,
  big clock, avatar, translucent card
- wallpaper-driven colors (accent is sampled from your current wallpaper)
- unlock via your real Windows password, validated by the OS (`LogonUser`) -
  or a pure dismiss mode where any key unlocks
- global hotkey trigger (default `Win+Shift+L`) - the hotkey is configurable
  because `Win+L` itself cannot be captured user-mode
- system tray icon, autostart with Windows, per-user only, no admin required
- sleeps are suppressed while the lock is up

## Install

```powershell
scoop bucket add wsddm https://github.com/rdepa29/wsddm
scoop install wsddm
```

or build it yourself:

```powershell
.\publish.ps1          # -> dist\wsddm-win-x64.zip + dist\wsddm.exe
```

## Config

`%USERPROFILE%\.config\wsddm\config.json` - the keys mirror caelestia's
`theme.conf` so the theme language stays portable:

```json
{
  "Hotkey": "Win+Shift+L",
  "AuthMode": "password",          // or "dismiss" (any key unlocks)
  "AutoStart": true,
  "EnableWelcomeMessage": true,
  "Ap": false,                    // 12-hour clock when true
  "WelcomeBgBlurAmount": 0.7,
  "WelcomeColorOpacity": 0.7,
  "MainCardBlurAmount": 0.9,
  "MainCardColorOpacity": 0.9,
  "MainCardComponentsOpacity": 0.9,
  "WelcomeBgBlur": true,
  "MainCardBgBlur": true
}
```

## Security notes

- Passwords are handed to the OS via `LogonUser` and never stored, logged or
  transmitted anywhere. WSDDM does not ship the typed string to disk.
- It is not a cryptographic or secure-desktop lock. Do not use it as your only
  protection.
- `dismiss` mode accepts any key - it is a screensaver, nothing more.

## Future

Planned port to C++/Qt/QML (the actual SDDM stack, so real caelestia QML themes
can run unmodified). The theme is kept data-driven for that reason.

## License

GPL-3.0 - the UI is a port of the caelestia-sddm `locklike` theme, which is
GPL-3.0. Credits to [ItsABigIgloo/caelestia-sddm](https://github.com/ItsABigIgloo/caelestia-sddm).
