#pragma once

#include <QString>

/// File logging for the GUI-subsystem executable.
///
/// wsddm is built as a WIN32 app so no console appears when the tray icon runs, which
/// also means qDebug/qWarning/qCritical have nowhere to go: they end up in the debugger
/// rather than anywhere a user can read. The QML loader reports parse errors and missing
/// context objects this way, so without a sink those failures are silent.
namespace Log {

/// Installs the Qt message handler. Safe to call before QGuiApplication exists.
void install();

/// Absolute path of the log file.
QString path();

} // namespace Log
