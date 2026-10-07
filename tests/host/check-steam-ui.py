#!/usr/bin/env python3
"""Typecheck the changed production UI/model methods with Apple's iOS SDK.

Requires macOS/Xcode. Unrelated runtime/Steam services are stubs; this verifies
SwiftUI and UIKit API use, not app linking or physical-device rotation.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
app = root / 'app/Madeira'


def block(text, marker):
    start = text.index(marker)
    cursor = text.index('{', start)
    depth = 1
    for end in range(cursor + 1, len(text)):
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
            if not depth:
                return text[start:end + 1]
    raise ValueError(marker)


owned = (app / 'SteamOwnedLibrary.swift').read_text()
games = (app / 'SteamGames.swift').read_text()
cloud = (app / 'SteamCloud.swift').read_text()
model_parts = [block(owned, marker) for marker in [
    'struct Download:', 'struct DLC:', 'private static func accountKey(',
    'private func preferenceKey(', 'private func selectedDLC(', 'func loadDLC(',
    'func installDLC(', 'func keepsLocalCloudSaves(', 'func setKeepsLocalCloudSaves(']]
stubs = r'''
import SwiftUI
import UIKit
import CryptoKit
enum VDFParser { static func parseTextVDF(from data: Data) -> [String: Any] { [:] } }
enum SteamError: Error { case appInfoNotFound(UInt32) }
enum SteamSignIn {
    static var accountName: String? { "fixture" }
    static func message(_ error: Error) -> String { error.localizedDescription }
}
@MainActor final class Fetcher {
    func fetchDLC(appID: UInt32) async throws -> (SteamAppInfo, [SteamAppInfo]) { (.init(appID: appID), []) }
    func fetchInstallInfo(appID: UInt32, selectedDLC: Set<UInt32>) async throws -> SteamAppInfo? { .init(appID: appID) }
}
@MainActor final class SteamOwnedLibrary: ObservableObject {
    static let shared = SteamOwnedLibrary()
    static let cloudEnabled = true
    static let steamApps = URL(fileURLWithPath: "/fixture")
    @Published var signedIn = true
    @Published var cloud: [Int: SteamCloudState] = [:]
    @Published var downloads: [Int: Download] = [:]
    var cloudBusy: Set<Int> = []
    var inSession = false
    struct Gate { var open = true }
    let gate = Gate()
    let fetcher = Fetcher()
    func install(_ appID: Int) {}
    func pause(_ appID: Int) {}
    func syncCloud(_ appID: Int) async {}
    func resolveCloud(_ appID: Int, useCloud: Bool) async {}
    MODEL_PARTS
}
struct SteamCloudAudit: Equatable {
    var entries: [SteamCloudEntry] = []
    func count(_ kind: SteamCloudEntry.Kind) -> Int { entries.filter { $0.kind == kind }.count }
}
func formatBytes(_ n: Int64) -> String { String(n) }
enum ProMotionIntent { static let holdMaximum = false }
enum MadeiraConfig { static func set(_ key: String, _ value: String?) {} }
final class LogStore { static let shared = LogStore(); func log(_ text: String) {} }
final class LibraryModel: ObservableObject {
    static let shared = LibraryModel()
    @Published var current: UUID?
}
struct ContentView: View { var body: some View { Text("Fixture") } }
enum TouchControlsHost { static func refreshFrame() {} }
enum MetalBackedView { static func refreshDisplayMode(reason: String) {} }
struct ClaimGamepadEvents: ViewModifier { func body(content: Content) -> some View { content } }
final class GamepadInput { static let shared = GamepadInput(); func start() {} }
final class HardwareInput { static let shared = HardwareInput(); func start() {} }
final class JITNetworkShortcut {
    static let shared = JITNetworkShortcut()
    func restoreLeftover() {}
    func handle(_ url: URL) {}
}
'''.replace('MODEL_PARTS', '\n'.join(model_parts))
parts = [
    block((app / 'SwiftSteam/Content/DepotDownloader.swift').read_text(), 'struct SteamDownloadProgress:'),
    block(cloud, 'struct SteamCloudEntry:'),
    block(cloud, 'struct SteamCloudState:'),
    block(games, 'struct SteamDownloadStatus:'),
    block(games, 'struct SteamCloudSection:'),
    block(games, 'struct SteamDLCView:'),
    block((app / 'Library.swift').read_text(), 'struct DisplayRateSettings:'),
]
sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
with tempfile.TemporaryDirectory() as temp:
    source = Path(temp) / 'UIFixture.swift'
    source.write_text(stubs + '\n' + '\n'.join(parts))
    subprocess.run(['xcrun', 'swiftc', '-typecheck', '-swift-version', '5',
                    '-target', 'arm64-apple-ios17.0', '-sdk', sdk, str(source),
                    str(app / 'MadeiraApp.swift'), str(app / 'SteamKeyValues.swift'),
                    str(app / 'SwiftSteam/Library/SteamAppInfo.swift'),
                    str(app / 'SwiftSteam/Install/AppManifestWriter.swift')], check=True)
print('PASS: production DLC/cloud views, settings, model methods and app orientation typecheck with the iOS SDK')
