#include "ThemeConfig.h"

#include <QFileInfo>
#include <QSettings>
#include <QSysInfo>

namespace {
/// Values the QML relies on when a key is missing, so a trimmed-down theme.conf
/// still produces a usable screen instead of empty strings.
const QHash<QString, QString> kDefaults = {
    {QStringLiteral("ap"), QStringLiteral("false")},
    {QStringLiteral("AvatarShape"), QStringLiteral("hexagon")},
    {QStringLiteral("background"), QStringLiteral("#0d0e12")},
    {QStringLiteral("backgroundVideoEnabled"), QStringLiteral("false")},
    {QStringLiteral("enableWelcomeMessage"), QStringLiteral("true")},
    {QStringLiteral("mainCard"), QStringLiteral("#0d0e12")},
    {QStringLiteral("mainCardBgBlur"), QStringLiteral("true")},
    {QStringLiteral("mainCardBlurAmount"), QStringLiteral("0.9")},
    {QStringLiteral("mainCardColorOpacity"), QStringLiteral("0.9")},
    {QStringLiteral("mainCardComponentsOpacity"), QStringLiteral("0.9")},
    {QStringLiteral("sessionPicker"), QStringLiteral("false")},
    {QStringLiteral("subComponents"), QStringLiteral("#17191f")},
    {QStringLiteral("text"), QStringLiteral("#e3e5f0")},
    {QStringLiteral("textDark"), QStringLiteral("#f0dfff")},
    {QStringLiteral("welcomeBgBlur"), QStringLiteral("true")},
    {QStringLiteral("welcomeBgBlurAmount"), QStringLiteral("0.7")},
    {QStringLiteral("welcomeColorOpacity"), QStringLiteral("0.7")},
};
} // namespace

ThemeConfig::ThemeConfig(QObject *parent)
    : QObject(parent)
    , m_values(kDefaults)
{
}

void ThemeConfig::load(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        qWarning("wsddm: theme.conf not found at %s, using defaults", qPrintable(path));
        return;
    }

    // theme.conf is INI shaped, so QSettings reads it directly rather than needing a
    // hand written parser. Everything the theme reads lives under [General].
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("General"));

    for (const QString &key : settings.allKeys()) {
        m_values.insert(key, settings.value(key).toString());
    }

    settings.endGroup();
}

void ThemeConfig::applyHostDefaults()
{
    // SDDM fills these in from the machine it greets. There is no such concept here, so
    // the values come from Windows and are only set when the theme did not specify them.
    if (!m_values.contains(QStringLiteral("os")))
        m_values.insert(QStringLiteral("os"), QSysInfo::prettyProductName());

    if (!m_values.contains(QStringLiteral("host")))
        m_values.insert(QStringLiteral("host"), QSysInfo::machineHostName());
}
