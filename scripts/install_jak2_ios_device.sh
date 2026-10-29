#!/usr/bin/env bash
# Sign the Jak 2 iOS app with a personal (free) Apple ID team, install it on a connected iPhone or
# iPad, copy the game data pack into its Documents and launch it.  See docs/ios/DECISIONI.md.
#
#   scripts/install_jak2_ios_device.sh [DEVICE_NAME_OR_UDID]     (default: the first connected one)
#
# One-time setup on the device: connect it with a cable, unlock it and trust this Mac; enable
# Settings > Privacy & Security > Developer Mode.  After the first install, trust the developer in
# Settings > General > VPN & Device Management.  On the Mac, answer "Always Allow" when the keychain
# asks to let codesign use the development key.  Free-team profiles last 7 days: rerun this script.
#
# Environment: TEAM_ID (default M7AU52VUR3, the personal team), BUNDLE_ID (com.ermadev.jak2),
# SKIP_DATA=1 to reinstall the app without copying the data pack again.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TEAM_ID="${TEAM_ID:-M7AU52VUR3}"
BUNDLE_ID="${BUNDLE_ID:-com.ermadev.jak2}"
BUILD_DIR="build-ios-device"
ANGLE_DIR="${ANGLE_DIR:-$HOME/jak-ios/deps/angle/chromium-7151_rev1/iphoneos-arm64}"
GEN_DIR="$ROOT/out/aot/jak2/macos-arm64"
WORK="$ROOT/out/ios-signing"
PROFILES="$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"

# --- device --------------------------------------------------------------------------------------
DEVICE_ARG="${1:-}"
DEVICES_JSON="$(mktemp)"
trap 'rm -f "$DEVICES_JSON"' EXIT
xcrun devicectl list devices --json-output "$DEVICES_JSON" >/dev/null
DEVICE=$(python3 -I - "$DEVICES_JSON" "$DEVICE_ARG" <<'EOF'
import json, sys
devices = json.load(open(sys.argv[1]))["result"]["devices"]
want = sys.argv[2]
for d in devices:
    props, hw = d.get("deviceProperties", {}), d.get("hardwareProperties", {})
    if hw.get("reality") != "physical" or hw.get("platform") != "iOS":
        continue
    if want and want not in (props.get("name"), hw.get("udid"), d.get("identifier")):
        continue
    if d.get("connectionProperties", {}).get("tunnelState") == "unavailable" and not want:
        continue
    print(hw["udid"])
    break
EOF
)
[ -n "$DEVICE" ] || { echo "no connected iOS device${DEVICE_ARG:+ named $DEVICE_ARG}" >&2; exit 1; }
echo "device: $DEVICE"

# --- build ---------------------------------------------------------------------------------------
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  cmake -G Ninja -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
    "-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_SYSTEM_NAME;CMAKE_OSX_SYSROOT;CMAKE_OSX_ARCHITECTURES" \
    -DOPENGOAL_EXECUTION_MODE=AOT -DOPENGOAL_GRAPHICS_BACKEND=GLES \
    -DOPENGOAL_ANGLE_DIR="$ANGLE_DIR" -DOPENGOAL_AOT_GENERATED_DIR="$GEN_DIR" \
    -DOG_BUILD_HOST_TOOLS=OFF -DSTATICALLY_LINK=ON -DOG_IOS_BUNDLE_ID="$BUNDLE_ID"
fi
ninja -C "$BUILD_DIR" gk

# --- provisioning profile ------------------------------------------------------------------------
find_profile() {
  python3 -I - "$PROFILES" "$TEAM_ID.$BUNDLE_ID" "$DEVICE" <<'EOF'
import datetime, glob, os, plistlib, subprocess, sys
folder, app_id, device = sys.argv[1:4]
best = None
for path in glob.glob(os.path.join(folder, "*.mobileprovision")):
    raw = subprocess.run(["security", "cms", "-D", "-i", path], capture_output=True).stdout
    try:
        p = plistlib.loads(raw)
    except Exception:
        continue
    if p.get("Entitlements", {}).get("application-identifier") != app_id:
        continue
    if device not in p.get("ProvisionedDevices", []):
        continue
    expires = p["ExpirationDate"].replace(tzinfo=datetime.timezone.utc)
    if expires < datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(hours=1):
        continue
    if not best or expires > best[0]:
        best = (expires, path)
if best:
    print(best[1])
EOF
}
PROFILE="$(find_profile)"
if [ -z "$PROFILE" ]; then
  # Let Xcode create (or renew) the free team's profile through a stub app with the same id.
  mkdir -p "$WORK/Stub"
  cat > "$WORK/project.yml" <<EOF
name: Jak2Signing
targets:
  Jak2Signing:
    type: application
    platform: iOS
    deploymentTarget: "17.0"
    sources: [Stub]
    settings:
      PRODUCT_BUNDLE_IDENTIFIER: $BUNDLE_ID
      DEVELOPMENT_TEAM: $TEAM_ID
      CODE_SIGN_STYLE: Automatic
      GENERATE_INFOPLIST_FILE: YES
      MARKETING_VERSION: "1.0"
      CURRENT_PROJECT_VERSION: "1"
EOF
  printf '#import <UIKit/UIKit.h>\nint main(int argc, char* argv[]) { return UIApplicationMain(argc, argv, nil, nil); }\n' \
    > "$WORK/Stub/main.m"
  (cd "$WORK" && xcodegen generate >/dev/null &&
   xcodebuild -project Jak2Signing.xcodeproj -scheme Jak2Signing -destination "id=$DEVICE" \
     -derivedDataPath "$WORK/dd" -allowProvisioningUpdates -allowProvisioningDeviceRegistration \
     build >"$WORK/xcodebuild.log" 2>&1) || { echo "xcodebuild failed, see $WORK/xcodebuild.log" >&2; exit 1; }
  PROFILE="$(find_profile)"
fi
[ -n "$PROFILE" ] || { echo "no provisioning profile for $TEAM_ID.$BUNDLE_ID and $DEVICE" >&2; exit 1; }
echo "profile: $PROFILE"

# Signing identity: the profile's certificate, if it is in the keychain.
security cms -D -i "$PROFILE" > "$WORK/profile.plist"
IDENTITY=$(python3 -I - "$WORK/profile.plist" <<'EOF'
import hashlib, plistlib, subprocess, sys
p = plistlib.load(open(sys.argv[1], "rb"))
valid = subprocess.run(["security", "find-identity", "-v", "-p", "codesigning"],
                       capture_output=True, text=True).stdout
for cert in p["DeveloperCertificates"]:
    sha1 = hashlib.sha1(cert).hexdigest().upper()
    if sha1 in valid:
        print(sha1)
        break
EOF
)
[ -n "$IDENTITY" ] || { echo "no keychain identity for the profile's certificates" >&2; exit 1; }

# --- sign ----------------------------------------------------------------------------------------
APP="$WORK/gk.app"
rm -rf "$APP"
cp -R "$BUILD_DIR/game/gk.app" "$APP"
cp "$PROFILE" "$APP/embedded.mobileprovision"
plutil -extract Entitlements xml1 -o "$WORK/entitlements.plist" "$WORK/profile.plist"
for framework in "$APP"/Frameworks/*.framework; do
  codesign --force --sign "$IDENTITY" --timestamp=none "$framework"
done
codesign --force --sign "$IDENTITY" --timestamp=none --entitlements "$WORK/entitlements.plist" "$APP"
codesign --verify --deep --strict "$APP"
echo "signed with $IDENTITY"

# --- install, data, launch -----------------------------------------------------------------------
xcrun devicectl device install app --device "$DEVICE" "$APP"
if [ "${SKIP_DATA:-0}" != 1 ]; then
  [ -d out/ios-data/jak2 ] || scripts/make_jak2_ios_data_pack.sh
  echo "copying the data pack (about 2.7 GB, a few minutes over USB)..."
  xcrun devicectl device copy to --device "$DEVICE" --domain-type appDataContainer \
    --domain-identifier "$BUNDLE_ID" --source out/ios-data/jak2 --destination Documents/jak2
fi
xcrun devicectl device process launch --device "$DEVICE" "$BUNDLE_ID"
echo "launched $BUNDLE_ID on $DEVICE"
