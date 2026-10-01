// SPDX-License-Identifier: MIT
#include <QtTest>

#include "effectstate.h"

using namespace projecteur;
using T = Command::Type;

namespace {
Command show(Mode m) { Command c{T::ShowAtPointer}; c.mode = m; return c; }
Command move(double dx, double dy) { Command c{T::MoveBy}; c.dx = dx; c.dy = dy; return c; }
Command setMode(Mode m) { Command c{T::SetMode}; c.mode = m; return c; }
Command fwd(RemoteKey k) { Command c{T::ForwardKey}; c.key = k; return c; }
const Command hide{T::Hide};
const Command recenter{T::Recenter};
}  // namespace

class EffectStateTest : public QObject {
  Q_OBJECT

 private slots:
  void holdShowsAtPointerAndMovementFollowsTheRemote() {
    EffectState s;
    QCOMPARE(s.holdDown(), (Commands{show(Mode::Highlight)}));
    QVERIFY(s.visible() && s.holding());
    QCOMPARE(s.rawMove(10, -4), (Commands{move(10, -4)}));
    QCOMPARE(s.rawMove(0, 0), Commands{});  // nothing to do for a zero delta
  }

  void releaseKeepsTheEffectWhereItIs_andHoldingAgainContinues() {
    EffectState s;  // freeze is on by default (reference configuration)
    s.holdDown();
    s.rawMove(5, 5);
    QCOMPARE(s.holdUp(), Commands{});  // frozen: no Hide
    QVERIFY(s.visible() && !s.holding());
    QCOMPARE(s.rawMove(7, 7), Commands{});  // ignored while the button is up
    QCOMPARE(s.holdDown(), Commands{});     // continues from the frozen position: no new ShowAtPointer
    QCOMPARE(s.rawMove(3, 2), (Commands{move(3, 2)}));
  }

  void withoutFreezeTheEffectIsHiddenOnRelease() {
    EffectSettings cfg;
    cfg.freeze = false;
    EffectState s(cfg);
    s.holdDown();
    QCOMPARE(s.holdUp(), (Commands{hide}));
    QVERIFY(!s.visible());
    QCOMPARE(s.holdUp(), Commands{});  // a second release changes nothing
  }

  void shortClickHidesAndTheNextHoldShowsAtThePointerAgain() {
    EffectState s;
    s.holdDown();
    s.holdUp();
    QCOMPARE(s.shortClick(), (Commands{hide}));
    QVERIFY(!s.visible());
    QCOMPARE(s.shortClick(), Commands{});  // nothing visible: nothing to hide
    QCOMPARE(s.holdDown(), (Commands{show(Mode::Highlight)}));  // starts at the pointer, not at the old position
  }

  void doubleClickCyclesHighlightMagnifyLaserAndWraps() {
    EffectState s;
    QCOMPARE(s.mode(), Mode::Highlight);
    QCOMPARE(s.doubleClick(), (Commands{setMode(Mode::Magnify)}));
    QCOMPARE(s.doubleClick(), (Commands{setMode(Mode::Laser)}));
    QCOMPARE(s.doubleClick(), (Commands{setMode(Mode::Highlight)}));
    QCOMPARE(s.holdDown(), (Commands{show(Mode::Highlight)}));
  }

  void doubleClickHidesAVisibleEffectFirst() {
    EffectState s;
    s.holdDown();
    s.holdUp();  // frozen and visible
    QCOMPARE(s.doubleClick(), (Commands{hide, setMode(Mode::Magnify)}));
    QVERIFY(!s.visible());
    QCOMPARE(s.holdDown(), (Commands{show(Mode::Magnify)}));
  }

  void cycleSkipsModesThatAreSwitchedOff() {
    EffectSettings cfg;
    cfg.modeEnabled = {true, false, true};  // Magnify unchecked
    EffectState s(cfg);
    QCOMPARE(s.doubleClick(), (Commands{setMode(Mode::Laser)}));
    QCOMPARE(s.doubleClick(), (Commands{setMode(Mode::Highlight)}));
  }

  void aSwitchedOffCurrentModeIsReplacedWhenSettingsChange() {
    EffectState s;
    s.doubleClick();  // Magnify
    EffectSettings cfg = s.settings();
    cfg.modeEnabled = {true, false, true};
    s.setSettings(cfg);
    QCOMPARE(s.mode(), Mode::Laser);
  }

  void nothingHappensWhenAllEffectsAreOff() {
    EffectSettings cfg;
    cfg.modeEnabled = {false, false, false};
    EffectState s(cfg);
    QCOMPARE(s.holdDown(), Commands{});
    QVERIFY(!s.visible());
    QCOMPARE(s.doubleClick(), Commands{});
  }

  void nextAndBackAreForwardedAndRecenterAVisibleEffect() {
    EffectState s;
    QCOMPARE(s.key(RemoteKey::Next), (Commands{fwd(RemoteKey::Next)}));  // hidden: no recenter
    s.holdDown();
    s.holdUp();
    QCOMPARE(s.key(RemoteKey::Back), (Commands{fwd(RemoteKey::Back), recenter}));
  }

  void recenterCanBeSwitchedOff() {
    EffectSettings cfg;
    cfg.recenter = false;
    EffectState s(cfg);
    s.holdDown();
    QCOMPARE(s.key(RemoteKey::Next), (Commands{fwd(RemoteKey::Next)}));
  }

  void keysStillWorkWhileTheActionButtonIsHeld() {  // measured: Next/Back arrive during a hold
    EffectState s;
    s.holdDown();
    const Commands out = s.key(RemoteKey::Next);
    QCOMPARE(out.front(), fwd(RemoteKey::Next));
  }

  void movementIsScaledByTheGain() {
    EffectSettings cfg;
    cfg.pixelsPerCount = 2.5;
    EffectState s(cfg);
    s.holdDown();
    QCOMPARE(s.rawMove(4, -2), (Commands{move(10, -5)}));
  }

  void ordinaryMouseClickAfterAFrozenEffectOnlyHidesIt() {
    // measured sequence: hold, release (frozen), short click, hold
    EffectState s;
    s.holdDown(); s.rawMove(1, 1); s.holdUp();
    QCOMPARE(s.shortClick(), (Commands{hide}));
    QCOMPARE(s.holdDown(), (Commands{show(Mode::Highlight)}));
  }
};

QTEST_APPLESS_MAIN(EffectStateTest)
#include "test_effectstate.moc"
