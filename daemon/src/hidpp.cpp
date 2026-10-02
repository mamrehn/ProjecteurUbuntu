// SPDX-License-Identifier: MIT
#include "hidpp.h"

#include <QString>

namespace projecteur::hidpp {

namespace {
constexpr uint8_t kShortReport = 0x10;
constexpr uint8_t kLongReport = 0x11;
constexpr uint8_t kErrorFeature = 0xff;
constexpr uint8_t kDeviceConnection = 0x41;  ///< HID++ 1.0 notification of the receiver

QByteArray message(uint8_t report, int size, uint8_t dev, uint8_t featureIndex, uint8_t function,
                   std::initializer_list<uint8_t> params) {
  QByteArray m(size, '\0');
  m[0] = static_cast<char>(report);
  m[1] = static_cast<char>(dev);
  m[2] = static_cast<char>(featureIndex);
  m[3] = static_cast<char>((function << 4) | kSoftwareId);
  int i = 4;
  for (uint8_t p : params) m[i++] = static_cast<char>(p);
  return m;
}
}  // namespace

QByteArray getFeatureIndex(Feature f, uint8_t dev, bool longForm) {
  const auto id = static_cast<uint16_t>(f);
  return longForm ? message(kLongReport, 20, dev, 0x00, 0, {static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id & 0xff)})
                  : message(kShortReport, 7, dev, 0x00, 0, {static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id & 0xff)});
}

QByteArray setCidReporting(uint8_t featureIndex, uint16_t cid, uint8_t flags, uint8_t dev) {
  return message(kLongReport, 20, dev, featureIndex, 3, {static_cast<uint8_t>(cid >> 8), static_cast<uint8_t>(cid & 0xff), flags});
}

QByteArray vibrate(uint8_t featureIndex, uint8_t length, uint8_t intensity, uint8_t dev) {
  return message(kLongReport, 20, dev, featureIndex, 1, {length, 0xe8, intensity});
}

QByteArray getBatteryStatus(uint8_t featureIndex, uint8_t dev, bool longForm) {
  return message(longForm ? kLongReport : kShortReport, longForm ? 20 : 7, dev, featureIndex, 0, {});
}

QByteArray getFirmwareEntityCount(uint8_t featureIndex, uint8_t dev, bool longForm) {
  return message(longForm ? kLongReport : kShortReport, longForm ? 20 : 7, dev, featureIndex, 0, {});
}

QByteArray getFirmwareInfo(uint8_t featureIndex, uint8_t entity, uint8_t dev, bool longForm) {
  return message(longForm ? kLongReport : kShortReport, longForm ? 20 : 7, dev, featureIndex, 1, {entity});
}

QByteArray setPointerSpeed(uint8_t featureIndex, uint8_t level, uint8_t dev) {
  return message(kLongReport, 20, dev, featureIndex, 1, {level, 0x00});
}

QByteArray getPointerSpeed(uint8_t featureIndex, uint8_t dev) { return message(kLongReport, 20, dev, featureIndex, 0, {}); }

std::optional<BatteryStatus> parseBatteryStatus(const QByteArray& msg) {
  if (msg.size() < 7) return std::nullopt;
  const auto b = [&](int i) { return static_cast<uint8_t>(msg[i]); };
  BatteryStatus s;
  s.percent = qMin<int>(b(4), 100);
  s.nextPercent = qMin<int>(b(5), 100);
  s.state = b(6) <= static_cast<uint8_t>(BatteryState::ChargingError) ? static_cast<BatteryState>(b(6)) : BatteryState::Unknown;
  return s;
}

bool isCharging(BatteryState s) {
  return s == BatteryState::Charging || s == BatteryState::AlmostFull || s == BatteryState::Full || s == BatteryState::SlowCharging;
}

QString batteryStateName(BatteryState s) {
  switch (s) {
    case BatteryState::Discharging: return QStringLiteral("discharging");
    case BatteryState::Charging: return QStringLiteral("charging");
    case BatteryState::AlmostFull: return QStringLiteral("almost full");
    case BatteryState::Full: return QStringLiteral("full");
    case BatteryState::SlowCharging: return QStringLiteral("charging slowly");
    case BatteryState::InvalidBattery: return QStringLiteral("invalid battery");
    case BatteryState::ThermalError: return QStringLiteral("thermal error");
    case BatteryState::ChargingError: return QStringLiteral("charging error");
    case BatteryState::Unknown: break;
  }
  return QStringLiteral("unknown");
}

int parseFirmwareEntityCount(const QByteArray& msg) {
  return msg.size() >= 5 ? static_cast<uint8_t>(msg[4]) : -1;
}

std::optional<FirmwareEntity> parseFirmwareInfo(const QByteArray& msg) {
  if (msg.size() < 12) return std::nullopt;
  const auto b = [&](int i) { return static_cast<uint8_t>(msg[i]); };
  FirmwareEntity e;
  e.type = b(4) & 0x0f;
  e.name = QString::fromLatin1(msg.mid(5, 3)).trimmed();
  // the Windows app shows 1.1.32 for the bytes 01 01 00 32: the digits of each number are read as hexadecimal
  e.version = QStringLiteral("%1.%2.%3").arg(b(8), 0, 16).arg(b(9), 0, 16).arg((b(10) << 8) | b(11), 0, 16);
  return e;
}

std::optional<Event> decode(const QByteArray& msg, uint8_t reprogIndex, uint8_t wirelessIndex, uint8_t batteryIndex) {
  if (msg.size() < 7) return std::nullopt;
  const auto b = [&](int i) { return static_cast<uint8_t>(msg[i]); };
  if (b(0) != kShortReport && b(0) != kLongReport) return std::nullopt;

  if (b(2) == kErrorFeature && msg.size() >= 6) return ErrorReply{b(3), b(4), b(5)};

  // The receiver's HID++ 1.0 "device connection" notification (sub id 0x41). Byte 3 is not a function / software id
  // here but the link's protocol type (non-zero on a real receiver), so this must come before the software id check.
  // Bit 6 of byte 4 set = the link is not established (the remote went away), per upstream's notes.
  if (b(0) == kShortReport && b(2) == kDeviceConnection) return DeviceStatus{(b(4) & 0x40) == 0, b(1)};

  // Answers to our own requests carry our software id; notifications have software id 0.
  if ((b(3) & 0x0f) != 0) return std::nullopt;

  if (batteryIndex != 0 && b(2) == batteryIndex && (b(3) >> 4) == 0) {
    if (const auto status = parseBatteryStatus(msg)) return BatteryChanged{*status};
    return std::nullopt;
  }

  if (reprogIndex != 0 && b(2) == reprogIndex && b(0) == kLongReport && msg.size() >= 8) {
    const uint8_t event = b(3) >> 4;
    if (event == 0) {  // diverted buttons: up to four control ids of the controls currently held
      ButtonsChanged e;
      for (int i = 4; i + 1 < qMin<qsizetype>(msg.size(), 12); i += 2) {
        const uint16_t cid = static_cast<uint16_t>((b(i) << 8) | b(i + 1));
        if (cid != 0) e.pressed.append(cid);
      }
      return e;
    }
    if (event == 1) {  // raw X/Y: two signed 16 bit counts, big endian
      return RawMove{static_cast<int16_t>((b(4) << 8) | b(5)), static_cast<int16_t>((b(6) << 8) | b(7))};
    }
  }

  // WirelessDeviceStatus (HID++ 2.0, long): only ever sent when the remote becomes active.
  if (wirelessIndex != 0 && b(2) == wirelessIndex) return DeviceStatus{true, b(1)};
  return std::nullopt;
}

bool isHidpp(const QByteArray& msg) {
  return !msg.isEmpty() && (static_cast<uint8_t>(msg[0]) == kShortReport || static_cast<uint8_t>(msg[0]) == kLongReport);
}

bool answers(const QByteArray& request, const QByteArray& reply) {
  if (request.size() < 4 || reply.size() < 5) return false;
  if (reply[1] != request[1]) return false;
  if (reply[2] == static_cast<char>(kErrorFeature)) return reply[3] == request[2] && reply[4] == request[3];
  return reply[2] == request[2] && reply[3] == request[3];
}

QString toHex(const QByteArray& msg) { return QString::fromLatin1(msg.toHex(' ')); }

}  // namespace projecteur::hidpp
