#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace {
QFile *g_file = nullptr;

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return QStringLiteral("debug");
    case QtInfoMsg:     return QStringLiteral("info");
    case QtWarningMsg:  return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg:    return QStringLiteral("fatal");
    }
    return QStringLiteral("log");
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString line = QStringLiteral("%1 %2 [%3] %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                  levelName(type),
                                  context.file ? QString::fromUtf8(context.file) : QString(),
                                  message);

    if (g_file) {
        QTextStream out(g_file);
        out << line << Qt::endl;
    }

    // Also to stderr, so a CLI run still shows what happened.
    const QByteArray utf8 = line.toUtf8();
    std::fputs(utf8.constData(), stderr);
    std::fputc('\n', stderr);
}
} // namespace

namespace Log {

QString path()
{
    // %USERPROFILE%\.config\wsddm\wsddm.log, matching the C# build's location.
    const QString dir = QDir::homePath() + QStringLiteral("/.config/wsddm");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/wsddm.log");
}

void install()
{
    if (g_file)
        return;

    auto *file = new QFile(path());
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        delete file;
    else
        g_file = file;

    qInstallMessageHandler(handler);
}

} // namespace Log
