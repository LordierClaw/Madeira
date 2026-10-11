// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Madeira fork contributors
// Madeira Converter Exception: see LICENSE-EXCEPTION.md
import Foundation
import CommonCrypto
#if canImport(Glibc)
import Glibc
#else
import Darwin
#endif

/// A verification hint, never a list of all files that may exist in a game.
/// Fixed-width records bound storage independently of manifest/path sizes.
/// Only files verified without modification during the operation are sealed;
/// newly downloaded files can join on a later verification. No extra content
/// read or full-tree walk is performed just to create this index.
struct SteamFileIndex: Sendable {
    static let limit = 16_384
    static let keyBytes = 20
    static let stampBytes = 72
    static let recordBytes = keyBytes + stampBytes
    static let maximumBytes = 8 + limit * recordBytes + 20
    private static let magic = Data("MDFIDX01".utf8)
    private(set) var records = Data()
    var count: Int { records.count / Self.recordBytes }

    static func url(appID: Int, steamApps: URL) -> URL {
        steamApps.appendingPathComponent("depotcache/madeira/\(appID).files-v1")
    }

    private static func digest(_ data: Data) -> Data {
        var bytes = [UInt8](repeating: 0, count: 20)
        data.withUnsafeBytes { raw in _ = CC_SHA1(raw.baseAddress, CC_LONG(raw.count), &bytes) }
        return Data(bytes)
    }

    static func key(depot: UInt32, manifest: UInt64, path: String) -> Data {
        digest(Data("\(depot):\(manifest):\(path)".utf8))
    }

    /// Identity, length, mode/owner, nanosecond mtime AND ctime. Restoring mtime
    /// after editing a mod cannot make that file appear unchanged.
    static func stamp(_ path: String) -> Data? {
        var info = stat()
        guard stat(path, &info) == 0, (info.st_mode & mode_t(S_IFMT)) == mode_t(S_IFREG), info.st_size >= 0 else { return nil }
        #if canImport(Glibc)
        let modified = info.st_mtim, changed = info.st_ctim
        #else
        let modified = info.st_mtimespec, changed = info.st_ctimespec
        #endif
        let values: [UInt64] = [UInt64(bitPattern: Int64(info.st_dev)), UInt64(info.st_ino), UInt64(info.st_size),
            UInt64(info.st_mode), UInt64(info.st_uid) << 32 | UInt64(info.st_gid),
            UInt64(bitPattern: Int64(modified.tv_sec)), UInt64(modified.tv_nsec),
            UInt64(bitPattern: Int64(changed.tv_sec)), UInt64(changed.tv_nsec)]
        var result = Data()
        for value in values { var little = value.littleEndian; withUnsafeBytes(of: &little) { result.append(contentsOf: $0) } }
        return result
    }

    mutating func append(key: Data, stamp: Data) {
        guard count < Self.limit, key.count == Self.keyBytes, stamp.count == Self.stampBytes else { return }
        records.append(key); records.append(stamp)
    }

    mutating func sort() {
        let order = (0..<count).sorted {
            records[($0 * Self.recordBytes)..<($0 * Self.recordBytes + Self.keyBytes)].lexicographicallyPrecedes(
                records[($1 * Self.recordBytes)..<($1 * Self.recordBytes + Self.keyBytes)])
        }
        var sorted = Data(capacity: records.count)
        for index in order { sorted.append(records[(index * Self.recordBytes)..<((index + 1) * Self.recordBytes)]) }
        records = sorted
    }

    func stamp(for key: Data) -> Data? {
        var low = 0, high = count
        while low < high {
            let middle = (low + high) / 2, offset = middle * Self.recordBytes
            let stored = records[offset..<(offset + Self.keyBytes)]
            if stored == key { return records.subdata(in: (offset + Self.keyBytes)..<(offset + Self.recordBytes)) }
            if stored.lexicographicallyPrecedes(key) { low = middle + 1 } else { high = middle }
        }
        return nil
    }

    static func load(_ url: URL?) -> Self {
        guard let url, let handle = try? FileHandle(forReadingFrom: url) else { return Self() }
        defer { try? handle.close() }
        guard let data = try? handle.read(upToCount: maximumBytes + 1), data.count >= 28,
              data.count <= maximumBytes, data.prefix(8) == magic,
              (data.count - 28) % recordBytes == 0,
              data.suffix(20) == digest(data.dropLast(20)) else { return Self() }
        var result = Self()
        result.records = data.subdata(in: 8..<(data.count - 20))
        // Reject malformed ordering/duplicate identities before binary search.
        if result.count > 1 {
            for i in 1..<result.count {
                let a = (i - 1) * recordBytes, b = i * recordBytes
                guard result.records[a..<(a + keyBytes)].lexicographicallyPrecedes(result.records[b..<(b + keyBytes)]) else { return Self() }
            }
        }
        return result
    }

    func publish(_ url: URL) throws {
        var data = Self.magic
        data.append(records); data.append(Self.digest(data))
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url, options: .atomic)
    }
}
