// SPDX-License-Identifier: MIT
#include "overlayclient.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcOverlay, "projecteur.overlay")

namespace projecteur {

QString toString(Mode mode) {
  switch (mode) {
    case Mode::Highlight: return QStringLiteral("highlight");
    case Mode::Magnify: return QStringLiteral("magnify");
    case Mode::Laser: return QStringLiteral("laser");
  }
  return QStringLiteral("highlight");
}

OverlayClient::OverlayClient(const QDBusConnection& bus, QObject* parent) : QObject(parent), bus_(bus) {}

void OverlayClient::call(const QString& method, const QVariantList& arguments) {
  QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.projecteur.Overlay"),
                                                    QStringLiteral("/org/projecteur/Overlay"),
                                                    QStringLiteral("org.projecteur.Overlay1"), method);
  msg.setArguments(arguments);
  auto* watcher = new QDBusPendingCallWatcher(bus_.asyncCall(msg, 2000), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher* w) {
    const QDBusPendingReply<> reply = *w;
    if (reply.isError()) {
      if (!warned_) {
        warned_ = true;
        qCWarning(lcOverlay).noquote()
            << QStringLiteral("The overlay did not accept %1: %2. Is the GNOME Shell extension "
                              "projecteur-overlay@mamrehn.github.io enabled?").arg(method, reply.error().message());
      }
    } else {
      warned_ = false;
    }
    w->deleteLater();
  });
}

void OverlayClient::apply(const Commands& commands) {
  for (const Command& c : commands) {
    switch (c.type) {
      case Command::Type::ShowAtPointer: call(QStringLiteral("ShowAtPointer"), {toString(c.mode)}); break;
      case Command::Type::MoveBy: call(QStringLiteral("MoveBy"), {c.dx, c.dy}); break;
      case Command::Type::Hide: call(QStringLiteral("Hide")); break;
      case Command::Type::SetMode: call(QStringLiteral("SetMode"), {toString(c.mode)}); break;
      case Command::Type::Recenter: call(QStringLiteral("Recenter")); break;
      case Command::Type::ForwardKey: break;  // not an overlay command
    }
  }
}

}  // namespace projecteur
