// SPDX-License-Identifier: MIT
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <csignal>
#include <cstdio>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

#include "dbusservice.h"
#include "discovery.h"
#include "keysink.h"
#include "overlayclient.h"
#include "remote.h"
#include "service.h"

Q_LOGGING_CATEGORY(lcMain, "projecteur.main")

using namespace projecteur;

namespace {
int signalPipe[2] = {-1, -1};
void onSignal(int) {
  const char c = 1;
  const ssize_t n = ::write(signalPipe[1], &c, 1);
  (void)n;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("projecteurd"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  qSetMessagePattern(QStringLiteral("%{time hh:mm:ss.zzz} [%{category}] %{message}"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Background service for the Logitech Spotlight (drives the GNOME Shell overlay)"));
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption testFds(QStringLiteral("test-fds"),
                             QStringLiteral("TESTING: use pre-opened descriptors HIDRAW,KEYBOARD,MOUSE,KEYOUT instead of the real device and /dev/uinput."),
                             QStringLiteral("fds"));
  QCommandLineOption noGrab(QStringLiteral("no-grab"), QStringLiteral("Do not capture the remote's input nodes exclusively."));
  QCommandLineOption verbose(QStringLiteral("verbose"), QStringLiteral("Log debug messages."));
  QCommandLineOption listDevices(QStringLiteral("list-devices"), QStringLiteral("Print the Spotlight device nodes that would be used, then exit."));
  parser.addOptions({testFds, noGrab, verbose, listDevices});
  parser.process(app);

  if (parser.isSet(listDevices)) {
    const SpotlightNodes n = findSpotlight();
    if (!n.complete()) {
      std::printf("No Spotlight found (USB receiver plugged in, or the remote paired and awake over Bluetooth?).\n");
      return 1;
    }
    std::printf("%s Spotlight\n  HID++ : %s\n  keys  : %s\n  mouse : %s\n", n.bluetooth ? "Bluetooth" : "USB receiver",
                qPrintable(n.hidraw), qPrintable(n.keyboard), qPrintable(n.mouse));
    return 0;
  }
  if (!parser.isSet(verbose)) QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false"));

  // clean shutdown: the buttons are given back to the system (undiverted) when the daemon stops
  if (::pipe(signalPipe) != 0) return 1;
  struct sigaction sa {};
  sa.sa_handler = onSignal;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  QSocketNotifier signalNotifier(signalPipe[0], QSocketNotifier::Read);
  QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [] { QCoreApplication::quit(); });

  QDBusConnection bus = QDBusConnection::sessionBus();  // honours DBUS_SESSION_BUS_ADDRESS (tests use a private bus)
  if (!bus.isConnected()) {
    qCCritical(lcMain) << "cannot connect to the session bus:" << bus.lastError().message();
    return 1;
  }
  OverlayClient overlay(bus);
  Service service;
  QString exportError;
  if (!exportService(bus, &service, &exportError)) {
    qCCritical(lcMain).noquote() << exportError;
    return 1;
  }

  Remote::Options options;
  options.grab = !parser.isSet(noGrab);
  options.config = service.config();

  std::unique_ptr<KeySink> keys;
  std::unique_ptr<PointerSink> pointer;
  Remote* testRemote = nullptr;
  RemoteWatcher* watcher = nullptr;

  if (parser.isSet(testFds)) {
    const QStringList f = parser.value(testFds).split(QLatin1Char(','));
    if (f.size() != 4) {
      qCCritical(lcMain) << "--test-fds needs HIDRAW,KEYBOARD,MOUSE,KEYOUT";
      return 2;
    }
    keys = std::make_unique<FdKeySink>(f[3].toInt());
    pointer = std::make_unique<FdPointerSink>(f[3].toInt());
    options.grab = false;
    testRemote = new Remote({f[0].toInt(), f[1].toInt(), f[2].toInt()}, keys.get(), pointer.get(), &overlay, options, &app);
    service.setRemote(testRemote);
  } else {
    QString error;
    keys = UinputKeyboard::create(&error);
    if (!keys) {
      qCWarning(lcMain).noquote() << error << "- not capturing the remote's keys, so Next/Back keep working unprocessed";
      options.grab = false;
    }
    pointer = UinputPointer::create(&error);
    if (!pointer) qCWarning(lcMain).noquote() << error << "- the scroll action is unavailable";
    watcher = new RemoteWatcher(keys.get(), pointer.get(), &overlay, options, &app);
    QObject::connect(watcher, &RemoteWatcher::remoteChanged, &service, &Service::setRemote);
    watcher->start();
  }
  qCInfo(lcMain) << "projecteurd running";

  const int rc = app.exec();
  delete testRemote;  // undivert
  delete watcher;
  return rc;
}
