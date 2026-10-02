#pragma once

#include <QObject>
#include <QString>

/// The `sddm` context object the theme calls into.
///
/// SDDM provides this to the theme; here it stands in for it, so the theme's own
/// QML needs no changes. Only three calls and three signals are used by locklike.
class Sddm : public QObject
{
    Q_OBJECT

public:
    explicit Sddm(QObject *parent = nullptr);

    /// Verifies a password and, on success, dismisses the lock screen.
    Q_INVOKABLE void login(const QString &user, const QString &password, int sessionIndex);

    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void reboot();

    /// Called by the theme from the end of its success animation so the host knows
    /// the overlay can be hidden. Emits unlockConfirmed().
    Q_INVOKABLE void finishUnlock();

private:
    /// Checks the password with LogonUser against the local SAM / machine account.
    static bool validateViaOs(const QString &password);

signals:
    /// Emitted after a successful verification; the theme plays its exit animation.
    void loginSucceeded();

    /// Emitted on a wrong password; the theme shakes the field and clears the buffer.
    void loginFailed();

    /// Emitted from finishUnlock(); the host drops the overlay.
    void unlockConfirmed();
};
