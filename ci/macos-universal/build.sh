#!/bin/bash
set -euo pipefail
mkdir -p universal-logs
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]]
export HEM_MAC_ARCH=arm64
bash ci/macos/build-test.sh validate
cp -R macos-logs/. universal-logs/
[[ $(git -C dependencies/JUCE rev-parse HEAD) == 3af3ce009f6a02f6fa651008fffb5b41743a9fab ]]
cmake -S native -B build-universal -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DJUCE_PATH="$PWD/dependencies/JUCE" \
  -DBUILD_TESTING=ON -DHEM_DSP_TESTS_ONLY=OFF -DHEM_ENABLE_ASIO=OFF \
  -DHEM_BUILD_VST3_TEST_FIXTURE=ON \
  "-DHEM_TEST_VST3_BUNDLE=$PWD/build-universal/Tests/VST3Fixture/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3" \
  2>&1 | tee universal-logs/configure.log
ctest --test-dir build-universal --show-only=json-v1 > universal-logs/test-inventory.json
cmake --build build-universal --parallel 2 2>&1 | tee universal-logs/build.log
ctest --test-dir build-universal --show-only=json-v1 > universal-logs/test-inventory.json
python3 ci/macos-universal/runtime.py stage
# Intermediate archive is explicitly not a release until both native test jobs pass.
tar -czf universal-runtime.tar.gz universal-runtime
shasum -a 256 universal-runtime.tar.gz > universal-logs/runtime-archive.sha256
python3 - <<'PY'
import pathlib
s=pathlib.Path('universal-runtime.tar.gz').stat().st_size
print('Runtime compressed bytes:',s)
assert s<250_000_000, 'Artifact storage guard exceeded'
PY
python3 ci/macos-universal/runtime.py verify
