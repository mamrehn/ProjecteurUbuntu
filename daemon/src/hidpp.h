// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QList>
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
  PresenterControl = 0x1a00,       ///< vibration (original Spotlight)
  ReprogramControlsV4 = 0x1b04,    ///< diverted buttons and raw X/Y
  WirelessDeviceStatus = 0x1db4,
};

/// Control ids of the Spotlight's action button.
constexpr uint16_t kCidHold = 0x00d8;         ///< the action button held down
constexpr uint16_t kCidDoubleClick = 0x00df;  ///< two quick presses

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

// --- notifications ----------------------------------------------------------------------------
/// The action button controls that are currently held (event 0 of ReprogramControlsV4).
struct ButtonsChanged { QList<uint16_t> pressed; };
/// Movement while a control with raw X/Y diverted is held (event 1), in the remote's counts.
struct RawMove { int dx; int dy; };
/// The remote woke up or went away (WirelessDeviceStatus); the daemon applies the diversions again.
struct DeviceStatus { bool awake; };
/// The device rejected a request (HID++ 2.0 error response).
struct ErrorReply { uint8_t featureIndex; uint8_t functionAndSw; uint8_t code; };

using Event = std::variant<ButtonsChanged, RawMove, DeviceStatus, ErrorReply>;

/// Decode a message from the remote. `reprogIndex` / `wirelessIndex` are the feature indices found at run time
/// (0 = unknown). Returns nothing for messages that are not events (answers to requests, noise).
std::optional<Event> decode(const QByteArray& msg, uint8_t reprogIndex, uint8_t wirelessIndex = 0);

/// Does `reply` answer `request` (same device, feature and function/software id)? Errors count as answers.
bool answers(const QByteArray& request, const QByteArray& reply);

QString toHex(const QByteArray& msg);

}  // namespace projecteur::hidpp
