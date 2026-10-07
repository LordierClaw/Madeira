import SwiftUI
import UIKit

@main
struct MadeiraApp: App {
    @UIApplicationDelegateAdaptor(MadeiraAppDelegate.self) private var appDelegate
    @ObservedObject private var library = LibraryModel.shared
    @Environment(\.scenePhase) private var scenePhase
    var body: some Scene {
        WindowGroup {
            ContentView()
                .modifier(ClaimGamepadEvents())
                .background {
                    GeometryReader { geometry in
                        Color.clear.onChange(of: geometry.size) { _, _ in
                            // Programmatic rotation under portrait lock need not
                            // post UIDevice.orientationDidChangeNotification.
                            TouchControlsHost.refreshFrame()
                            MetalBackedView.refreshDisplayMode(reason: "interface-size")
                        }
                    }.allowsHitTesting(false)
                }
                .onAppear {
                    GamepadInput.shared.start()
                    HardwareInput.shared.start()
                    JITNetworkShortcut.shared.restoreLeftover()   // also starts its network path monitor
                    MadeiraOrientation.shared.setPlaying(library.current != nil)
                    MadeiraOrientation.shared.apply()
                }
                .onChange(of: library.current) { _, current in
                    MadeiraOrientation.shared.setPlaying(current != nil)
                }
                .onChange(of: scenePhase) { _, phase in
                    if phase == .active { MadeiraOrientation.shared.apply() }
                }
                // madeira://jit-network/...: the Madeira JIT shortcut returning (JITNetwork.swift).
                .onOpenURL { url in JITNetworkShortcut.shared.handle(url) }
        }
    }
}

/// A programmatic scene rotation, independent of device-orientation sensor
/// notifications (which portrait lock suppresses). No private UIDevice KVC.
@MainActor final class MadeiraOrientation: ObservableObject {
    static let shared = MadeiraOrientation()
    // Retain the existing saved choice, with the narrower playing-only scope.
    @Published var forceLandscapeWhenPlaying = UserDefaults.standard.bool(forKey: "madeira.forceLandscape") {
        didSet {
            guard forceLandscapeWhenPlaying != oldValue else { return }
            UserDefaults.standard.set(forceLandscapeWhenPlaying, forKey: "madeira.forceLandscape")
            apply()
        }
    }
    @Published private(set) var problem: String?
    private(set) var isPlaying = false
    private var appliedLandscape = false
    private var restorePending = false
    private var returnOrientations: [String: UIInterfaceOrientationMask] = [:]
    private var generation = 0

    private var shouldForceLandscape: Bool { forceLandscapeWhenPlaying && isPlaying }
    var supported: UIInterfaceOrientationMask { shouldForceLandscape ? .landscape : .allButUpsideDown }

    func setPlaying(_ playing: Bool) {
        guard playing != isPlaying else { return }
        isPlaying = playing
        apply()
    }

    func apply() {
        let force = shouldForceLandscape
        if force && !appliedLandscape { returnOrientations.removeAll() }
        if appliedLandscape && !force { restorePending = true }
        if force { restorePending = false }
        appliedLandscape = force
        generation += 1
        let request = generation
        problem = nil
        for case let scene as UIWindowScene in UIApplication.shared.connectedScenes
            where scene.activationState == .foregroundActive {
            // Save this before notifying UIKit: changing the supported mask
            // can itself begin an automatic rotation.
            let id = scene.session.persistentIdentifier
            if force && returnOrientations[id] == nil {
                switch scene.interfaceOrientation {
                case .landscapeLeft: returnOrientations[id] = .landscapeLeft
                case .landscapeRight: returnOrientations[id] = .landscapeRight
                default: returnOrientations[id] = .portrait
                }
            }
            for window in scene.windows {
                var controller = window.rootViewController
                while let current = controller {
                    current.setNeedsUpdateOfSupportedInterfaceOrientations()
                    controller = current.presentedViewController
                }
            }
            guard force || restorePending else { continue }
            let orientation: UIInterfaceOrientationMask = force ? .landscape : (returnOrientations[id] ?? .portrait)
            scene.requestGeometryUpdate(.iOS(interfaceOrientations: orientation)) { [weak self] error in
                Task { @MainActor in
                    guard let self, self.generation == request else { return }
                    if !force { self.restorePending = true }
                    self.problem = "Could not rotate the screen: \(error.localizedDescription)"
                }
            }
        }
        // Preserve a pending restore if the session ended while in the background.
        if !force, UIApplication.shared.connectedScenes.contains(where: { $0.activationState == .foregroundActive }) {
            restorePending = false
        }
    }
}

final class MadeiraAppDelegate: NSObject, UIApplicationDelegate {
    func application(_ application: UIApplication, supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        MadeiraOrientation.shared.supported
    }
}

/// Identifies which build is installed: logged at launch and shown under
/// Setup Guide > About.
enum BuildInfo {

    /// When the app code was linked: the newer modification time of the executable
    /// and, in Debug builds, Madeira.debug.dylib (Xcode may leave the stub alone).
    static let builtAt: String = {
        var paths = [Bundle.main.executablePath].compactMap { $0 }
        paths.append(Bundle.main.bundlePath + "/Madeira.debug.dylib")
        let dates = paths.compactMap {
            (try? FileManager.default.attributesOfItem(atPath: $0))?[.modificationDate] as? Date
        }
        guard let date = dates.max() else { return "unknown" }
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return f.string(from: date)
    }()

    static var summary: String { "built \(builtAt)" }
}
