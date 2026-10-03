#!/usr/bin/env python3
"""Correct the missing bundle OS metadata, ad-hoc sign, and seal final test bytes."""
import hashlib,json,os,pathlib,plistlib,subprocess
root=pathlib.Path('universal-runtime')
manifest=json.loads((root/'SHA256.json').read_text())
for rel,expected in manifest.items():
    assert hashlib.sha256((root/rel).read_bytes()).hexdigest()==expected,rel
source=json.loads((root/'source.json').read_text())
assert source['commit']==os.environ['HEM_COMPILED_SHA']
for rel in ['HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.app',
            'Tests/VST3Fixture/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3']:
    bundle=root/rel;p=bundle/'Contents/Info.plist'
    info=plistlib.loads(p.read_bytes())
    existing=info.get('LSMinimumSystemVersion')
    assert existing is None or existing=='12.0'
    info['LSMinimumSystemVersion']='12.0'
    p.write_bytes(plistlib.dumps(info))
    # Metadata changes invalidate bundle seals. Local ad-hoc signing is not notarization.
    subprocess.run(['codesign','--force','--deep','--sign','-',str(bundle)],check=True)
    subprocess.run(['codesign','--verify','--deep','--strict',str(bundle)],check=True)
source['packaging_commit']=os.environ['GITHUB_SHA']
source['metadata_correction']='Explicit macOS 12.0 Info.plist minimum; ad-hoc bundle reseal'
(root/'source.json').write_text(json.dumps(source,indent=2))
hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file() and p.name!='SHA256.json'}
(root/'SHA256.json').write_text(json.dumps(hashes,indent=2))
print('Original archive bytes verified; minimum OS metadata corrected; ad-hoc signatures verified; final hashes sealed')
