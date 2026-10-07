#!/usr/bin/env python3
"""Replay touch lifetimes through the production trackpad, with recorded mouse edges.

UIKit delivery is simulated; the real iOS build separately typechecks the view.
--source permits replaying the same regression against the previous revision.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=root / 'app/Madeira/ContentView.swift')
args = parser.parse_args()
source = args.source.read_text()
trackpad = source[source.index('    static var cursor = CGPoint('):source.index('\n/// Arrow-key button')]
stubs = r'''
import Foundation

enum Clock { static var now = 100.0 }
struct Date { var timeIntervalSinceReferenceDate: Double { Clock.now } }
struct DispatchTime {
    var value: Double
    static func now() -> Self { Self(value: Clock.now) }
    static func + (lhs: Self, rhs: Double) -> Self { Self(value: lhs.value + rhs) }
}
final class DispatchQueue {
    static let main = DispatchQueue()
    var tasks: [(Double, () -> Void)] = []
    func asyncAfter(deadline: DispatchTime, execute: @escaping () -> Void) {
        tasks.append((deadline.value, execute))
    }
    func advance(_ seconds: Double) {
        Clock.now += seconds
        let ready = tasks.filter { $0.0 <= Clock.now }
        tasks.removeAll { $0.0 <= Clock.now }
        for task in ready { task.1() }
    }
}
class UIView {
    func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {}
    func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {}
    func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {}
    func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {}
}
final class UITouch: Hashable {
    enum Phase { case began, moved, stationary, ended, cancelled }
    var phase = Phase.began
    var point = CGPoint(x: 40, y: 40)
    weak var view: UIView?
    init(_ view: UIView) { self.view = view }
    func location(in view: UIView) -> CGPoint { point }
    static func == (a: UITouch, b: UITouch) -> Bool { a === b }
    func hash(into hasher: inout Hasher) { hasher.combine(ObjectIdentifier(self)) }
}
final class UIEvent {
    var allTouches: Set<UITouch>?
    init(_ touches: UITouch...) { allTouches = Set(touches) }
}
final class InputSettings {
    static let shared = InputSettings()
    var relative = false
    var sensRel = 1.0, sensAbs = 1.0
}
final class HardwareInput {
    enum Phase { case began, moved, ended, cancelled }
    static let shared = HardwareInput()
    var intercept = false
    func interceptTouches(_ touches: Set<UITouch>, _ event: UIEvent?, _ phase: Phase) -> Bool { intercept }
}
struct UIImpactFeedbackGenerator {
    enum Style { case medium }
    init(style: Style) {}
    func impactOccurred() {}
}
var mouse: [UInt32] = []
func winios_pointer(_ x: Int32, _ y: Int32, _ flags: UInt32, _ data: UInt32) { mouse.append(flags) }
func winios_screen_size(_ w: inout Int32, _ h: inout Int32) { w = 1920; h = 1080 }
func winios_post_touch_down(_ x: Int32, _ y: Int32) { fatalError("unexpected direct input") }
func winios_post_touch_up(_ x: Int32, _ y: Int32) { fatalError("unexpected direct input") }
func winios_post_touch_move(_ x: Int32, _ y: Int32) { fatalError("unexpected direct input") }
class MetalBackedView: UIView {
    var touchPointerMode = false
    func touchModeBegan(_ t: Set<UITouch>) { fatalError("unexpected touch mode") }
    func touchModeMoved(_ t: Set<UITouch>, _ e: UIEvent?) { fatalError("unexpected touch mode") }
    func touchModeEnded(_ t: Set<UITouch>, _ e: UIEvent?) { fatalError("unexpected touch mode") }
    func touchModeCancelled(_ t: Set<UITouch>) { fatalError("unexpected touch mode") }
    func mapTouch(_ t: UITouch) -> (Int32, Int32) { (0, 0) }
'''
tests = r'''
func expect(_ want: [UInt32], _ reason: String) {
    if mouse != want {
        fputs("FAIL: \(reason): got \(mouse), expected \(want)\n", stderr)
        exit(1)
    }
}
func fresh() -> MetalBackedView {
    DispatchQueue.main.tasks.removeAll()
    mouse = []
    HardwareInput.shared.intercept = false
    InputSettings.shared.relative = false
    return MetalBackedView()
}
func startDrag(_ v: MetalBackedView) -> UITouch {
    let a = UITouch(v)
    v.touchesBegan([a], with: UIEvent(a))
    DispatchQueue.main.advance(0.51)
    expect([2], "long press sends left down")
    return a
}
func end(_ v: MetalBackedView, _ t: UITouch, _ event: UIEvent?) {
    t.phase = .ended
    v.touchesEnded([t], with: event)
}
setenv("MADEIRA_DESKTOP", "1", 1)

do {
    let v = fresh(), a = UITouch(v)
    v.touchesBegan([a], with: UIEvent(a))
    DispatchQueue.main.advance(0.1)
    end(v, a, UIEvent(a))
    DispatchQueue.main.advance(1)
    expect([2, 4], "tap stays a click; stale timer cannot press again")
}
do {
    let v = fresh(), a = startDrag(v)
    a.phase = .moved; a.point.x += 20
    v.touchesMoved([a], with: UIEvent(a))
    end(v, a, UIEvent(a))
    expect([2, 0x8001, 4], "one-finger drag/drop keeps pointer motion")
}
do {
    let v = fresh(), a = startDrag(v), b = UITouch(v)
    v.touchesBegan([b], with: UIEvent(a, b))
    end(v, a, UIEvent(a, b))
    end(v, b, UIEvent(b))
    expect([2, 4], "REGRESSION: drag owner lifts before second finger; release exactly once")
    let c = UITouch(v)
    v.touchesBegan([c], with: UIEvent(c))
    DispatchQueue.main.advance(0.1)
    end(v, c, UIEvent(c))
    expect([2, 4, 2, 4], "a new tap works after multi-finger drag")
}
do {
    let v = fresh(), a = startDrag(v), b = UITouch(v)
    v.touchesBegan([b], with: UIEvent(a, b))
    end(v, b, UIEvent(a, b))
    expect([2], "lifting the extra finger must not drop the drag")
    a.point.x += 20; a.phase = .moved
    v.touchesMoved([a], with: UIEvent(a))
    end(v, a, UIEvent(a))
    expect([2, 0x8001, 4], "owner keeps moving after extra finger lifts")
}
do {
    let v = fresh(), a = startDrag(v), b = UITouch(v)
    v.touchesBegan([b], with: UIEvent(a, b))
    a.phase = .ended; b.phase = .ended
    v.touchesEnded([a, b], with: UIEvent(a, b))
    expect([2, 4], "both fingers lift in one callback")
}
do {
    let v = fresh(), a = startDrag(v)
    a.phase = .ended // lost callback, as with an interrupted touch stream
    let b = UITouch(v)
    v.touchesBegan([b], with: UIEvent(b))
    DispatchQueue.main.advance(0.1)
    end(v, b, UIEvent(b))
    expect([2, 4, 2, 4], "new touch recovers an ended owner before clicking")
}
do {
    let v = fresh(), a = startDrag(v), b = UITouch(v)
    _ = a // owner missing from event, even if UIKit phase has not caught up
    v.touchesBegan([b], with: UIEvent(b))
    DispatchQueue.main.advance(0.1)
    end(v, b, UIEvent(b))
    expect([2, 4, 2, 4], "absent owner cannot strand the mouse button")
}
do {
    let v = fresh(), a = startDrag(v)
    HardwareInput.shared.intercept = true
    a.phase = .cancelled
    v.touchesCancelled([a], with: UIEvent(a))
    v.touchesCancelled([a], with: UIEvent(a))
    expect([2, 4], "cancellation releases before an input interceptor consumes it")
}
do {
    let v = fresh(), a = startDrag(v)
    v.touchPointerMode = true
    end(v, a, nil)
    expect([2, 4], "mode change cannot swallow the owner's release")
}
do {
    let v = fresh(), a = UITouch(v), b = UITouch(v)
    v.touchesBegan([a], with: UIEvent(a))
    DispatchQueue.main.advance(0.1)
    v.touchesBegan([b], with: UIEvent(a, b))
    end(v, a, UIEvent(a, b)); end(v, b, UIEvent(b))
    DispatchQueue.main.advance(1)
    expect([8, 16], "two-finger tap stays a right-click with no stale left down")
}
do {
    let v = fresh(), a = UITouch(v), b = UITouch(v)
    v.touchesBegan([a, b], with: UIEvent(a, b))
    a.point.y += 28; b.point.y += 28
    v.touchesMoved([a, b], with: UIEvent(a, b))
    end(v, a, UIEvent(a, b)); end(v, b, UIEvent(b))
    expect([0x800, 0x800], "two-finger scroll is not a click")
}
do {
    let v = fresh(), otherView = UIView(), a = UITouch(v), b = UITouch(otherView)
    v.touchesBegan([a], with: UIEvent(a, b))
    DispatchQueue.main.advance(0.1)
    end(v, a, UIEvent(a, b))
    expect([2, 4], "a touch on another view cannot hijack the trackpad")
}
do {
    let v = fresh(), a = UITouch(v)
    InputSettings.shared.relative = true
    v.touchesBegan([a], with: UIEvent(a))
    DispatchQueue.main.advance(1)
    end(v, a, UIEvent(a))
    expect([], "holding to aim in relative mode must not click")
}
print("PASS: trackpad click, drag ownership, both lift orders, orphan recovery, cancellation, routing, multitouch and relative mode")
'''
with tempfile.TemporaryDirectory(prefix='madeira-trackpad-') as temp:
    swift = Path(temp) / 'Trackpad.swift'
    exe = Path(temp) / 'trackpad'
    swift.write_text(stubs + trackpad + tests)
    subprocess.run(['xcrun', 'swiftc', '-swift-version', '5', str(swift), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
