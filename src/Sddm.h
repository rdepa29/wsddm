#pragma once

#include <QObject>
#include <QString>

/// The `sddm` context object the theme calls into.
///
/// SDDM provides this to the theme; here it stands in for it, so the theme's own
/// QML needs no changes. Only three calls and two signals are used by locklike.
class Sddm : public QObject
{
    Q_OBJECT

public:
    explicit Sddm(QObject *parent = nullptr);

    /// Verifies a password and, on success, dismisses the lock screen.
    Q_INVOKABLE void login(const QString &user, const QString &password, int sessionIndex);

    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void reboot();

signals:
    /// Emitted after a successful verification; the theme clears its loading state.
    void loginSucceeded();

    /// Emitted on a wrong password; the theme shakes the field and clears the buffer.
    void loginFailed();
};
