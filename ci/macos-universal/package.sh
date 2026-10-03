#!/bin/bash
set -euo pipefail
mkdir -p universal-logs dist-universal
python3 - <<'PY'
import json,pathlib,hashlib,os
h=hashlib.sha256(pathlib.Path('universal-runtime.tar.gz').read_bytes()).hexdigest()
for arch in ['arm64','x86_64']:
    p=pathlib.Path('evidence')/arch/'result.json'; r=json.loads(p.read_text())
    assert r['commit']==os.environ['GITHUB_SHA'] and r['native_architecture']==arch
    assert r['tests']==29 and r['failures']==0 and r['skips']==0 and r['app_startup_window']=='PASS'
    assert r['runtime_archive_sha256']==h
print('Both native architectures executed identical runtime archive and passed all 29 tests plus app startup')
PY
tar -xzf universal-runtime.tar.gz
python3 ci/macos-universal/runtime.py verify
APP="$PWD/universal-runtime/HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.app"
ARCHIVE="HEM-Studio-Suite-macOS-Universal-Release-$GITHUB_SHA.zip"
ditto -c -k --sequesterRsrc --keepParent "$APP" "dist-universal/$ARCHIVE"
unzip -t "dist-universal/$ARCHIVE" | tee universal-logs/zip-integrity.txt
cp -R evidence dist-universal/
cp universal-logs/universal-binary-audit.json universal-logs/bundle-info.json dist-universal/
cat > dist-universal/BUILD-REPORT.txt <<EOF
HYBRID ELITE MAFIA - STUDIO SUITE: Universal macOS engineering build
Compiled source: ${HEM_COMPILED_SHA:-$GITHUB_SHA}
Packaging and test workflow revision: $GITHUB_SHA
ARM64 parent: a71dd3554afa8ee79f636409e6d42b433f1217b3
Workflow: .github/workflows/macos-universal-recovery.yml
Run: $GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID
Build: Release C++20, Apple Clang, JUCE 8.0.10
Architectures: arm64 and x86_64 in every shipped Mach-O
Native Apple Silicon: 29/29 CTests PASS and packaged application startup/window PASS
Native Intel: 29/29 CTests PASS and the identical packaged application startup/window PASS
All existing test assertions retained. Source protection checks retained.
Configured minimum deployment: macOS 12.0 Monterey, checked in Mach-O slices and Info.plist
Actually exercised OS versions: see evidence/*/test-os.txt (macOS 15 runners)
macOS 12-14 runtime support: NOT VERIFIED; deployment target is not an old-OS test
Other Mac OS releases and arbitrary third-party plugins: NOT VERIFIED
Physical Mac audio, devices, latency, recording, listening and final GUI: NOT HARDWARE-VALIDATED
Independent shipped-master -0.95 dBTP acceptance: NOT VERIFIED, original gate absent
Developer ID signature/notarization: NOT PERFORMED; linker ad-hoc signature only
Gatekeeper may block this engineering package. Do not bypass security warnings.
Public-distribution licensing: NOT CLEARED
Windows and previous ARM64 release unchanged; no second AI or paid model API added
EOF
(cd dist-universal && shasum -a 256 "$ARCHIVE" BUILD-REPORT.txt > SHA256SUMS.txt)
