// SPDX-License-Identifier: MIT
#pragma once

#include <QDBusConnection>
#include <QObject>

#include "effectstate.h"

namespace projecteur {

/// Where overlay commands go. The real one is OverlayClient; tests record them.
class OverlaySink {
 public:
  virtual ~OverlaySink() = default;
  /// Handle the overlay-related commands (everything except ForwardKey, which the caller handles).
  virtual void apply(const Commands& commands) = 0;
};

/// Talks to the GNOME Shell extension (org.projecteur.Overlay1). Calls are asynchronous and never block the daemon;
/// if the extension is not running they are dropped (the user sees a warning once). The movement while the button is
/// held is sent without asking for a reply: one D-Bus round trip and one watcher less per report, on both sides.
class OverlayClient : public QObject, public OverlaySink {
  Q_OBJECT
 public:
  explicit OverlayClient(const QDBusConnection& bus, QObject* parent = nullptr);

  void apply(const Commands& commands) override;

 private:
  void call(const QString& method, const QVariantList& arguments = {});
  void post(const QString& method, const QVariantList& arguments);  ///< no reply wanted (the movement, ~100 a second)

  QDBusConnection bus_;
  bool warned_ = false;
};

QString toString(Mode mode);  ///< "highlight" / "magnify" / "laser", the names the extension understands

}  // namespace projecteur
