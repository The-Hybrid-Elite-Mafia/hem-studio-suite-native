import Foundation
import AppKit
import CoreGraphics

// The actual packaged executable is launched. No test-only application path is substituted.
let app = URL(fileURLWithPath: CommandLine.arguments[1])
let executable = app.appendingPathComponent("Contents/MacOS/Hybrid Elite Mafia - Studio Suite")
let process = Process()
process.executableURL = executable
let output = FileHandle.standardOutput
process.standardOutput = output
process.standardError = output
try process.run()
let pid = process.processIdentifier
var found = false
for _ in 0..<30 {
    Thread.sleep(forTimeInterval: 0.5)
    if !process.isRunning { break }
    if let windows = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]] {
        for window in windows {
            if (window[kCGWindowOwnerPID as String] as? Int32) == pid,
               (window[kCGWindowLayer as String] as? Int) == 0,
               let bounds = window[kCGWindowBounds as String] as? [String: Any],
               let width = bounds["Width"] as? Double, let height = bounds["Height"] as? Double,
               width > 100, height > 100 {
                found = true
                print("Observed application window:", window)
            }
        }
    }
    if found { break }
}
let passed = found && process.isRunning
if process.isRunning { process.terminate(); process.waitUntilExit() }
print("Packaged app startup/window smoke:", passed ? "PASS" : "FAIL", "pid", pid)
exit(passed ? 0 : 1)
