#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QResizeEvent>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

#include <functional>

#include "Log.h"
#include "Sddm.h"
#include "ThemeConfig.h"
#include "UserModel.h"

#include <windows.h>

namespace {
/// The theme's authored canvas, matching Main.qml's root width/height.
constexpr int kDesignWidth = 1920;
constexpr int kDesignHeight = 1080;

/// A view that keeps the theme at its authored 1920x1080 and scales it to the window.
///
/// QQuickView only offers SizeRootObjectToView in Qt 6, which resizes the root to the
/// window. That is wrong here: the theme is a fixed design whose fonts, paddings and
/// anchors are all authored against 1920x1080, so squeezing it to 1280x720 would leave
/// the layout collapsed rather than proportionally smaller. Overriding resizeEvent lets
/// the root keep its design size and be scaled, which is what SDDM does on any display.
class ThemedView : public QQuickView
{
public:
    using QQuickView::QQuickView;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QQuickView::resizeEvent(event);
        applyScale();
    }

    void applyScale() const
    {
        auto *theme = qobject_cast<QQuickItem *>(rootObject());
        if (!theme)
            return;

        const qreal scale =
            qMin(width() / qreal(kDesignWidth), height() / qreal(kDesignHeight));

        theme->setWidth(kDesignWidth);
        theme->setHeight(kDesignHeight);
        theme->setTransformOrigin(QQuickItem::TopLeft);
        theme->setScale(scale);
        // Centred, so a display that is not 16:9 letterboxes instead of offsetting.
        theme->setX((width() - kDesignWidth * scale) / 2.0);
        theme->setY((height() - kDesignHeight * scale) / 2.0);
    }
};

/// theme.conf next to the executable, falling back to the copy in the working directory
/// so the theme can be edited without rebuilding.
QString findThemeConf()
{
    const QString beside = QCoreApplication::applicationDirPath()
        + QStringLiteral("/theme.conf");
    if (QFileInfo::exists(beside))
        return beside;

    return QDir::current().filePath(QStringLiteral("theme.conf"));
}

/// Reports where the bundled QML actually ended up in the resource tree, which is
/// otherwise invisible: a WIN32 build has no console, so a wrong load path only shows
/// up as "No such file or directory" with nothing to compare it against.
void findInResources(const QString &root, int depth)
{
    if (depth <= 0)
        return;

    const QStringList entries = QDir(root).entryList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

    for (const QString &entry : entries) {
        const QString path = root + QLatin1Char('/') + entry;
        if (entry == QLatin1String("Main.qml") || entry == QLatin1String("MaterialIcon.qml"))
            qCritical().noquote() << "wsddm: found" << path;
        if (QFileInfo(path).isDir())
            findInResources(path, depth - 1);
    }
}
} // namespace

int main(int argc, char *argv[])
{
    // Installed before anything else so QML parse errors during setSource are captured.
    Log::install();

    // The theme customises the internals of ScrollBar and ComboBox, which Qt Quick
    // Controls' native Windows style refuses. Upstream SDDM runs with the Basic style for
    // the same reason, and the theme's colours are not the system style's anyway.
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    // RandomQuote reads config/quotes.json over XHR, which Qt blocks for local files
    // unless this is set.
    qputenv("QML_XHR_ALLOW_FILE_READ", "1");

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wsddm"));
    app.setOrganizationName(QStringLiteral("wsddm"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Windows desktop privacy overlay (caelestia locklike)"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    ThemeConfig config;
    config.load(findThemeConf());
    config.applyHostDefaults();

    Sddm sddm;
    UserModel users;

    // The theme's root object is a Rectangle, not a Window, because under SDDM it is
    // placed inside a window the greeter owns. ThemedView (a QQuickView) provides that
    // window, so the QML loads unmodified.
    ThemedView view;
    view.setFlags(Qt::Window | Qt::FramelessWindowHint);
    view.setTitle(QStringLiteral("wsddm"));

    view.rootContext()->setContextProperty(QStringLiteral("config"), &config);
    view.rootContext()->setContextProperty(QStringLiteral("sddm"), &sddm);
    view.rootContext()->setContextProperty(QStringLiteral("userModel"), &users);
    // The theme guards this one with a null check, so a null model is a supported state.
    view.rootContext()->setContextProperty(QStringLiteral("sessionModel"), nullptr);

    // The theme uses relative imports ("components", "widgets"), so it is loaded from
    // the bundled qml/ tree rather than as an imported QML module. qt_add_qml_module
    // places it under its URI prefix, hence the doubled "qml/wsddm/qml" in the path;
    // relative imports resolve inside that tree exactly as they do under SDDM.
    view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/wsddm/qml/Main.qml")));
    if (view.status() == QQuickView::Error) {
        for (const auto &error : view.errors())
            qCritical().noquote() << "wsddm:" << error.toString();

        findInResources(QStringLiteral(":/"), 6);
        return 1;
    }

    QScreen *screen = view.screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    if (screen) {
        // Re-apply the scale on display changes so an unplugged or switched monitor
        // re-lays out the theme instead of leaving it at the old factor.
        const auto refresh = [&view] {
            if (view.rootObject())
                view.resize(view.width(), view.height());
        };
        QObject::connect(screen, &QScreen::geometryChanged, &app, refresh);
        QObject::connect(screen, &QScreen::availableGeometryChanged, &app, refresh);
    }

    // Cover the whole primary screen. The theme scales itself to fit whatever size it
    // is given, so a 1280x720 display shows the same layout at 2/3 scale.
    if (screen)
        view.setGeometry(screen->geometry());

    view.showFullScreen();
    view.raise();
    view.requestActivate();

    // Keep the display awake while the screen is up, matching the C# build.
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    return app.exec();
}



