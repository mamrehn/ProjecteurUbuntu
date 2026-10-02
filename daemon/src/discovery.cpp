// SPDX-License-Identifier: MIT
#include "discovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

#include "hidpp.h"

Q_LOGGING_CATEGORY(lcDiscovery, "projecteur.discovery")

namespace projecteur {

namespace {
constexpr QLatin1StringView kUsbReceiver("Vendor=046d Product=c53e");
constexpr QLatin1StringView kBluetoothRemote("Vendor=046d Product=b503");

/// For each input device of `vendorProduct` in the text of /proc/bus/input/devices (blocks separated by an empty
/// line): its name and its event node. Plain string scanning: this runs whenever a device node appears.
template <typename Fn>
void forEachInputNode(QStringView proc, QLatin1StringView vendorProduct, const QString& devDir, Fn&& fn) {
  for (QStringView block : proc.split(u"\n\n", Qt::SkipEmptyParts)) {
    if (!block.contains(vendorProduct, Qt::CaseInsensitive)) continue;
    QStringView name, event;
    for (QStringView line : block.split(u'\n')) {
      if (line.startsWith(u"N: Name=\"") && line.endsWith(u'"')) {
        name = line.sliced(9, line.size() - 10);
      } else if (line.startsWith(u"H: Handlers=")) {
        for (QStringView handler : line.sliced(12).split(u' ', Qt::SkipEmptyParts)) {
          if (handler.size() > 5 && handler.startsWith(u"event") &&
              std::all_of(handler.begin() + 5, handler.end(), [](QChar c) { return c.isDigit(); }))
            event = handler;
        }
      }
    }
    if (!name.isEmpty() && !event.isEmpty()) fn(name, devDir + QStringLiteral("/input/") + event);
  }
}

/// The first hidraw node in `sysHidraw` whose HID device matches `accept` (given the canonical path of the device).
template <typename Fn>
QString findHidraw(const QString& sysHidraw, const QString& devDir, Fn&& accept) {
  const QDir hidrawClass(sysHidraw);
  for (const QString& entry : hidrawClass.entryList({QStringLiteral("hidraw*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    const QString device = QFileInfo(hidrawClass.filePath(entry + QStringLiteral("/device"))).canonicalFilePath();
    if (!device.isEmpty() && accept(device)) return devDir + QLatin1Char('/') + entry;
  }
  return {};
}

QByteArray readSmallFile(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.read(4096) : QByteArray();
}
}  // namespace

SpotlightNodes findUsbReceiver(const QString& procInputDevices, const QString& sysHidraw, const QString& devDir) {
  SpotlightNodes nodes;
  // the receiver registers several input nodes; Next / Back come from the keyboard, the short click from the mouse
  forEachInputNode(procInputDevices, kUsbReceiver, devDir, [&](QStringView name, const QString& path) {
    if (name == u"Logitech USB Receiver") nodes.keyboard = path;
    else if (name == u"Logitech USB Receiver Mouse") nodes.mouse = path;
  });
  if (nodes.keyboard.isEmpty() && nodes.mouse.isEmpty()) return nodes;   // no receiver: no need to look at sysfs

  // The HID++ interface is interface number 2 of the same USB device.
  nodes.hidraw = findHidraw(sysHidraw, devDir, [](const QString& device) {
    return QString::fromLatin1(readSmallFile(device + QStringLiteral("/uevent"))).contains(u"0000046D:0000C53E", Qt::CaseInsensitive) &&
           readSmallFile(QFileInfo(device).dir().filePath(QStringLiteral("bInterfaceNumber"))).trimmed() == "02";
  });
  return nodes;
}

SpotlightNodes findBluetoothSpotlight(const QString& procInputDevices, const QString& sysHidraw, const QString& devDir) {
  SpotlightNodes nodes;
  nodes.bluetooth = true;
  forEachInputNode(procInputDevices, kBluetoothRemote, devDir, [&](QStringView name, const QString& path) {
    if (name == u"SPOTLIGHT Keyboard") nodes.keyboard = path;
    else if (name == u"SPOTLIGHT Mouse") nodes.mouse = path;
  });
  if (nodes.keyboard.isEmpty() && nodes.mouse.isEmpty()) return nodes;
  nodes.hidraw = findHidraw(sysHidraw, devDir, [](const QString& device) {
    return QString::fromLatin1(readSmallFile(device + QStringLiteral("/uevent"))).contains(u"0005:0000046D:0000B503", Qt::CaseInsensitive);
  });
  return nodes;
}

SpotlightNodes findSpotlight(const QString& procInputDevices, const QString& sysHidraw, const QString& devDir) {
  const SpotlightNodes usb = findUsbReceiver(procInputDevices, sysHidraw, devDir);
  return usb.complete() ? usb : findBluetoothSpotlight(procInputDevices, sysHidraw, devDir);
}

SpotlightNodes findSpotlight() {
  QFile f(QStringLiteral("/proc/bus/input/devices"));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return findSpotlight(QString::fromUtf8(f.readAll()));
}

SpotlightNodes findUsbReceiver() {
  QFile f(QStringLiteral("/proc/bus/input/devices"));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return findUsbReceiver(QString::fromUtf8(f.readAll()));
}

RemoteWatcher::RemoteWatcher(KeySink* keys, PointerSink* pointer, OverlaySink* overlay, Remote::Options options, QObject* parent)
    : QObject(parent), keys_(keys), pointer_(pointer), overlay_(overlay), options_(options), find_([] { return findSpotlight(); }) {
  settle_.setSingleShot(true);
  connect(&settle_, &QTimer::timeout, this, &RemoteWatcher::poll);
  connect(&fallback_, &QTimer::timeout, this, &RemoteWatcher::poll);
  connect(&nodes_, &QFileSystemWatcher::directoryChanged, this, [this] {
    // a device node appeared, vanished or got its permissions: look once things have settled (plugging in the
    // receiver creates five nodes in a row, and udev hands them to the user a moment later)
    if (!remote_) settle_.start(kSettleMs);
  });
}

void RemoteWatcher::setFinder(Finder find, const QStringList& watchedDirs) {
  find_ = std::move(find);
  watchedDirs_ = watchedDirs;
}

void RemoteWatcher::start(int fallbackPollMs) {
  // The kernel tells (inotify) when a node appears in /dev or /dev/input, so there is no need to poll every couple of
  // seconds while no remote is around (the daemon then sleeps). The slow poll only covers a missed event.
  const QStringList failed = nodes_.addPaths(watchedDirs_);
  if (!failed.isEmpty()) {
    qCWarning(lcDiscovery).noquote() << "cannot watch" << failed.join(QStringLiteral(", ")) << "- looking for the remote every 2 s instead";
    fallbackPollMs = qMin(fallbackPollMs, 2000);
  }
  fallback_.start(fallbackPollMs);
  poll();
}

void RemoteWatcher::poll() {
  if (remote_) return;
  ++polls_;
  const SpotlightNodes nodes = find_();
  if (!nodes.complete()) {
    openRetries_ = 0;
    warnedOpen_ = false;
    return;
  }

  const int hidraw = ::open(nodes.hidraw.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  const int keyboard = ::open(nodes.keyboard.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  const int mouse = ::open(nodes.mouse.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (hidraw < 0 || keyboard < 0 || mouse < 0) {
    for (int fd : {hidraw, keyboard, mouse})
      if (fd >= 0) ::close(fd);
    // udev may not have handed the fresh nodes to the user yet: try again shortly, a few times
    if (openRetries_++ < kOpenRetries) {
      settle_.start(kRetryMs);
      return;
    }
    if (!warnedOpen_)   // once, not at every look while another program holds the nodes
      qCWarning(lcDiscovery).noquote() << "found the Spotlight but cannot open its nodes (hidraw" << nodes.hidraw
                                        << "keyboard" << nodes.keyboard << "mouse" << nodes.mouse
                                        << "): needs a local session with the udev rule; another program (projecteur?) may hold them";
    warnedOpen_ = true;
    return;
  }
  openRetries_ = 0;
  warnedOpen_ = false;
  qCInfo(lcDiscovery).noquote() << (nodes.bluetooth ? "Bluetooth Spotlight found:" : "Spotlight receiver found:") << nodes.hidraw
                                << nodes.keyboard << nodes.mouse;
  Remote::Options options = options_;
  options.bluetooth = nodes.bluetooth;
  if (nodes.bluetooth) {                 // directly connected: device index 0xff, the hidraw node takes 20 byte reports only
    options.device.deviceIndex = hidpp::kDirectDeviceIndex;
    options.device.longMessagesOnly = true;
  }
  remote_ = new Remote({hidraw, keyboard, mouse}, keys_, pointer_, overlay_, options, this);
  emit remoteChanged(remote_);
  connect(remote_, &Remote::gone, this, [this] {
    qCInfo(lcDiscovery) << "Spotlight unplugged or out of range";
    Remote* gone = remote_;
    remote_ = nullptr;
    emit remoteChanged(nullptr);
    gone->deleteLater();
    settle_.start(kSettleMs);   // the nodes may still be there (only the link failed): do not wait for the slow poll
  });
}

}  // namespace projecteur
