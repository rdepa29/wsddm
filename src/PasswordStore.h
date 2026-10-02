#pragma once

#include <QByteArray>
#include <QString>

/// Verifies the unlock secret against a salted, iterated hash kept in the config file.
///
/// A Microsoft account cannot be checked through Windows: the local SAM stores an opaque
/// credential rather than the account password, so the OS reject every password typed at
/// the lock screen. The local verifier is the only path that unlocks for those accounts.
/// It uses PBKDF2-HMAC-SHA256 at 600k iterations (OWASP's floor for PBKDF2-SHA256), a
/// random per-user salt, and DPAPI CurrentUser protection so the stored hash is only
/// readable by processes running as the signed-in user.
class PasswordStore
{
public:
    static constexpr int kDefaultIterations = 600000;
    static constexpr int kSaltBytes = 16;
    static constexpr int kHashBytes = 32;

    /// True when a verifier is configured, so callers can prefer it over the OS check.
    static bool hasVerifier();

    /// Resolves PasswordCheckMode ("os" / "local" / "auto"); auto prefers the local
    /// verifier when one is configured, because on a Microsoft account the OS never wins.
    static bool useLocalCheck();

    /// True when the password is long enough to store (mirrors the trimmming in set()).
    static bool isAcceptable(const QString &password);

    /// Replaces the stored verifier with one derived from password.
    static bool set(const QString &password, QString *error = nullptr);

    /// Removes the stored verifier.
    static bool clear();

    /// Constant-time check of password against the stored hash.
    static bool verify(const QString &password);

private:
    static QString normalize(const QString &password);
    static QByteArray derive(const QString &password, const QByteArray &salt, int iterations);
    static QByteArray protectCurrentUser(const QByteArray &data, const QByteArray &entropy);
    static QByteArray unprotectCurrentUser(const QByteArray &data, const QByteArray &entropy);
};