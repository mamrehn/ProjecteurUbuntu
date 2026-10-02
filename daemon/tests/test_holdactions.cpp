// SPDX-License-Identifier: MIT
#include <QSignalSpy>
#include <QtTest>
#include <linux/input.h>

#include "holdactions.h"

using namespace projecteur;

namespace {
struct RecordingKeys : KeySink {
  void key(int code, bool down) override { events.append({code, down}); }
  QList<int> taps() const {   // key codes in order of pressing
    QList<int> out;
    for (const auto& e : events) if (e.second) out.append(e.first);
    return out;
  }
  QList<QPair<int, bool>> events;
};
struct RecordingPointer : PointerSink {
  void scroll(int n) override { notches.append(n); }
  QList<int> notches;
};

Config with(HoldAction next, HoldAction back = HoldAction::None) {
  Config c;
  c.holdNext = next;
  c.holdBack = back;
  return c;
}
}  // namespace

class HoldActionsTest : public QObject {
  Q_OBJECT

 private slots:
  void startPresentationPressesF5OnceAndSaysSo() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setConfig(with(HoldAction::StartPresentation));
    QSignalSpy started(&a, &HoldActions::presentationStarted);
    a.down(Side::Next);
    a.up(Side::Next);
    QCOMPARE(k.taps(), (QList<int>{KEY_F5}));
    QCOMPARE(started.count(), 1);
  }

  void blankScreenPressesB() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setConfig(with(HoldAction::None, HoldAction::BlankScreen));
    a.down(Side::Back);
    a.up(Side::Back);
    QCOMPARE(k.taps(), (QList<int>{KEY_B}));
  }

  void noActionDoesNothing() {
    RecordingKeys k;
    RecordingPointer p;
    HoldActions a(&k, &p);
    a.setConfig(with(HoldAction::None));
    a.down(Side::Next);
    a.move(Side::Next, 0, -500);
    a.up(Side::Next);
    QVERIFY(k.events.isEmpty());
    QVERIFY(p.notches.isEmpty());
  }

  void fastForwardRepeatsWhileHeldAndStopsOnRelease() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setRepeatMs(20);
    a.setConfig(with(HoldAction::FastForward));
    a.down(Side::Next);
    QCOMPARE(k.taps(), (QList<int>{KEY_RIGHT}));   // at once
    QTRY_VERIFY(k.taps().size() >= 4);
    a.up(Side::Next);
    const int atRelease = k.taps().size();
    QTest::qWait(80);
    QCOMPARE(k.taps().size(), atRelease);
    for (int code : k.taps()) QCOMPARE(code, int(KEY_RIGHT));
  }

  void aProfileSwitchDuringAFastForwardStopsTheRepeat() {
    // the focused application changed and its profile holds Next for "nothing": the slides must stop moving
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setRepeatMs(20);
    a.setConfig(with(HoldAction::FastForward));
    a.down(Side::Next);
    QTRY_VERIFY(k.taps().size() >= 3);
    a.setConfig(with(HoldAction::None));
    const qsizetype before = k.taps().size();
    QTest::qWait(100);
    QCOMPARE(k.taps().size(), before);
    a.setConfig(with(HoldAction::FastForward));   // switching back does not resume a hold that is half over
    QTest::qWait(100);
    QCOMPARE(k.taps().size(), before);
  }

  void fastRewindRepeatsBack() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setRepeatMs(20);
    a.setConfig(with(HoldAction::None, HoldAction::FastRewind));
    a.down(Side::Back);
    QTRY_VERIFY(k.taps().size() >= 3);
    a.up(Side::Back);
    for (int code : k.taps()) QCOMPARE(code, int(KEY_LEFT));
  }

  void volumeFollowsTheUpAndDownMovementInSteps() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setConfig(with(HoldAction::Volume));
    a.down(Side::Next);
    a.move(Side::Next, 0, -HoldActions::kCountsPerVolumeStep);       // turned up: one step louder
    QCOMPARE(k.taps(), (QList<int>{KEY_VOLUMEUP}));
    a.move(Side::Next, 5, 2 * HoldActions::kCountsPerVolumeStep);    // turned down: two steps quieter
    QCOMPARE(k.taps(), (QList<int>{KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_VOLUMEDOWN}));
    a.up(Side::Next);
  }

  void smallMovementsAddUpInsteadOfBeingLost() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setConfig(with(HoldAction::Volume));
    a.down(Side::Next);
    for (int i = 0; i < 10; ++i) a.move(Side::Next, 0, -15);   // ten reports of 15 counts
    QCOMPARE(k.taps(), (QList<int>{KEY_VOLUMEUP}));
  }

  void everyHoldStartsFromZero() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setConfig(with(HoldAction::Volume));
    a.down(Side::Next);
    a.move(Side::Next, 0, -100);   // not enough for a step
    a.up(Side::Next);
    a.down(Side::Next);
    a.move(Side::Next, 0, -100);   // does not add to the first hold
    QVERIFY(k.events.isEmpty());
  }

  void scrollingTurnsDownWhenTheRemoteTurnsDown() {
    RecordingKeys k;
    RecordingPointer p;
    HoldActions a(&k, &p);
    a.setConfig(with(HoldAction::None, HoldAction::Scroll));
    a.down(Side::Back);
    a.move(Side::Back, 0, 2 * HoldActions::kCountsPerScrollNotch);      // down: content moves up, wheel notches negative
    a.move(Side::Back, 0, -3 * HoldActions::kCountsPerScrollNotch);
    QCOMPARE(p.notches, (QList<int>{-2, 3}));
    QVERIFY(k.events.isEmpty());
  }

  void aShortcutIsAChordPressedInOrderAndReleasedInReverse() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    Config c = with(HoldAction::Shortcut);
    c.shortcutNext = {KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_P};
    a.setConfig(c);
    a.down(Side::Next);
    using E = QPair<int, bool>;
    QCOMPARE(k.events, (QList<E>{{KEY_LEFTCTRL, true}, {KEY_LEFTSHIFT, true}, {KEY_P, true},
                                 {KEY_P, false}, {KEY_LEFTSHIFT, false}, {KEY_LEFTCTRL, false}}));
  }

  void aShortcutWithoutKeysDoesNothing() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setConfig(with(HoldAction::Shortcut));
    a.down(Side::Next);
    QVERIFY(k.events.isEmpty());
  }

  void movementOfTheOtherSideAndOfNothingHeldIsIgnored() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setConfig(with(HoldAction::Volume, HoldAction::Volume));
    a.move(Side::Next, 0, -1000);          // nothing held
    a.down(Side::Next);
    a.move(Side::Back, 0, -1000);          // the other key
    QVERIFY(k.events.isEmpty());
  }

  void aSecondHoldWhileOneIsActiveIsIgnored() {
    RecordingKeys k;
    HoldActions a(&k, nullptr);
    a.setConfig(with(HoldAction::StartPresentation, HoldAction::BlankScreen));
    a.down(Side::Next);
    a.down(Side::Back);
    QCOMPARE(k.taps(), (QList<int>{KEY_F5}));
  }
};

QTEST_MAIN(HoldActionsTest)
#include "test_holdactions.moc"
