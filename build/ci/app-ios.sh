#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p outputs
case "${1:?stage required}" in
dxmt)
    git submodule update --init --recursive --depth 1 dxmt
    xcodebuild -downloadComponent MetalToolchain
    if ! bash build/dxmt-ios/build.sh; then
        find build/dxmt-ios/obj -name '*.err' -exec tail -n 35 {} \;
        exit 1
    fi
    xcrun -sdk iphoneos libtool -static -o app/Madeira/libdxmt_combined.a \
        build/dxmt-ios/obj/*.o toolchains/llvm-ios-build/lib/*.a
    ;;
app)
    # The app's own SwiftUI effects also compile Metal on this runner.
    xcodebuild -downloadComponent MetalToolchain
    git submodule update --init --depth 1 FEX
    git -C FEX submodule update --init --depth 1 External/fmt External/range-v3 External/unordered_dense
    mkdir -p app/Madeira/x86_64-vcruntime app/Madeira/i386-windows
    bash build/stage-licenses.sh
    xcodebuild -project app/Madeira.xcodeproj -scheme Madeira -configuration Debug \
        -destination 'generic/platform=iOS' -derivedDataPath outputs/DerivedData \
        CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY= \
        CURRENT_PROJECT_VERSION="${GITHUB_RUN_NUMBER:-1}" build 2>&1 | tee outputs/xcodebuild.log
    APP=outputs/DerivedData/Build/Products/Debug-iphoneos/Madeira.app
    test -x "$APP/Madeira"
    # Build receipts travel with the app and the final local IPA.
    cp -R app/Madeira/build-info "$APP/"
    test -f "$APP/gl/build-info.json"
    # An ad-hoc signature carries the source entitlements for the sideloader to
    # preserve, especially get-task-allow, which both JIT methods require.
    # No Apple account, certificate or provisioning profile is used here.
    find "$APP" -type f -name '*.dylib' -exec codesign --force --sign - --timestamp=none {} \;
    codesign --force --sign - --timestamp=none "$APP/Frameworks/StikJIT.framework"
    codesign --force --sign - --timestamp=none "$APP/PlugIns/MadeiraJITHelper.appex"
    codesign --force --sign - --timestamp=none --entitlements app/Madeira/Madeira.entitlements "$APP"
    codesign --verify --deep --strict "$APP"
    # Local packaging adds separately supplied runtime payloads. This artifact is
    # deliberately named native-app, not a ready-to-install IPA.
    tar -czf outputs/Madeira-native-app.tar.gz -C "$(dirname "$APP")" Madeira.app
    git rev-parse HEAD > outputs/source-commit.txt
    ;;
*) exit 2 ;;
esac
