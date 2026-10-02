#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
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
#include <cstdio>
#include <utility>

#include "Config.h"
#include "Log.h"
#include "PasswordStore.h"
#include "Sddm.h"
#include "SessionModel.h"
#include "ThemeConfig.h"
#include "UserModel.h"

#include <windows.h>

// CLI entry helpers; defined below, forward-declared so their mutually-referencing
// implementations can stay in a readable order.
bool argvHas(int argc, char *argv[], const char *name);
bool bindConsole();
QByteArray fgetsLine();
QString readHidden(const QByteArray &prompt);
void writeUsage();
int runCli(int argc, char *argv[]);

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

/// Whether an argument (case-insensitive name) appears on the command line.
bool argvHas(int argc, char *argv[], const char *name)
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]).trimmed();
        if (arg.compare(QString::fromLatin1("--") + QString::fromLatin1(name),
                        Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/// dBinds the standard streams for a console-less WIN32 GUI process, mirroring the C#
/// NativeConsole.Bind. Returns false when there is nowhere to write at all.
bool bindConsole()
{
    const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if (!attached)
        AllocConsole();

    // Keep whatever handle the parent gave us when there is one (a pipe or a file), so
    // `wsddm --version | ...` still works; only missing handles fall back to the console.
    bool ok = false;
    for (const auto &[kind, name] : {std::pair{DWORD(STD_OUTPUT_HANDLE), "CONOUT$"},
                                     std::pair{DWORD(STD_ERROR_HANDLE), "CONOUT$"},
                                     std::pair{DWORD(STD_INPUT_HANDLE), "CONIN$"}}) {
        HANDLE handle = GetStdHandle(kind);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
            handle = CreateFileA(name, kind == STD_INPUT_HANDLE ? GENERIC_READ : GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 0, nullptr);
            if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
                SetStdHandle(kind, handle);
        }
        if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
            ok = true;
    }

    return attached || ok;
}

/// Reads a password without echoing it, or a plain line when stdin is redirected (a pipe
/// cannot suppress echo at the OS level). Returns null on Escape/EOF.
QString readHidden(const QByteArray &prompt)
{
    const QByteArray bell = "\r" + prompt;

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD consoleMode = 0;
    const bool isConsole = GetConsoleMode(input, &consoleMode) != FALSE;

    std::fputs(bell.constData(), stdout);
    std::fflush(stdout);

    if (isConsole) {
        // With echo disabled, ReadConsole still blocks until Enter and lets the user edit
        // the line; the returned buffer is the final text with the CRLF stripped.
        SetConsoleMode(input, consoleMode & ~ENABLE_ECHO_INPUT);

        wchar_t buffer[512];
        DWORD read = 0;
        const BOOL got = ReadConsoleW(input, buffer, sizeof(buffer) / sizeof(buffer[0]) - 1,
                                      &read, nullptr);
        SetConsoleMode(input, consoleMode);

        std::fputc('\n', stdout);
        std::fflush(stdout);

        if (!got)
            return {};

        while (read > 0 && (buffer[read - 1] == L'\r' || buffer[read - 1] == L'\n'))
            --read;
        return QString::fromWCharArray(buffer, static_cast<int>(read));
    }

    // Redirected stdin: the caller's pipe is the only source, and it cannot be hidden.
    QByteArray line = fgetsLine();
    std::fputc('\n', stdout);
    std::fflush(stdout);
    return QString::fromUtf8(line);
}

/// Reads one UTF-8 line from stdin (used for piped passwords in the redirected case).
QByteArray fgetsLine()
{
    QByteArray line;
    int c;
    while ((c = std::fgetc(stdin)) != EOF && c != '\n')
        line.append(static_cast<char>(c));
    return line;
}

/// Writes the usage reference, the same one the C# build printed.
void writeUsage()
{
    std::fputs("\n"
               "wsddm 0.0.2 - Windows desktop privacy overlay (caelestia locklike)\n"
               "\n"
               "Usage:\n"
               "  wsddm                    Start in the tray (default when run with no options)\n"
               "  wsddm --lock             Start in the tray and show the lock screen immediately\n"
               "  wsddm --set-password     Set or replace the unlock password, then exit\n"
               "  wsddm --clear-password   Remove the stored unlock password, then exit\n"
               "  wsddm --version          Print the version, then exit\n"
               "  wsddm --help             Print this help, then exit\n"
               "\n"
               "Passwords:\n"
               "  Microsoft accounts cannot be checked through Windows, because the local\n"
               "  account store holds an opaque credential rather than the account password.\n"
               "  For those, run --set-password and wsddm checks the secret itself, using a\n"
               "  salted, iterated hash stored under PasswordHash in the config file.\n"
               "  Local accounts can use the Windows check instead by setting\n"
               "  PasswordCheckMode to \"os\" in the config file.\n",
               stdout);
}

/// Handles the CLI entry points; returns >= 0 when the argument was a CLI command that has
/// been fully dealt with (and that is the process exit code), or -1 to start the tray app.
int runCli(int argc, char *argv[])
{
    if (argc < 2)
        return -1;

    // --lock belongs to the tray app itself (it just asks it to raise the screen via the
    // named event), so it must pass through to main() rather than be treated as a standalone
    // command or an unknown option.
    const bool lock = argvHas(argc, argv, "lock");
    const bool set = argvHas(argc, argv, "set-password");
    const bool clear = argvHas(argc, argv, "clear-password");
    const bool help = argvHas(argc, argv, "help") || argvHas(argc, argv, "h")
        || argvHas(argc, argv, "?");
    const bool version = argvHas(argc, argv, "version");

    if (!set && !clear && !help && !version && !lock) {
        // An unrecognised option is a mistake worth reporting, not something to silently
        // swallow by starting a tray app in the background.
        for (int i = 1; i < argc; ++i) {
            if (argv[i][0] == '-' || argv[i][0] == '/') {
                bindConsole();
                std::fprintf(stderr, "wsddm: unknown option: %s\n", argv[i]);
                writeUsage();
                return 2;
            }
        }
        return -1;
    }

    if (lock)
        return -1; // let main() create the mutex/event so a second instance signals the first

    if (!bindConsole()) {
        // No console and no piped handle: there is nowhere to report failure, but the
        // command still ran far enough to know it was requested. Exit silently.
        return 0;
    }

    if (help) {
        writeUsage();
        return 0;
    }

    if (version) {
        std::fputs("wsddm 0.0.2\n", stdout);
        return 0;
    }

    if (set) {
        std::fputs("wsddm unlock password\n\n", stdout);
        if (PasswordStore::hasVerifier())
            std::fputs("  A password is already set. This replaces it.\n", stdout);

        const QString mode = Config::load()
                                 .value(QStringLiteral("PasswordCheckMode"))
                                 .toString(QStringLiteral("auto"))
                                 .trimmed()
                                 .toLower();
        if (mode == QLatin1String("os")) {
            std::fputs("  Note: PasswordCheckMode is \"os\", which checks the Windows account\n"
                       "  instead, so this password would not be used. Edit config.json and set\n"
                       "  PasswordCheckMode to \"auto\" or \"local\" first.\n",
                       stdout);
        }

        const QString first = readHidden("New password (min 8 characters): ");
        if (first.isNull()) {
            std::fputs("Cancelled. Nothing changed.\n", stdout);
            return 1;
        }

        const QString second = readHidden("Confirm: ");
        if (second.isNull()) {
            std::fputs("Cancelled. Nothing changed.\n", stdout);
            return 1;
        }

        if (first != second) {
            std::fputs("The two entries do not match. Nothing changed.\n", stderr);
            return 1;
        }

        if (!PasswordStore::isAcceptable(first)) {
            std::fputs("Password must be at least 8 characters. Nothing changed.\n", stderr);
            return 1;
        }

        QString error;
        if (!PasswordStore::set(first, &error)) {
            std::fprintf(stderr, "Failed to save: %s\n", qPrintable(error));
            return 1;
        }

        qInfo("wsddm: password verifier saved from cli");
        std::fputs("\nSaved a hash of it. The password itself was not stored.\n", stdout);
        return 0;
    }

    if (clear) {
        if (!PasswordStore::hasVerifier()) {
            std::fputs("No password is set, so there is nothing to clear.\n", stdout);
            return 0;
        }

        PasswordStore::clear();
        qInfo("wsddm: password verifier cleared from cli");
        std::fputs("Cleared. Unlocking now falls back to the Windows account check.\n", stdout);
        return 0;
    }

    return -1;
}

int main(int argc, char *argv[])
{
    // Installed before anything else so QML parse errors during setSource are captured.
    Log::install();

    // The locklike theme renders its glyphs (power, reboot, chevrons, widgets) with the
    // "Material Symbols Rounded" family. Bundling it makes the icons appear as intended
    // instead of the tofu boxes the system fallback shows for those PUA codepoints.
    const int fontId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/qt/qml/wsddm/qml/assets/material-symbols/MaterialSymbolsRounded.ttf"));
    if (fontId < 0)
        qWarning("wsddm: Material Symbols Rounded font failed to load; theme icons may be missing");
    else
        Q_UNUSED(QFontDatabase::applicationFontFamilies(fontId).value(0))

    // CLI commands are handled before the single-instance mutex so --set-password keeps
    // working while the tray app is already running, exactly like the C# build.
    {
        const int cliResult = runCli(argc, argv);
        if (cliResult >= 0)
            return cliResult;
    }

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

    const bool lockNowRequested = argvHas(argc, argv, "lock");

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
    SessionModel sessions;

    // The theme's root object is a Rectangle, not a Window, because under SDDM it is
    // placed inside a window the greeter owns. ThemedView (a QQuickView) provides that
    // window, so the QML loads unmodified.
    ThemedView view;
    view.setFlags(Qt::Window | Qt::FramelessWindowHint);
    view.setTitle(QStringLiteral("wsddm"));

    view.rootContext()->setContextProperty(QStringLiteral("config"), &config);
    view.rootContext()->setContextProperty(QStringLiteral("sddm"), &sddm);
    view.rootContext()->setContextProperty(QStringLiteral("userModel"), &users);
    // A Windows lock overlay has no sessions to switch to; the theme gets one placeholder
    // entry so its picker's component-loading code has an array to index into.
    view.rootContext()->setContextProperty(QStringLiteral("sessionModel"), &sessions);

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

    // On a correct password the theme plays its exit animation, then calls
    // sddm.finishUnlock() which emits unlockConfirmed(). Only then drop the overlay and let
    // the display go back to its own power management.
    const auto unlock = [&] {
        locked = false;
        view.hide();
        qInfo("wsddm: unlocked");
        SetThreadExecutionState(ES_CONTINUOUS);
    };
    QObject::connect(&sddm, &Sddm::unlockConfirmed, &app, unlock);

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