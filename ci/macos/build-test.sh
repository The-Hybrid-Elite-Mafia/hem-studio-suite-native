#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
BUILD="$ROOT/build-macos"
LOG="$ROOT/macos-logs"
mkdir -p "$LOG"
ARCH="${HEM_MAC_ARCH:?Expected explicit arm64 or x86_64}"
case "$ARCH" in arm64|x86_64) ;; *) exit 2;; esac
[[ "$(uname -s)" == Darwin ]] || { echo 'Native macOS required'; exit 2; }
[[ "$(uname -m)" == "$ARCH" ]] || { echo 'Runner architecture mismatch'; exit 2; }
runlog() { local log="$1"; shift; "$@" 2>&1 | tee "$LOG/$log"; }
case "${1:?phase required}" in
validate)
  runlog os.txt sw_vers
  runlog architecture.txt uname -a
  runlog xcode.txt xcodebuild -version
  runlog compiler.txt clang++ --version
  runlog cmake.txt cmake --version
  runlog source.txt git rev-parse HEAD
  git status --porcelain > "$LOG/source-status.txt"
  python3 - <<'PY'
import json,hashlib,pathlib
root=pathlib.Path('native')
manifest=json.loads((root/'INTEGRATION-MANIFEST.json').read_text())
for path in ['Source/CoreBaseline/Engine.h','Source/CoreBaseline/Engine.cpp']:
    expected=next(x['sha256'] for x in manifest['files'] if x['path']==path)
    actual=hashlib.sha256((root/path).read_bytes()).hexdigest()
    print(path, actual, 'expected',expected)
    assert actual==expected, 'Protected baseline changed'
PY
  ;;
configure)
  [[ "$(git -C dependencies/JUCE rev-parse HEAD)" == 3af3ce009f6a02f6fa651008fffb5b41743a9fab ]]
  runlog configure.log cmake -S native -B "$BUILD" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DJUCE_PATH="$ROOT/dependencies/JUCE" \
    -DBUILD_TESTING=ON -DHEM_DSP_TESTS_ONLY=OFF -DHEM_ENABLE_ASIO=OFF \
    -DHEM_BUILD_VST3_TEST_FIXTURE=ON \
    "-DHEM_TEST_VST3_BUNDLE=$BUILD/Tests/VST3Fixture/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3"
  ctest --test-dir "$BUILD" --show-only=json-v1 > "$LOG/test-inventory.json"
  python3 - <<'PY'
import json,pathlib
actual=sorted(x['name'] for x in json.loads(pathlib.Path('macos-logs/test-inventory.json').read_text())['tests'])
expected=sorted(pathlib.Path('ci/macos/expected-tests.txt').read_text().split())
assert actual==expected, f'Full 29-test suite missing: actual={actual}, expected={expected}'
print('All 29 expected tests registered')
PY
  ;;
build)
  runlog build.log cmake --build "$BUILD" --parallel 2
  test -x "$BUILD/HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.app/Contents/MacOS/Hybrid Elite Mafia - Studio Suite"
  test -d "$BUILD/Tests/VST3Fixture/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3"
  ;;
test)
  runlog ctest-full.log ctest --test-dir "$BUILD" --parallel 1 --verbose \
    --output-on-failure --no-tests=error --timeout 240 --output-junit "$LOG/ctest-junit.xml"
  if grep -E 'SKIP:|\*\*\*Skipped' "$LOG/ctest-full.log"; then
    echo 'Skipped coverage prevents full pass'; exit 1
  fi
  ;;
package)
  APP="$BUILD/HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.app"
  BIN="$APP/Contents/MacOS/Hybrid Elite Mafia - Studio Suite"
  runlog binary.txt file "$BIN"
  runlog architectures.txt lipo -archs "$BIN"
  [[ "$(lipo -archs "$BIN")" == "$ARCH" ]]
  runlog dependencies.txt otool -L "$BIN"
  codesign -dv --verbose=4 "$APP" > "$LOG/signature.txt" 2>&1 || true
  mkdir -p dist-macos
  ARCHIVE="HEM-Studio-Suite-macOS-$ARCH-Release-$GITHUB_SHA.zip"
  ditto -c -k --sequesterRsrc --keepParent "$APP" "dist-macos/$ARCHIVE"
  (cd dist-macos && shasum -a 256 "$ARCHIVE" > SHA256SUMS.txt)
  cp "$LOG/ctest-junit.xml" dist-macos/
  cat > dist-macos/BUILD-REPORT.txt <<EOF
HYBRID ELITE MAFIA - STUDIO SUITE
Source: $GITHUB_SHA
Windows baseline: 55a881700f4d90775f36b3e785d763a8652a19dc
Run: $GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID
Native architecture: $ARCH
Build: Release, C++20, Apple Clang, JUCE 8.0.10
Targets: full default build including HEMStudioSuite, VST3 fixture and all tests
Registered tests: 29, required unchanged; see included JUnit evidence
Developer ID signing / notarization: NOT PERFORMED
Application may have linker ad-hoc signature; this is not Apple notarization.
Minimum deployment target: macOS 12.0; only runner OS actually exercised
Physical Mac audio, recording devices, latency, listening, final GUI: NOT HARDWARE-VALIDATED
Native master independent -0.95 dBTP acceptance: NOT VERIFIED; original gate absent
Passing separate limiter tests or baseline parity does not establish that gate.
Private engineering package; distribution license clearance remains outstanding.
EOF
  ;;
*) echo 'Unknown phase'; exit 2;;
esac
