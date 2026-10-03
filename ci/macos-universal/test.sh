#!/bin/bash
set -euo pipefail
mkdir -p universal-logs
[[ $(uname -s) == Darwin && $(uname -m) == "$HEM_MAC_ARCH" ]]
[[ $(sysctl -in sysctl.proc_translated 2>/dev/null || echo 0) != 1 ]]
sw_vers | tee universal-logs/test-os.txt
uname -a | tee universal-logs/test-architecture.txt
cmake --version | tee universal-logs/test-cmake.txt
shasum -a 256 universal-runtime.tar.gz | tee universal-logs/runtime-archive.sha256
tar -xzf universal-runtime.tar.gz
python3 ci/macos-universal/runtime.py verify
python3 ci/macos-universal/runtime.py ctest
ctest --test-dir universal-runtime --show-only=json-v1 > universal-logs/executed-inventory.json
ctest --test-dir universal-runtime --parallel 1 --verbose --output-on-failure --no-tests=error --timeout 240 \
  --output-junit "$PWD/universal-logs/ctest-$HEM_MAC_ARCH.xml" 2>&1 | tee universal-logs/ctest-full.log
if grep -E 'SKIP:|\*\*\*Skipped' universal-logs/ctest-full.log; then echo 'Skipped coverage prevents pass'; exit 1; fi
swift ci/macos-universal/smoke.swift "$PWD/universal-runtime/HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.app" \
  2>&1 | tee universal-logs/app-startup-smoke.log
python3 - <<'PY'
import os, pathlib, json, xml.etree.ElementTree as E
arch=os.environ['HEM_MAC_ARCH']; root=E.parse(f'universal-logs/ctest-{arch}.xml').getroot()
assert int(root.attrib['tests'])==29 and int(root.attrib.get('failures',0))==0
assert int(root.attrib.get('skipped',0))==0 and int(root.attrib.get('disabled',0))==0
result={'commit':os.environ['GITHUB_SHA'],'native_architecture':arch,'tests':29,'failures':0,'skips':0,'app_startup_window':'PASS','runtime_archive_sha256':pathlib.Path('universal-logs/runtime-archive.sha256').read_text().split()[0]}
pathlib.Path('universal-logs/result.json').write_text(json.dumps(result,indent=2))
PY
