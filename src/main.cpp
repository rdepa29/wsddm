#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QResizeEvent>
#include <QScreen>
#include <QSystemTrayIcon>
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

/// Routes the global hotkey (WIN+SHIFT+L) to the popup action.
///
/// RegisterHotKey is given a null HWND, which posts WM_HOTKEY into this thread's queue
/// rather than to a window proc; Qt's dispatcher runs every queued message through the
/// native event filters, so this is where that message is seen.
class HotkeyFilter final : public QAbstractNativeEventFilter
{
public:
    using Action = std::function<void()>;

    explicit HotkeyFilter(Action action)
        : m_action(std::move(action))
    {
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
    {
        if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
            const MSG *msg = static_cast<const MSG *>(message);
            if (msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {
                m_action();
                return true;
            }
        }
        return false;
    }

    static constexpr UINT kHotkeyId = 0x5753; // "WS"

private:
    Action m_action;
};

/// A simple tray glyph matching the archived C# app: a bright ring with a hollow white
/// centre, drawn programmatically so there is no .ico to maintain.
QIcon buildTrayIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x2A, 0xAA, 0xFF));
    painter.drawEllipse(2, 2, 28, 28);
    painter.setBrush(Qt::white);
    painter.drawEllipse(12, 12, 8, 8);
    painter.end();

    return QIcon(pixmap);
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

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wsddm"));
    app.setOrganizationName(QStringLiteral("wsddm"));
    // The only window is the lock view, which is hidden whenever the screen is not
    // locked. Closing (or never showing) it must not tear down the tray process.
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Windows desktop privacy overlay (caelestia locklike)"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption lockOption(QStringLiteral("lock"),
        QStringLiteral("Show the lock screen immediately instead of waiting for the tray."));
    parser.addOption(lockOption);
    parser.process(app);
    const bool lockNowRequested = parser.isSet(lockOption);

    // One process at a time. A second launch with --lock simply raises the screen in the
    // instance that is already sitting in the tray; anything else just exits.
    HANDLE singleMutex = CreateMutexW(nullptr, TRUE, L"WSDDM_SingleInstance");
    const bool isFirstInstance = GetLastError() != ERROR_ALREADY_EXISTS;

    HANDLE lockEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"WSDDM_LockRequest");
    if (!isFirstInstance) {
        if (lockNowRequested && lockEvent)
            SetEvent(lockEvent);
        if (lockEvent)
            CloseHandle(lockEvent);
        return 0;
    }

    if (!lockEvent)
        lockEvent = CreateEventW(nullptr, TRUE, FALSE, L"WSDDM_LockRequest");

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

    // Raise the full-screen lock view. Idempotent, so the tray, the menu item and the
    // hotkey can all call it without worrying about double locking.
    bool locked = false;
    const auto lockNow = [&] {
        if (locked)
            return;

        locked = true;
        qInfo("wsddm: lock requested");
        QScreen *target = view.screen();
        if (!target)
            target = QGuiApplication::primaryScreen();
        if (target)
            view.setGeometry(target->geometry());

        view.showFullScreen();
        view.raise();
        view.requestActivate();

        // Keep the display awake while the screen is showing.
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    };

    // The theme emits this on a correct password (and on a power/reboot click). Drop the
    // overlay and let the display go back to its own power management.
    const auto unlock = [&] {
        locked = false;
        view.hide();
        qInfo("wsddm: unlocked");
        SetThreadExecutionState(ES_CONTINUOUS);
    };
    QObject::connect(&sddm, &Sddm::loginSucceeded, &app, unlock);

    // The tray icon. Left-click locks immediately; the menu also offers Edit config and
    // Quit, mirroring the archived C# tray.
    QSystemTrayIcon tray(buildTrayIcon());
    tray.setToolTip(QStringLiteral("WSDDM - an SDDM-style lock screen for Windows"));
    tray.show();

    QMenu trayMenu;
    QAction *lockAction = trayMenu.addAction(QStringLiteral("Lock now"));
    trayMenu.addSeparator();
    const QString themeConfPath = findThemeConf();
    QAction *editAction = trayMenu.addAction(QStringLiteral("Edit config"));
    trayMenu.addSeparator();
    QAction *quitAction = trayMenu.addAction(QStringLiteral("Quit"));
    tray.setContextMenu(&trayMenu);

    QObject::connect(lockAction, &QAction::triggered, &app, lockNow);
    QObject::connect(editAction, &QAction::triggered, &app, [themeConfPath] {
        // theme.conf is INI shaped and reads naturally in notepad, matching the WPF
        // build's "edit config" entry which opened the same file path.
        QProcess::startDetached(QStringLiteral("notepad.exe"), QStringList{themeConfPath});
    });
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &app,
                     [lockNow](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::Trigger)
                             lockNow();
                     });

    // Global Win+Shift+L hotkey.
    HotkeyFilter hotkey(lockNow);
    app.installNativeEventFilter(&hotkey);
    const bool hotkeyOk = RegisterHotKey(nullptr, HotkeyFilter::kHotkeyId,
                                         MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, /*L*/ 'L');
    if (!hotkeyOk)
        qWarning("wsddm: failed to register Win+Shift+L hotkey");

    // A second `wsddm --lock` signals this process through the named event instead of
    // fighting over the tray; poll it cheaply on the main thread.
    QTimer lockPoller;
    QObject::connect(&lockPoller, &QTimer::timeout, &app, [&] {
        if (WaitForSingleObject(lockEvent, 0) == WAIT_OBJECT_0) {
            ResetEvent(lockEvent);
            lockNow();
        }
    });
    lockPoller.start(200);

    // Testable and scriptable: `wsddm --lock` behaves like pressing the tray icon.
    if (lockNowRequested)
        QTimer::singleShot(0, &app, lockNow);

    const int exitCode = app.exec();

    UnregisterHotKey(nullptr, HotkeyFilter::kHotkeyId);
    if (lockEvent)
        CloseHandle(lockEvent);
    if (singleMutex)
        CloseHandle(singleMutex);
    return exitCode;
}