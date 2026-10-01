// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>
#include <optional>
#include <variant>

/// HID++ 2.0 messages for the Logitech Spotlight: building requests and decoding what the remote sends.
/// Only what the daemon needs; see doc/ubuntu/INPUT-MODEL.md for how each message was measured.
namespace projecteur::hidpp {

constexpr uint8_t kDeviceIndex = 0x01;        ///< the remote behind the USB receiver
constexpr uint8_t kDirectDeviceIndex = 0xff;  ///< a remote connected directly (Bluetooth)
constexpr uint8_t kSoftwareId = 0x0d;   ///< low nibble of byte 3 in our requests

enum class Feature : uint16_t {
  Root = 0x0000,
  FirmwareVersion = 0x0003,        ///< firmware entities (main firmware, bootloader, hardware)
  DeviceName = 0x0005,
  BatteryStatus = 0x1000,          ///< level in percent, in steps; the original Spotlight uses this one
  PresenterControl = 0x1a00,       ///< vibration (original Spotlight)
  ReprogramControlsV4 = 0x1b04,    ///< diverted buttons and raw X/Y
  WirelessDeviceStatus = 0x1db4,
  PointerSpeed = 0x2205,           ///< speed of the pointer movement the remote sends when it controls the cursor
};

/// Control ids of the Spotlight's action button.
constexpr uint16_t kCidHold = 0x00d8;         ///< the action button held down
constexpr uint16_t kCidDoubleClick = 0x00df;  ///< two quick presses
constexpr uint16_t kCidNextHold = 0x00da;     ///< the Next key held (upstream's notes: doc/LogitechSpotlightHID++.md)
constexpr uint16_t kCidBackHold = 0x00dc;     ///< the Back key held

/// Flags of setCidReporting (function 3). 0x01 divert, 0x10 also divert raw X/Y; 0x02 / 0x20 mark the bit valid.
constexpr uint8_t kDivert = 0x03;
constexpr uint8_t kDivertWithRawXY = 0x33;
constexpr uint8_t kUndivert = 0x22;

// --- requests ----------------------------------------------------------------------------------
/// Short (7 bytes) by default; `longForm` builds the 20 byte variant that the Bluetooth hidraw node requires
/// (its descriptor declares only report 0x11).
QByteArray getFeatureIndex(Feature f, uint8_t dev = kDeviceIndex, bool longForm = false);
QByteArray setCidReporting(uint8_t featureIndex, uint16_t cid, uint8_t flags, uint8_t dev = kDeviceIndex);  // long, 20
/// Vibration. length 0 is NOT felt on the original Spotlight; 1 is a short pulse.
QByteArray vibrate(uint8_t featureIndex, uint8_t length, uint8_t intensity, uint8_t dev = kDeviceIndex);   // long, 20

QByteArray getBatteryStatus(uint8_t featureIndex, uint8_t dev = kDeviceIndex, bool longForm = false);
QByteArray getFirmwareEntityCount(uint8_t featureIndex, uint8_t dev = kDeviceIndex, bool longForm = false);
QByteArray getFirmwareInfo(uint8_t featureIndex, uint8_t entity, uint8_t dev = kDeviceIndex, bool longForm = false);
/// Pointer speed level for the pointer movement the remote sends when it controls the cursor itself. Upstream's notes
/// give 0x10 .. 0x19 as the range and 0x14 as the default; the value is a 16 bit number with the level in the high byte.
QByteArray setPointerSpeed(uint8_t featureIndex, uint8_t level, uint8_t dev = kDeviceIndex);
QByteArray getPointerSpeed(uint8_t featureIndex, uint8_t dev = kDeviceIndex);  ///< the level is in byte 4 of the answer
constexpr uint8_t kPointerSpeedMin = 0x10, kPointerSpeedMax = 0x19, kPointerSpeedDefault = 0x14;

// --- answers that carry data --------------------------------------------------------------------
enum class BatteryState : uint8_t {
  Discharging = 0, Charging = 1, AlmostFull = 2, Full = 3, SlowCharging = 4, InvalidBattery = 5, ThermalError = 6,
  ChargingError = 7, Unknown = 255
};
struct BatteryStatus {
  int percent = -1;      ///< 0..100
  int nextPercent = -1;  ///< the level at which the remote reports next (it does not report continuously)
  BatteryState state = BatteryState::Unknown;
  bool operator==(const BatteryStatus&) const = default;
};
/// From the answer to getBatteryStatus, or from a battery notification: level, next level, state in bytes 4 to 6.
std::optional<BatteryStatus> parseBatteryStatus(const QByteArray& message);
bool isCharging(BatteryState state);
QString batteryStateName(BatteryState state);

struct FirmwareEntity {
  int type = -1;      ///< 0 main firmware, 1 bootloader, 2 hardware
  QString name;       ///< three letters, for example "MPO" (main firmware) or "BOT" (bootloader)
  QString version;    ///< as the Windows app shows it: major.minor.build in hex digits, for example "1.1.32"
};
/// Number of firmware entities, from the answer to getFirmwareEntityCount (-1 if it is not one).
int parseFirmwareEntityCount(const QByteArray& message);
std::optional<FirmwareEntity> parseFirmwareInfo(const QByteArray& message);

// --- notifications ----------------------------------------------------------------------------
/// The action button controls that are currently held (event 0 of ReprogramControlsV4).
struct ButtonsChanged { QList<uint16_t> pressed; };
/// Movement while a control with raw X/Y diverted is held (event 1), in the remote's counts.
struct RawMove { int dx; int dy; };
/// The remote woke up or went away (WirelessDeviceStatus); the daemon applies the diversions again.
struct DeviceStatus { bool awake; };
/// The device rejected a request (HID++ 2.0 error response).
struct ErrorReply { uint8_t featureIndex; uint8_t functionAndSw; uint8_t code; };

/// The battery level changed (the remote announces it when it crosses one of its steps, or starts/stops charging).
struct BatteryChanged { BatteryStatus status; };

using Event = std::variant<ButtonsChanged, RawMove, DeviceStatus, ErrorReply, BatteryChanged>;

/// Decode a message from the remote. `reprogIndex` / `wirelessIndex` / `batteryIndex` are the feature indices found
/// at run time (0 = unknown). Returns nothing for messages that are not events (answers to requests, noise).
std::optional<Event> decode(const QByteArray& msg, uint8_t reprogIndex, uint8_t wirelessIndex = 0, uint8_t batteryIndex = 0);

/// Is this a HID++ report (short 0x10 or long 0x11)? The Bluetooth hidraw node also carries the remote's ordinary
/// keyboard (report 1) and mouse (report 2) input, which is not ours to interpret.
bool isHidpp(const QByteArray& msg);

/// Does `reply` answer `request` (same device, feature and function/software id)? Errors count as answers.
bool answers(const QByteArray& request, const QByteArray& reply);

QString toHex(const QByteArray& msg);

}  // namespace projecteur::hidpp
