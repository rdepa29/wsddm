#include "Sddm.h"

#include "Config.h"
#include "PasswordStore.h"

#include <QDebug>
#include <QJsonObject>
#include <QSysInfo>

#include <windows.h>

Sddm::Sddm(QObject *parent)
    : QObject(parent)
{
}

void Sddm::login(const QString &user, const QString &password, int sessionIndex)
{
    Q_UNUSED(user)
    Q_UNUSED(sessionIndex)

    const bool accepted = PasswordStore::useLocalCheck()
        ? PasswordStore::verify(password)
        : validateViaOs(password);

    if (accepted) {
        qInfo("wsddm: unlocked");
        emit loginSucceeded();
    } else {
        emit loginFailed();
    }
}

bool Sddm::validateViaOs(const QString &password)
{
    // The signed-in account is the only one on the lock screen, so the username comes
    // from the session rather than the theme.
    wchar_t buffer[32768];
    DWORD size = _countof(buffer);
    if (!GetUserNameW(buffer, &size))
        return false;

    const QString fullName = QString::fromWCharArray(buffer, static_cast<int>(size));
    const bool hasDomain = fullName.contains(QLatin1Char('\\'));
    const QString bareName = hasDomain ? fullName.section(QLatin1Char('\\'), 1) : fullName;

    // "." is the shorthand for the local SAM; the machine name is also tried because it
    // resolves reliably on some domain-joined builds, mirroring the archived Unlock.cs.
    const QStringList candidates =
        hasDomain ? QStringList{QLatin1String(".")} : QStringList{QLatin1String("."), QSysInfo::machineHostName()};

    for (const QString &domain : candidates) {
        HANDLE token = nullptr;
        if (LogonUserW(reinterpret_cast<LPCWSTR>(bareName.utf16()),
                       reinterpret_cast<LPCWSTR>(domain.utf16()),
                       reinterpret_cast<LPCWSTR>(password.utf16()),
                       LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token)) {
            CloseHandle(token);
            return true;
        }
    }

    return false;
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

void Sddm::finishUnlock()
{
    qInfo() << "wsddm: exit animation finished, hiding overlay";
    emit unlockConfirmed();
}
