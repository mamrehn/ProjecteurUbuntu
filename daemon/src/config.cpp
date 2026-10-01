// SPDX-License-Identifier: MIT
#include "config.h"

#include "hidpp.h"

#include <QJsonArray>
#include <cmath>

namespace projecteur {

namespace {
struct ActionName { HoldAction action; const char* name; };
constexpr ActionName kActions[] = {
    {HoldAction::None, "none"},
    {HoldAction::StartPresentation, "start-presentation"},
    {HoldAction::BlankScreen, "blank-screen"},
    {HoldAction::FastForward, "fast-forward"},
    {HoldAction::FastRewind, "fast-rewind"},
    {HoldAction::Volume, "volume"},
    {HoldAction::Scroll, "scroll"},
    {HoldAction::Shortcut, "shortcut"},
};

constexpr int kMaxAlerts = 3;
constexpr int kMaxTimerMinutes = 600;

QJsonArray toArray(const QList<int>& v) {
  QJsonArray a;
  for (int x : v) a.append(x);
  return a;
}

bool readInt(const QJsonValue& v, int min, int max, int* out, const QString& key, QStringList* problems) {
  if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble())) {
    if (problems) problems->append(QStringLiteral("%1: expected a whole number").arg(key));
    return false;
  }
  const double d = v.toDouble();
  if (d < min || d > max) {
    if (problems) problems->append(QStringLiteral("%1: %2 is outside %3 to %4").arg(key).arg(d).arg(min).arg(max));
    return false;
  }
  *out = static_cast<int>(d);
  return true;
}

bool readBool(const QJsonValue& v, bool* out, const QString& key, QStringList* problems) {
  if (!v.isBool()) {
    if (problems) problems->append(QStringLiteral("%1: expected true or false").arg(key));
    return false;
  }
  *out = v.toBool();
  return true;
}

bool readIntList(const QJsonValue& v, int min, int max, int maxCount, QList<int>* out, const QString& key, QStringList* problems) {
  if (!v.isArray() || v.toArray().size() > maxCount) {
    if (problems) problems->append(QStringLiteral("%1: expected a list of at most %2 numbers").arg(key).arg(maxCount));
    return false;
  }
  QList<int> result;
  for (const QJsonValue& item : v.toArray()) {
    int x = 0;
    if (!readInt(item, min, max, &x, key, problems)) return false;
    result.append(x);
  }
  *out = result;
  return true;
}

bool readAction(const QJsonValue& v, HoldAction* out, const QString& key, QStringList* problems) {
  const auto action = v.isString() ? holdActionFromName(v.toString()) : std::nullopt;
  if (!action) {
    if (problems) problems->append(QStringLiteral("%1: unknown action").arg(key));
    return false;
  }
  *out = *action;
  return true;
}
}  // namespace

QString holdActionName(HoldAction action) {
  for (const auto& a : kActions)
    if (a.action == action) return QString::fromLatin1(a.name);
  return QStringLiteral("none");
}

std::optional<HoldAction> holdActionFromName(const QString& name) {
  for (const auto& a : kActions)
    if (name == QLatin1String(a.name)) return a.action;
  return std::nullopt;
}

double Config::pixelsPerCount() const { return qMax(pointerSpeed, 1) / 35.0; }

EffectSettings Config::effectSettings() const {
  EffectSettings s;
  s.modeEnabled = cursorControl ? std::array<bool, kModeCount>{false, false, false} : modes;
  s.freeze = freeze;
  s.recenter = recenter;
  s.pixelsPerCount = pixelsPerCount();
  return s;
}

// percent x 2.55 rounded to the nearest whole number, in integers (50 % is 0x80 like the Windows app, not 127.4999)
uint8_t Config::vibrationIntensity() const { return static_cast<uint8_t>((qBound(0, vibrationPercent, 100) * 255 + 50) / 100); }

uint8_t Config::pointerSpeedLevel() const {
  return static_cast<uint8_t>(hidpp::kPointerSpeedMin + std::lround(qBound(0, pointerSpeed, 100) / 100.0 * (hidpp::kPointerSpeedMax - hidpp::kPointerSpeedMin)));
}

QJsonObject Config::toJson() const {
  QJsonObject o;
  o[QStringLiteral("highlight-enabled")] = modes[0];
  o[QStringLiteral("magnify-enabled")] = modes[1];
  o[QStringLiteral("laser-enabled")] = modes[2];
  o[QStringLiteral("freeze-effects")] = freeze;
  o[QStringLiteral("recenter-effects")] = recenter;
  o[QStringLiteral("cursor-control")] = cursorControl;
  o[QStringLiteral("pointer-speed")] = pointerSpeed;
  o[QStringLiteral("hold-next-action")] = holdActionName(holdNext);
  o[QStringLiteral("hold-back-action")] = holdActionName(holdBack);
  o[QStringLiteral("hold-next-shortcut")] = toArray(shortcutNext);
  o[QStringLiteral("hold-back-shortcut")] = toArray(shortcutBack);
  o[QStringLiteral("vibration-intensity")] = vibrationPercent;
  o[QStringLiteral("battery-warning")] = batteryWarning;
  o[QStringLiteral("timer-notification")] = timerNotification;
  o[QStringLiteral("timer-enabled")] = timerEnabled;
  o[QStringLiteral("timer-minutes")] = timerMinutes;
  o[QStringLiteral("timer-auto-start")] = timerAutoStart;
  o[QStringLiteral("timer-alerts")] = toArray(timerAlerts);
  return o;
}

Config Config::fromJson(const QJsonObject& json, const Config& base, QStringList* problems) {
  Config c = base;
  for (auto it = json.begin(); it != json.end(); ++it) {
    const QString key = it.key();
    const QJsonValue v = it.value();
    if (key == QLatin1String("highlight-enabled")) readBool(v, &c.modes[0], key, problems);
    else if (key == QLatin1String("magnify-enabled")) readBool(v, &c.modes[1], key, problems);
    else if (key == QLatin1String("laser-enabled")) readBool(v, &c.modes[2], key, problems);
    else if (key == QLatin1String("freeze-effects")) readBool(v, &c.freeze, key, problems);
    else if (key == QLatin1String("recenter-effects")) readBool(v, &c.recenter, key, problems);
    else if (key == QLatin1String("cursor-control")) readBool(v, &c.cursorControl, key, problems);
    else if (key == QLatin1String("pointer-speed")) readInt(v, 0, 100, &c.pointerSpeed, key, problems);
    else if (key == QLatin1String("hold-next-action")) readAction(v, &c.holdNext, key, problems);
    else if (key == QLatin1String("hold-back-action")) readAction(v, &c.holdBack, key, problems);
    else if (key == QLatin1String("hold-next-shortcut")) readIntList(v, 1, 0x2ff, 6, &c.shortcutNext, key, problems);
    else if (key == QLatin1String("hold-back-shortcut")) readIntList(v, 1, 0x2ff, 6, &c.shortcutBack, key, problems);
    else if (key == QLatin1String("vibration-intensity")) readInt(v, 0, 100, &c.vibrationPercent, key, problems);
    else if (key == QLatin1String("battery-warning")) readBool(v, &c.batteryWarning, key, problems);
    else if (key == QLatin1String("timer-notification")) readBool(v, &c.timerNotification, key, problems);
    else if (key == QLatin1String("timer-enabled")) readBool(v, &c.timerEnabled, key, problems);
    else if (key == QLatin1String("timer-minutes")) readInt(v, 1, kMaxTimerMinutes, &c.timerMinutes, key, problems);
    else if (key == QLatin1String("timer-auto-start")) readBool(v, &c.timerAutoStart, key, problems);
    else if (key == QLatin1String("timer-alerts")) readIntList(v, 0, kMaxTimerMinutes, kMaxAlerts, &c.timerAlerts, key, problems);
    // everything else (appearance, profiles, ...) is for other consumers of the same settings: ignored on purpose
  }
  return c;
}

}  // namespace projecteur
