#pragma once

#include <QHash>
#include <QObject>
#include <QString>

/// Exposes theme.conf to QML as the `config` context object.
///
/// The QML reads every key as a string and compares it itself
/// (`config.ap === "true"`), so the values are kept as QString rather than being
/// converted to bool/double here. That keeps the parsing rules in the QML where the
/// theme already defines them, instead of splitting them across two languages.
class ThemeConfig : public QObject
{
    Q_OBJECT

    // Declares a read-only QString property backed by the same-named theme.conf key.
#define THEME_KEY(name)                             \
    Q_PROPERTY(QString name READ name CONSTANT)     \
    QString name() const { return valueOf(QStringLiteral(#name)); }

    THEME_KEY(ap)
    THEME_KEY(AvatarShape)
    THEME_KEY(background)
    THEME_KEY(backgroundVideoEnabled)
    THEME_KEY(enableWelcomeMessage)
    THEME_KEY(host)
    THEME_KEY(inverseOnSurface)
    THEME_KEY(mainCard)
    THEME_KEY(mainCardBgBlur)
    THEME_KEY(mainCardBlurAmount)
    THEME_KEY(mainCardColorOpacity)
    THEME_KEY(mainCardComponentsOpacity)
    THEME_KEY(onPrimary)
    THEME_KEY(onSecondary)
    THEME_KEY(onSuccess)
    THEME_KEY(onTertiary)
    THEME_KEY(os)
    THEME_KEY(outline)
    THEME_KEY(primary)
    THEME_KEY(secondary)
    THEME_KEY(sessionPicker)
    THEME_KEY(subComponents)
    THEME_KEY(surface)
    THEME_KEY(surfaceContainer)
    THEME_KEY(surfaceContainerHigh)
    THEME_KEY(surfaceContainerLow)
    THEME_KEY(surfaceVariant)
    THEME_KEY(tertiary)
    THEME_KEY(text)
    THEME_KEY(textDark)
    THEME_KEY(welcomeBgBlur)
    THEME_KEY(welcomeBgBlurAmount)
    THEME_KEY(welcomeColorOpacity)

#undef THEME_KEY

public:
    explicit ThemeConfig(QObject *parent = nullptr);

    /// Reads theme.conf, falling back to the values the QML already defaults for.
    void load(const QString &path);

    QString valueOf(const QString &key) const
    {
        return m_values.value(key);
    }

    /// Host and OS have no meaning under SDDM, so they come from the running machine.
    void applyHostDefaults();

private:
    QHash<QString, QString> m_values;
};
