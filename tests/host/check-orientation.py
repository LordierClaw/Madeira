#!/usr/bin/env python3
"""Exercise the production orientation state machine with simulated UIKit scenes.

Requires macOS/Swift (Combine). These tests cover when rotation is requested;
physical portrait-lock behavior still needs an iPhone. The separate SDK check
compiles the same class against real UIKit.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'app/Madeira/MadeiraApp.swift').read_text()
orientation = source[source.index('@MainActor final class MadeiraOrientation:'):source.index('\nfinal class MadeiraAppDelegate:')]
stubs = r"""
import Foundation
import Combine
struct UIInterfaceOrientationMask: OptionSet {
    let rawValue: Int
    static let portrait = Self(rawValue: 1)
    static let landscapeLeft = Self(rawValue: 2)
    static let landscapeRight = Self(rawValue: 4)
    static let portraitUpsideDown = Self(rawValue: 8)
    static let landscape: Self = [.landscapeLeft, .landscapeRight]
    static let allButUpsideDown: Self = [.portrait, .landscape]
}
enum UIInterfaceOrientation { case portrait, landscapeLeft, landscapeRight, portraitUpsideDown, unknown }
@MainActor class UIScene {
    enum Activation { case foregroundActive, background }
    var activationState = Activation.foregroundActive
}
@MainActor final class UIWindowScene: UIScene {
    struct Session { var persistentIdentifier = "fixture" }
    enum Preferences { case iOS(interfaceOrientations: UIInterfaceOrientationMask) }
    var session = Session()
    var windows: [UIWindow] = []
    var interfaceOrientation = UIInterfaceOrientation.portrait
    var requests: [UIInterfaceOrientationMask] = []
    var callbacks: [(Error) -> Void] = []
    func requestGeometryUpdate(_ preferences: Preferences, errorHandler: @escaping (Error) -> Void) {
        if case .iOS(let mask) = preferences { requests.append(mask) }
        callbacks.append(errorHandler)
    }
}
@MainActor final class UIWindow { var rootViewController: UIViewController? }
@MainActor final class UIViewController {
    var presentedViewController: UIViewController?
    func setNeedsUpdateOfSupportedInterfaceOrientations() {}
}
@MainActor final class UIApplication {
    static let shared = UIApplication()
    var connectedScenes: [UIScene] = []
}
"""
tests = r"""
@main struct Fixture {
    @MainActor static func main() async {
        let defaults = UserDefaults.standard, key = "madeira.forceLandscape"
        let saved = defaults.object(forKey: key)
        defaults.removeObject(forKey: key)
        defer { if let saved { defaults.set(saved, forKey: key) } else { defaults.removeObject(forKey: key) } }
        let scene = UIWindowScene()
        UIApplication.shared.connectedScenes = [scene]
        let model = MadeiraOrientation()
        model.forceLandscapeWhenPlaying = true
        precondition(scene.requests.isEmpty && model.supported == .allButUpsideDown, "enabling in the library must not rotate")
        model.apply()
        precondition(scene.requests.isEmpty, "cold launch and foregrounding outside a session keep normal rotation")
        model.setPlaying(true)
        precondition(scene.requests.last == .landscape && model.supported == .landscape, "Play forces the scene's landscape mask")
        let staleFailure = scene.callbacks.last!
        model.setPlaying(false)
        precondition(scene.requests.last == .portrait && model.supported == .allButUpsideDown, "exit or launch failure restores the library")
        staleFailure(NSError(domain: "old request", code: 1))
        await Task.yield()
        precondition(model.problem == nil, "a previous session's error must not reappear after it ends")
        scene.interfaceOrientation = .landscapeRight
        model.setPlaying(true)
        model.setPlaying(false)
        precondition(scene.requests.last == .landscapeRight, "restore the user's actual pre-game orientation")
        scene.interfaceOrientation = .portrait
        model.setPlaying(true)
        model.forceLandscapeWhenPlaying = false
        precondition(scene.requests.last == .portrait && model.supported == .allButUpsideDown, "disabling mid-session releases the orientation lock")
        model.forceLandscapeWhenPlaying = true
        precondition(scene.requests.last == .landscape, "enabling mid-session applies immediately")
        scene.activationState = .background
        let count = scene.requests.count
        model.setPlaying(false)
        precondition(scene.requests.count == count, "a background scene receives no geometry request")
        scene.activationState = .foregroundActive
        model.apply()
        precondition(scene.requests.last == .portrait, "an exit while backgrounded restores on return")
        let afterExit = scene.requests.count
        model.forceLandscapeWhenPlaying = false
        model.forceLandscapeWhenPlaying = true
        model.apply()
        precondition(scene.requests.count == afterExit, "idle settings changes cannot rotate the library or downloads")
        print("PASS: idle, launch, exit/failure, mid-game toggle, previous orientation, background exit, stale callback")
    }
}
"""
with tempfile.TemporaryDirectory(prefix='madeira-orientation-') as temp:
    swift = Path(temp) / 'Orientation.swift'
    exe = Path(temp) / 'orientation'
    swift.write_text(stubs + '\n' + orientation + '\n' + tests)
    subprocess.run(['xcrun', 'swiftc', '-swift-version', '5', '-parse-as-library', str(swift), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
