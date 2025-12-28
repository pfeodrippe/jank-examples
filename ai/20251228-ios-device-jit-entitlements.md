# iOS Device JIT Entitlements

## Problem
App crashes on iOS device after JIT compilation, but works on simulator.

## Root Cause
iOS device requires special entitlements for JIT code execution. Unlike simulator (which runs on macOS with no JIT restrictions), iOS device restricts dynamic code execution.

## Entitlements Not Available with Free Developer Account

With a **free Apple Developer account**, you CANNOT add custom JIT entitlements:
- `com.apple.security.cs.allow-jit` - NOT AVAILABLE
- `com.apple.security.cs.allow-unsigned-executable-memory` - NOT AVAILABLE

These entitlements require a paid Apple Developer Program membership.

## Solution: Launch from Xcode with Debugger

The only way to enable JIT on device with a free account:

1. **Open the project in Xcode**:
   ```bash
   open SdfViewerMobile/SdfViewerMobile-JIT-Only-Device.xcodeproj
   ```

2. **Select your device** as the target

3. **Press Run** (Cmd+R) - this launches with lldb debugger attached

The `get-task-allow` entitlement (auto-added by Xcode for debug builds) allows JIT execution when launched under a debugger.

## Why This Works

When Xcode launches an app with lldb attached:
- The debugger can mark memory pages as executable
- This bypasses iOS's normal code signing requirements for executable memory
- JIT compilation works because lldb enables it

## CLI Workflow (Free Account)

For CLI-only deployment with remote compile server:
1. Start compile server: `make ios-compile-server-device`
2. Start nREPL proxy: `make ios-device-nrepl-proxy`
3. Deploy from Xcode (Run button) - NOT `make ios-jit-only-device-run`

The Makefile can build and deploy, but the app must be **launched from Xcode** for JIT to work.

## Paid Developer Account Alternative

With a paid Apple Developer account, you could:
1. Add JIT entitlements to `jank-jit.entitlements`:
   ```xml
   <key>com.apple.security.cs.allow-jit</key>
   <true/>
   <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
   <true/>
   ```
2. Configure a provisioning profile that allows these entitlements
3. Deploy and run from CLI without Xcode

## Files Involved
- `SdfViewerMobile/jank-jit.entitlements` - Keep empty for free account
- `SdfViewerMobile/project-jit-only-device.yml` - References entitlements file

## Commands
- `lsof -i :5571` - Check if device compile server is running
- `lsof -i :5559` - Check if nREPL proxy is running
- `idevicesyslog | grep SdfViewerMobile` - View device logs
