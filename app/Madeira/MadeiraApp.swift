import SwiftUI
import UIKit

@main
struct MadeiraApp: App {
    @UIApplicationDelegateAdaptor(MadeiraAppDelegate.self) private var appDelegate
    @Environment(\.scenePhase) private var scenePhase
    var body: some Scene {
        WindowGroup {
            ContentView()
                .modifier(ClaimGamepadEvents())
                .onAppear {
                    GamepadInput.shared.start()
                    HardwareInput.shared.start()
                    JITNetworkShortcut.shared.restoreLeftover()   // also starts its network path monitor
                    MadeiraOrientation.shared.apply()
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
    @Published var forceLandscape = UserDefaults.standard.bool(forKey: "madeira.forceLandscape") {
        didSet {
            guard forceLandscape != oldValue else { return }
            UserDefaults.standard.set(forceLandscape, forKey: "madeira.forceLandscape")
            apply(restorePortrait: !forceLandscape)
        }
    }
    @Published private(set) var problem: String?

    var supported: UIInterfaceOrientationMask { forceLandscape ? .landscape : .allButUpsideDown }

    func apply(restorePortrait: Bool = false) {
        problem = nil
        for case let scene as UIWindowScene in UIApplication.shared.connectedScenes
            where scene.activationState == .foregroundActive {
            for window in scene.windows {
                var controller = window.rootViewController
                while let current = controller {
                    current.setNeedsUpdateOfSupportedInterfaceOrientations()
                    controller = current.presentedViewController
                }
            }
            guard forceLandscape || restorePortrait else { continue }
            scene.requestGeometryUpdate(.iOS(interfaceOrientations: forceLandscape ? .landscape : .portrait)) { [weak self] error in
                Task { @MainActor in
                    self?.problem = "Could not rotate the screen: \(error.localizedDescription)"
                }
            }
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
