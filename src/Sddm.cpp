#include "Sddm.h"

#include <QDebug>

#include <windows.h>

Sddm::Sddm(QObject *parent)
    : QObject(parent)
{
}

void Sddm::login(const QString &user, const QString &password, int sessionIndex)
{
    // Password verification is not wired up yet; this step exists to prove the theme
    // renders and reacts to the host. Anything typed is reported as a failed login,
    // which is also the correct answer for a Microsoft account: the local account
    // store holds an opaque credential, so the OS cannot check the password.
    qWarning() << "wsddm: login not implemented yet for user" << user
               << "(session" << sessionIndex << "), reporting failure";
    Q_UNUSED(password)

    emit loginFailed();
}

void Sddm::powerOff()
{
    qInfo() << "wsddm: powering off";
    ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0);
}

void Sddm::reboot()
{
    qInfo() << "wsddm: rebooting";
    ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);
}
