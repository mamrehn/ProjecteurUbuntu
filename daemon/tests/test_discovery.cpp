// SPDX-License-Identifier: MIT
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "discovery.h"

using namespace projecteur;

namespace {
// Excerpt in the format of /proc/bus/input/devices: the USB receiver (several nodes), a Bluetooth Spotlight and
// a few unrelated devices.
const char* kProc = R"(I: Bus=0011 Vendor=0001 Product=0001 Version=ab41
N: Name="AT Translated Set 2 keyboard"
P: Phys=isa0060/serio0/input0
H: Handlers=sysrq kbd event2 leds

I: Bus=0003 Vendor=046d Product=c53e Version=0111
N: Name="Logitech USB Receiver"
P: Phys=usb-0000:00:14.0-4/input0
H: Handlers=sysrq kbd event5 leds

I: Bus=0003 Vendor=046d Product=c53e Version=0111
N: Name="Logitech USB Receiver Mouse"
P: Phys=usb-0000:00:14.0-4/input1
H: Handlers=mouse2 event6

I: Bus=0003 Vendor=046d Product=c53e Version=0111
N: Name="Logitech USB Receiver Consumer Control"
P: Phys=usb-0000:00:14.0-4/input1
H: Handlers=kbd event7

I: Bus=0005 Vendor=046d Product=b503 Version=0032
N: Name="SPOTLIGHT Keyboard"
P: Phys=40:1c:83:3b:81:f6
H: Handlers=sysrq kbd event14
)";

// The Bluetooth Spotlight exactly as /proc/bus/input/devices listed it on the test machine (2026-10-01).
const char* kProcBluetooth = R"(I: Bus=0005 Vendor=046d Product=b503 Version=0032
N: Name="SPOTLIGHT Keyboard"
P: Phys=40:1c:83:3b:81:f6
S: Sysfs=/devices/virtual/misc/uhid/0005:046D:B503.0005/input/input21
U: Uniq=de:4e:1c:b5:7c:96
H: Handlers=sysrq kbd event5
B: PROP=0

I: Bus=0005 Vendor=046d Product=b503 Version=0032
N: Name="SPOTLIGHT Mouse"
P: Phys=40:1c:83:3b:81:f6
S: Sysfs=/devices/virtual/misc/uhid/0005:046D:B503.0005/input/input22
U: Uniq=de:4e:1c:b5:7c:96
H: Handlers=mouse2 event6
B: PROP=0
)";

void write(const QString& path, const QByteArray& content) {
  QDir().mkpath(QFileInfo(path).path());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(content);
}
}  // namespace

class DiscoveryTest : public QObject {
  Q_OBJECT

 private slots:
  void findsKeyboardAndMouseNodesOfTheUsbReceiverOnly() {
    const SpotlightNodes n = findUsbReceiver(QString::fromLatin1(kProc), QStringLiteral("/nonexistent"));
    QCOMPARE(n.keyboard, QStringLiteral("/dev/input/event5"));
    QCOMPARE(n.mouse, QStringLiteral("/dev/input/event6"));  // not the consumer control (7), not the Bluetooth one (14)
    QVERIFY(n.hidraw.isEmpty());
    QVERIFY(!n.complete());
  }

  void findsTheHidppInterfaceBySysfsLayout() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    // /sys/devices/usb/<interface>/<hid device>/uevent and the hidraw class symlinks, like the real sysfs
    const QString devices = root.path() + QStringLiteral("/devices");
    for (const auto& [hidraw, iface, hid] : {std::tuple{"hidraw1", "00", "0003:046D:C53E.0005"},
                                             std::tuple{"hidraw2", "01", "0003:046D:C53E.0006"},
                                             std::tuple{"hidraw3", "02", "0003:046D:C53E.0007"}}) {
      const QString ifaceDir = devices + QStringLiteral("/3-4:1.") + QString::number(QString::fromLatin1(iface).toInt());
      write(ifaceDir + QStringLiteral("/bInterfaceNumber"), QByteArray(iface) + "\n");
      write(ifaceDir + QLatin1Char('/') + QString::fromLatin1(hid) + QStringLiteral("/uevent"),
            QByteArray("HID_ID=0003:0000046D:0000C53E\nHID_NAME=Logitech USB Receiver\n"));
      QDir().mkpath(root.path() + QStringLiteral("/class/hidraw/") + QString::fromLatin1(hidraw));
      QVERIFY(QFile::link(ifaceDir + QLatin1Char('/') + QString::fromLatin1(hid),
                          root.path() + QStringLiteral("/class/hidraw/") + QString::fromLatin1(hidraw) + QStringLiteral("/device")));
    }
    // an unrelated hidraw device must not match
    const QString other = devices + QStringLiteral("/1-2:1.2");
    write(other + QStringLiteral("/bInterfaceNumber"), "02\n");
    write(other + QStringLiteral("/0003:1234:5678.0001/uevent"), "HID_ID=0003:00001234:00005678\n");
    QDir().mkpath(root.path() + QStringLiteral("/class/hidraw/hidraw0"));
    QVERIFY(QFile::link(other + QStringLiteral("/0003:1234:5678.0001"), root.path() + QStringLiteral("/class/hidraw/hidraw0/device")));

    const SpotlightNodes n = findUsbReceiver(QString::fromLatin1(kProc), root.path() + QStringLiteral("/class/hidraw"));
    QCOMPARE(n.hidraw, QStringLiteral("/dev/hidraw3"));  // interface 02 of the receiver
    QVERIFY(n.complete());
  }

  void findsABluetoothSpotlightByItsNodeNamesAndHidrawId() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString hid = root.path() + QStringLiteral("/devices/uhid/0005:046D:B503.0005");
    write(hid + QStringLiteral("/uevent"), "HID_ID=0005:0000046D:0000B503\nHID_NAME=SPOTLIGHT\n");
    QDir().mkpath(root.path() + QStringLiteral("/class/hidraw/hidraw1"));
    QVERIFY(QFile::link(hid, root.path() + QStringLiteral("/class/hidraw/hidraw1/device")));
    // the touchpad's hidraw (hidraw0) must not match
    const QString pad = root.path() + QStringLiteral("/devices/i2c/0018:06CB:CE2D.0001");
    write(pad + QStringLiteral("/uevent"), "HID_ID=0018:000006CB:0000CE2D\n");
    QDir().mkpath(root.path() + QStringLiteral("/class/hidraw/hidraw0"));
    QVERIFY(QFile::link(pad, root.path() + QStringLiteral("/class/hidraw/hidraw0/device")));

    const SpotlightNodes n = findBluetoothSpotlight(QString::fromLatin1(kProcBluetooth), root.path() + QStringLiteral("/class/hidraw"));
    QCOMPARE(n.keyboard, QStringLiteral("/dev/input/event5"));
    QCOMPARE(n.mouse, QStringLiteral("/dev/input/event6"));
    QCOMPARE(n.hidraw, QStringLiteral("/dev/hidraw1"));
    QVERIFY(n.bluetooth && n.complete());
  }

  void theUsbFinderIgnoresTheBluetoothDevice() {
    QVERIFY(!findUsbReceiver(QString::fromLatin1(kProcBluetooth), QStringLiteral("/nonexistent")).complete());
    QVERIFY(findUsbReceiver(QString::fromLatin1(kProcBluetooth), QStringLiteral("/nonexistent")).keyboard.isEmpty());
  }

  void nothingFoundWithoutAReceiver() {
    const SpotlightNodes n = findUsbReceiver(QStringLiteral("I: Bus=0011 Vendor=0001 Product=0001\nN: Name=\"x\"\nH: Handlers=event2\n"));
    QVERIFY(!n.complete());
    QVERIFY(n.keyboard.isEmpty() && n.mouse.isEmpty());
  }
};

QTEST_APPLESS_MAIN(DiscoveryTest)
#include "test_discovery.moc"
