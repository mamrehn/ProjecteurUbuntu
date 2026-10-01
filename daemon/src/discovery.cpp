// SPDX-License-Identifier: MIT
#include "discovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTextStream>
#include <fcntl.h>

Q_LOGGING_CATEGORY(lcDiscovery, "projecteur.discovery")

namespace projecteur {

SpotlightNodes findUsbReceiver(const QString& procInputDevices, const QString& sysHidraw, const QString& devDir) {
  SpotlightNodes nodes;

  // /proc/bus/input/devices: blocks separated by an empty line; the receiver registers several input nodes.
  static const QRegularExpression vendor(QStringLiteral("Vendor=046d Product=c53e"), QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression name(QStringLiteral("^N: Name=\"([^\"]*)\"$"), QRegularExpression::MultilineOption);
  static const QRegularExpression event(QStringLiteral("\\bevent(\\d+)\\b"));
  for (const QString& block : procInputDevices.split(QStringLiteral("\n\n"))) {
    if (!vendor.match(block).hasMatch()) continue;
    const auto n = name.match(block);
    const auto e = event.match(block);
    if (!n.hasMatch() || !e.hasMatch()) continue;
    const QString path = devDir + QStringLiteral("/input/event") + e.captured(1);
    if (n.captured(1) == QStringLiteral("Logitech USB Receiver")) nodes.keyboard = path;
    else if (n.captured(1) == QStringLiteral("Logitech USB Receiver Mouse")) nodes.mouse = path;
  }

  // The HID++ interface is interface number 2 of the same USB device.
  const QDir hidrawClass(sysHidraw);
  for (const QString& entry : hidrawClass.entryList({QStringLiteral("hidraw*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    const QString device = QFileInfo(hidrawClass.filePath(entry + QStringLiteral("/device"))).canonicalFilePath();
    if (device.isEmpty()) continue;
    QFile uevent(device + QStringLiteral("/uevent"));
    QFile iface(QFileInfo(device).dir().filePath(QStringLiteral("bInterfaceNumber")));
    if (!uevent.open(QIODevice::ReadOnly) || !iface.open(QIODevice::ReadOnly)) continue;
    if (QString::fromLatin1(uevent.readAll()).toUpper().contains(QStringLiteral("0000046D:0000C53E")) &&
        QString::fromLatin1(iface.readAll()).trimmed() == QStringLiteral("02")) {
      nodes.hidraw = devDir + QLatin1Char('/') + entry;
      break;
    }
  }
  return nodes;
}

SpotlightNodes findUsbReceiver() {
  QFile f(QStringLiteral("/proc/bus/input/devices"));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return findUsbReceiver(QString::fromUtf8(f.readAll()));
}

RemoteWatcher::RemoteWatcher(KeySink* keys, OverlaySink* overlay, Remote::Options options, QObject* parent)
    : QObject(parent), keys_(keys), overlay_(overlay), options_(options) {
  connect(&timer_, &QTimer::timeout, this, &RemoteWatcher::poll);
}

void RemoteWatcher::start(int intervalMs) {
  timer_.start(intervalMs);
  poll();
}

void RemoteWatcher::poll() {
  if (remote_) return;
  const SpotlightNodes nodes = findUsbReceiver();
  if (!nodes.complete()) return;

  const int hidraw = ::open(nodes.hidraw.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  const int keyboard = ::open(nodes.keyboard.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  const int mouse = ::open(nodes.mouse.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (hidraw < 0 || keyboard < 0 || mouse < 0) {
    qCWarning(lcDiscovery).noquote() << "found the Spotlight receiver but cannot open its nodes (hidraw" << nodes.hidraw
                                      << "keyboard" << nodes.keyboard << "mouse" << nodes.mouse
                                      << "): needs a local session with the udev rule; another program (projecteur?) may hold them";
    for (int fd : {hidraw, keyboard, mouse})
      if (fd >= 0) ::close(fd);
    return;
  }
  qCInfo(lcDiscovery).noquote() << "Spotlight receiver found:" << nodes.hidraw << nodes.keyboard << nodes.mouse;
  remote_ = new Remote({hidraw, keyboard, mouse}, keys_, overlay_, options_, this);
  connect(remote_, &Remote::gone, this, [this] {
    qCInfo(lcDiscovery) << "Spotlight receiver unplugged";
    remote_->deleteLater();
    remote_ = nullptr;
  });
}

}  // namespace projecteur
