#!/usr/bin/env bash
# Builds Oxide-3216 for macOS, runs the engine tests, and packs the app with the README into
# dist/Oxide-3216-<version>-mac.zip.
# Usage:  ./build-mac.sh [Release|Debug]
# Universal (Apple silicon + Intel) by default; OXIDE_ARCHS=arm64 builds for this Mac only, about twice as fast.
# Needs the Xcode command line tools (xcode-select --install) and CMake 3.22+ (brew install cmake).
set -euo pipefail
cd "$(dirname "$0")"
config="${1:-Release}"
archs="${OXIDE_ARCHS:-arm64;x86_64}"

cmake -S . -B build-mac -DCMAKE_BUILD_TYPE="$config" -DCMAKE_OSX_ARCHITECTURES="$archs"
cmake --build build-mac --parallel "$(sysctl -n hw.ncpu)"

build-mac/EngineTests

app="build-mac/Oxide3216_artefacts/$config/Oxide-3216.app"
# No Apple developer ID yet: an ad-hoc signature is what lets it run at all on Apple silicon.
# Downloaded copies still need right-click > Open the first time (see README).
codesign --force --deep --sign - "$app"

version="$(sed -nE 's/^project\(Oxide3216 VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)"
version+="$(sed -nE 's/.*OXIDE_VERSION_LABEL "\$\{PROJECT_VERSION\}([^"]*)".*/\1/p' CMakeLists.txt)"
stage="dist/Oxide-3216-$version"
zip="dist/Oxide-3216-$version-mac.zip"

rm -rf "$stage" "$zip"
mkdir -p "$stage"
cp -R "$app" "$stage/"
cp README.txt "$stage/"
ditto -c -k --keepParent "$stage" "$zip"   # ditto keeps the bundle's signature and permissions intact

echo
echo "Built: $app"
echo "Release: $zip"
