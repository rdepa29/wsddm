#pragma once

#include <QJsonObject>
#include <QString>

/// Reads and writes %USERPROFILE%\.config\wsddm\config.json.
///
/// The file carries several families of settings (tray hotkey, theme colours, and the
/// local password verifier). Only the keys each component needs are touched; everything
/// else is preserved exactly, so C# and C++ builds can share the same config file.
class Config
{
public:
    /// The config file location, matching the archived C# build.
    static QString configPath();

    /// Loads the config; an absent or unreadable file yields an empty object.
    static QJsonObject load();

    /// Writes the whole object back; creates the .config/wsddm directory as needed.
    static bool save(const QJsonObject &object);
};