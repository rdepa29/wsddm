#include "PasswordStore.h"

#include "Config.h"

#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QStringList>

#include <windows.h>
#include <wincrypt.h>

namespace {
/// HMAC-SHA256 helper; PBKDF2 is built from this.
QByteArray hmacSha256(const QByteArray &key, const QByteArray &message)
{
    QMessageAuthenticationCode code(QCryptographicHash::Sha256, key);
    code.addData(message);
    return code.result();
}

/// Constant-time equality so timing cannot reveal how many leading bytes matched.
bool fixedTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;

    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}
} // namespace

bool PasswordStore::hasVerifier()
{
    const QJsonObject config = Config::load();
    return !config.value(QStringLiteral("PasswordHash")).toString().isEmpty();
}

bool PasswordStore::useLocalCheck()
{
    const QString mode = Config::load()
                             .value(QStringLiteral("PasswordCheckMode"))
                             .toString(QStringLiteral("auto"))
                             .trimmed()
                             .toLower();

    if (mode == QLatin1String("os"))
        return false;
    if (mode == QLatin1String("local"))
        return true;
    return hasVerifier();
}

QString PasswordStore::normalize(const QString &password)
{
    return QStringView(password).trimmed().toString();
}

bool PasswordStore::isAcceptable(const QString &password)
{
    return normalize(password).size() >= 8;
}

QByteArray PasswordStore::derive(const QString &password, const QByteArray &salt, int iterations)
{
    // PBKDF2-HMAC-SHA256: T_i = U_1 ^ U_2 ^ ... ^ U_c, U_1 = HMAC(P, S || INT(i)).
    const QByteArray passwordUtf8 = password.toUtf8();
    const int blockCount = (kHashBytes + 31) / 32;
    QByteArray out;
    out.reserve(blockCount * 32);

    for (int block = 1; block <= blockCount; ++block) {
        QByteArray u = salt;
        u.append(static_cast<char>((block >> 24) & 0xFF));
        u.append(static_cast<char>((block >> 16) & 0xFF));
        u.append(static_cast<char>((block >> 8) & 0xFF));
        u.append(static_cast<char>(block & 0xFF));

        QByteArray t = hmacSha256(passwordUtf8, u);
        u = t;
        for (int i = 1; i < iterations; ++i) {
            u = hmacSha256(passwordUtf8, u);
            for (int j = 0; j < 32; ++j)
                t[j] = static_cast<char>(t[j] ^ u[j]);
        }
        out.append(t);
    }

    return out.left(kHashBytes);
}

QByteArray PasswordStore::protectCurrentUser(const QByteArray &data, const QByteArray &entropy)
{
    DATA_BLOB in, saltBlob, out;
    out.pbData = nullptr;

    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()));
    in.cbData = static_cast<DWORD>(data.size());
    saltBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(entropy.constData()));
    saltBlob.cbData = static_cast<DWORD>(entropy.size());

    if (!CryptProtectData(&in, L"wsddm.password", &saltBlob, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};

    QByteArray result(reinterpret_cast<char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return result;
}

QByteArray PasswordStore::unprotectCurrentUser(const QByteArray &data, const QByteArray &entropy)
{
    DATA_BLOB in, saltBlob, out;
    out.pbData = nullptr;

    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()));
    in.cbData = static_cast<DWORD>(data.size());
    saltBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(entropy.constData()));
    saltBlob.cbData = static_cast<DWORD>(entropy.size());

    if (!CryptUnprotectData(&in, nullptr, &saltBlob, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};

    QByteArray result(reinterpret_cast<char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return result;
}

bool PasswordStore::set(const QString &password, QString *error)
{
    const QString normalized = normalize(password);
    if (normalized.size() < 8) {
        if (error)
            *error = QStringLiteral("Password must be at least 8 characters.");
        return false;
    }

    QByteArray salt(kSaltBytes, '\0');
    for (int i = 0; i < kSaltBytes; ++i)
        salt[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));

    const QByteArray hash = derive(normalized, salt, kDefaultIterations);
    const QByteArray protectedHash = protectCurrentUser(hash, salt);
    if (protectedHash.isEmpty()) {
        if (error)
            *error = QStringLiteral("DPAPI protection failed; nothing was stored.");
        return false;
    }

    QJsonObject config = Config::load();
    config.insert(QStringLiteral("PasswordSalt"), QString::fromLatin1(salt.toBase64()));
    config.insert(QStringLiteral("PasswordHash"), QString::fromLatin1(protectedHash.toBase64()));
    config.insert(QStringLiteral("PasswordIterations"), kDefaultIterations);
    return Config::save(config);
}

bool PasswordStore::clear()
{
    QJsonObject config = Config::load();
    config.remove(QStringLiteral("PasswordSalt"));
    config.remove(QStringLiteral("PasswordHash"));
    config.remove(QStringLiteral("PasswordIterations"));
    return Config::save(config);
}

bool PasswordStore::verify(const QString &password)
{
    const QJsonObject config = Config::load();
    const QString saltB64 = config.value(QStringLiteral("PasswordSalt")).toString();
    const QString hashB64 = config.value(QStringLiteral("PasswordHash")).toString();
    if (saltB64.isEmpty() || hashB64.isEmpty())
        return false;

    const QByteArray salt = QByteArray::fromBase64(saltB64.toLatin1());
    const QByteArray protectedHash = QByteArray::fromBase64(hashB64.toLatin1());
    if (salt.isEmpty() || protectedHash.isEmpty())
        return false;

    const int iterations = config.value(QStringLiteral("PasswordIterations")).toInt(kDefaultIterations);

    const QByteArray expected = unprotectCurrentUser(protectedHash, salt);
    if (expected.isEmpty())
        return false;

    const QByteArray actual = derive(normalize(password), salt, iterations);
    return fixedTimeEquals(expected, actual);
}