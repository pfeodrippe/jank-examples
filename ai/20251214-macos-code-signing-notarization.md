# macOS Code Signing & Notarization Guide

**Date:** 2024-12-14
**Topic:** Properly signing macOS apps for verified distribution

## Current State

The current build uses **ad-hoc signing** in `bin/run_sdf.sh` (lines 345-354):

```bash
codesign --force --sign - "$lib"
```

This means:
- Works locally on your machine
- Shows "unidentified developer" warning on other Macs
- Users must right-click → Open to bypass Gatekeeper

## What's Needed for Verified Distribution

### 1. Apple Developer Account ($99/year)

Enroll at https://developer.apple.com/programs/

### 2. Create Certificates

In Keychain Access or Apple Developer portal, create:
- **Developer ID Application** certificate (for signing apps outside App Store)
- **Developer ID Installer** certificate (optional, for pkg installers)

Find your certificate identity with:
```bash
security find-identity -v -p codesigning
```

### 3. Update Code Signing in `bin/run_sdf.sh`

Replace the ad-hoc signing (lines 345-354) with proper signing:

```bash
# Developer ID certificate identity
SIGNING_IDENTITY="${SIGNING_IDENTITY:-}"

if [ -n "$SIGNING_IDENTITY" ]; then
    echo "Code signing with Developer ID..."
    # Sign with hardened runtime (required for notarization)
    for lib in "$APP_BUNDLE/Contents/Frameworks/"*.dylib; do
        codesign --force --options runtime --entitlements entitlements.plist --sign "$SIGNING_IDENTITY" "$lib"
    done
    codesign --force --options runtime --entitlements entitlements.plist --sign "$SIGNING_IDENTITY" "$EXEC_PATH"
    codesign --force --options runtime --entitlements entitlements.plist --sign "$SIGNING_IDENTITY" "$APP_BUNDLE"
else
    echo "Code signing (ad-hoc)..."
    # Ad-hoc signing for local development
    for lib in "$APP_BUNDLE/Contents/Frameworks/"*.dylib; do
        codesign --force --sign - "$lib" 2>/dev/null || true
    done
    codesign --force --sign - "$EXEC_PATH"
    codesign --force --sign - "$APP_BUNDLE"
fi
```

### 4. Create Entitlements File

Create `entitlements.plist` in the project root for hardened runtime exceptions:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <!-- Required for jank JIT compilation -->
    <key>com.apple.security.cs.allow-jit</key>
    <true/>
    <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
    <true/>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
```

**Note:** JIT compilation (which jank uses) requires these entitlements.

### 5. Add Notarization Step

After creating the DMG in `bin/run_sdf.sh`, add notarization:

```bash
# Create DMG
hdiutil create -volname "$APP_NAME" -srcfolder "$APP_BUNDLE" -ov -format UDZO "$DMG_NAME"

# Notarize if credentials are available
if [ -n "$APPLE_ID" ] && [ -n "$APPLE_TEAM_ID" ] && [ -n "$APPLE_APP_PASSWORD" ]; then
    echo "Notarizing DMG..."
    xcrun notarytool submit "$DMG_NAME" \
        --apple-id "$APPLE_ID" \
        --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" \
        --wait

    # Staple the notarization ticket to the DMG
    echo "Stapling notarization ticket..."
    xcrun stapler staple "$DMG_NAME"

    echo "DMG is now notarized and ready for distribution!"
else
    echo "Skipping notarization (credentials not set)"
fi
```

### 6. Update CI/CD (`.github/workflows/ci.yml`)

#### Add GitHub Secrets

Add these secrets to your GitHub repo (Settings → Secrets → Actions):

| Secret Name | Description |
|-------------|-------------|
| `APPLE_CERTIFICATE_BASE64` | Your .p12 certificate encoded in base64 |
| `APPLE_CERTIFICATE_PASSWORD` | Password for the .p12 certificate |
| `APPLE_ID` | Your Apple ID email |
| `APPLE_TEAM_ID` | Your 10-character team ID |
| `APPLE_APP_PASSWORD` | App-specific password from appleid.apple.com |

To encode your certificate:
```bash
base64 -i certificate.p12 | pbcopy
```

#### Update CI Workflow

Add these steps before the build in `.github/workflows/ci.yml`:

```yaml
- name: Import Code Signing Certificate
  if: github.event_name != 'pull_request'
  env:
    CERTIFICATE_BASE64: ${{ secrets.APPLE_CERTIFICATE_BASE64 }}
    CERTIFICATE_PASSWORD: ${{ secrets.APPLE_CERTIFICATE_PASSWORD }}
  run: |
    # Skip if no certificate (e.g., PRs from forks)
    if [ -z "$CERTIFICATE_BASE64" ]; then
      echo "No certificate available, skipping"
      exit 0
    fi

    # Decode certificate
    echo "$CERTIFICATE_BASE64" | base64 --decode > certificate.p12

    # Create temporary keychain
    security create-keychain -p "" build.keychain
    security default-keychain -s build.keychain
    security unlock-keychain -p "" build.keychain

    # Import certificate
    security import certificate.p12 -k build.keychain -P "$CERTIFICATE_PASSWORD" -T /usr/bin/codesign
    security set-key-partition-list -S apple-tool:,apple: -s -k "" build.keychain

    # Clean up
    rm certificate.p12

- name: Build standalone app
  env:
    SIGNING_IDENTITY: ${{ secrets.APPLE_CERTIFICATE_BASE64 && 'Developer ID Application: Your Name (TEAMID)' || '' }}
  run: |
    export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
    export PATH="$HOME/jank/compiler+runtime/build:$PATH"
    make sdf-standalone

- name: Notarize DMG
  if: github.event_name != 'pull_request'
  env:
    APPLE_ID: ${{ secrets.APPLE_ID }}
    APPLE_TEAM_ID: ${{ secrets.APPLE_TEAM_ID }}
    APPLE_APP_PASSWORD: ${{ secrets.APPLE_APP_PASSWORD }}
  run: |
    # Skip if no credentials
    if [ -z "$APPLE_ID" ]; then
      echo "No Apple credentials, skipping notarization"
      exit 0
    fi

    xcrun notarytool submit SDFViewer.dmg \
      --apple-id "$APPLE_ID" \
      --team-id "$APPLE_TEAM_ID" \
      --password "$APPLE_APP_PASSWORD" \
      --wait

    xcrun stapler staple SDFViewer.dmg
```

## Verification Commands

After signing, verify with:

```bash
# Check code signature
codesign -dv --verbose=4 SDFViewer.app

# Verify signature is valid
codesign --verify --deep --strict SDFViewer.app

# Check notarization status
spctl --assess --verbose=4 --type execute SDFViewer.app

# Check if DMG is notarized
stapler validate SDFViewer.dmg
```

## Summary of Files to Modify

1. **`bin/run_sdf.sh`** - Update code signing section (lines 345-354)
2. **`entitlements.plist`** - Create new file with JIT entitlements
3. **`.github/workflows/ci.yml`** - Add certificate import and notarization steps

## What Was Learned

- Current setup uses ad-hoc signing which shows "unidentified developer" warnings
- Proper distribution requires: Developer ID certificate + Hardened Runtime + Notarization + Stapling
- JIT compilation (jank) requires specific entitlements for hardened runtime
- CI needs secure certificate storage via GitHub Secrets
- Notarization takes 1-5 minutes and must be done after DMG creation

## Next Steps

1. Enroll in Apple Developer Program if not already
2. Create Developer ID Application certificate
3. Create `entitlements.plist` file
4. Update `bin/run_sdf.sh` with proper signing
5. Add GitHub Secrets for CI
6. Update `.github/workflows/ci.yml` with signing and notarization
7. Test locally before pushing to CI
