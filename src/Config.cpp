#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

QString Config::configPath()
{
    return QDir::home().filePath(
        QStringLiteral(".config/wsddm/config.json"));
}

QJsonObject Config::load()
{
    const QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QByteArray data = file.readAll();
    return QJsonDocument::fromJson(data).object();
}

bool Config::save(const QJsonObject &object)
{
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return true;
}