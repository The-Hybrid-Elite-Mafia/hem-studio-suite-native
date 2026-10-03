#!/usr/bin/env python3
"""Prepare and verify the same universal runtime on both native Mac architectures."""
import hashlib, json, os, pathlib, plistlib, re, shutil, subprocess, sys
ROOT=pathlib.Path.cwd().resolve()
BUILD=ROOT/'build-universal'
PAYLOAD=ROOT/'universal-runtime'
LOG=ROOT/'universal-logs'
LOG.mkdir(exist_ok=True)
APP_REL=pathlib.Path('HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.app')
FIXTURE_REL=pathlib.Path('Tests/VST3Fixture/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3')
def out(*args): return subprocess.check_output(args,text=True).strip()
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def inventory(p):
    data=json.loads(p.read_text())
    expected=sorted((ROOT/'ci/macos/expected-tests.txt').read_text().split())
    assert sorted(t['name'] for t in data['tests'])==expected and len(expected)==29
    return data

def stage():
    inv=inventory(LOG/'test-inventory.json')
    PAYLOAD.mkdir()
    for t in inv['tests']:
        src=pathlib.Path(t['command'][0]); rel=src.relative_to(BUILD)
        dst=PAYLOAD/rel; dst.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(src,dst)
    for rel in [APP_REL,FIXTURE_REL]: shutil.copytree(BUILD/rel,PAYLOAD/rel,symlinks=True)
    # Test commands, arguments and working directories are preserved; only the build-root prefix is relocated.
    for t in inv['tests']:
        t['command']=[x.replace(str(BUILD),'@RUNTIME@') for x in t['command']]
        for prop in t.get('properties',[]):
            if isinstance(prop.get('value'),str): prop['value']=prop['value'].replace(str(BUILD),'@RUNTIME@')
    (PAYLOAD/'tests.json').write_text(json.dumps(inv,indent=2))
    (PAYLOAD/'source.json').write_text(json.dumps({'commit':os.environ['GITHUB_SHA'],'deployment_target':'12.0','build_root':str(BUILD)},indent=2))
    # Preserve checksums for every regular file; identical bytes must reach each architecture's test job.
    hashes={str(p.relative_to(PAYLOAD)):digest(p) for p in sorted(PAYLOAD.rglob('*')) if p.is_file()}
    (PAYLOAD/'SHA256.json').write_text(json.dumps(hashes,indent=2))

def verify():
    assert json.loads((PAYLOAD/'source.json').read_text())['commit']==os.environ['GITHUB_SHA']
    for rel,h in json.loads((PAYLOAD/'SHA256.json').read_text()).items(): assert digest(PAYLOAD/rel)==h, rel
    reports=[]
    for p in sorted(PAYLOAD.rglob('*')):
        if not p.is_file() or p.is_symlink(): continue
        description=out('file','-b',str(p))
        if 'Mach-O' not in description: continue
        arches=out('lipo','-archs',str(p)).split()
        assert set(arches)=={'arm64','x86_64'}, (p,arches)
        slices={}
        for arch in ['arm64','x86_64']:
            load=out('otool','-arch',arch,'-l',str(p))
            minima=re.findall(r'\bminos\s+(\d+(?:\.\d+)*)',load)
            if not minima: minima=re.findall(r'\bversion\s+(\d+(?:\.\d+)*)',load)
            assert minima and all(tuple(map(int,x.split('.'))) <= (12,0,0) for x in minima), (p,arch,minima)
            deps=out('otool','-arch',arch,'-L',str(p))
            depnames=[line.strip().split(' (compatibility')[0] for line in deps.splitlines()[1:] if '(compatibility' in line]
            # This app bundles no non-system dylibs. Reject accidental Homebrew/build-machine dependencies.
            assert all(d.startswith(('/System/Library/','/usr/lib/')) for d in depnames),(p,depnames)
            slices[arch]={'minimum_os':minima,'dependencies':depnames}
        reports.append({'path':str(p.relative_to(PAYLOAD)),'architectures':arches,'slices':slices})
    assert len(reports)>=31, len(reports) # 29 tests, application and fixture
    app=PAYLOAD/APP_REL
    info=plistlib.loads((app/'Contents/Info.plist').read_bytes())
    assert info.get('LSMinimumSystemVersion')=='12.0',info.get('LSMinimumSystemVersion')
    subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
    subprocess.run(['codesign','--verify','--deep','--strict',str(PAYLOAD/FIXTURE_REL)],check=True)
    (LOG/'universal-binary-audit.json').write_text(json.dumps(reports,indent=2))
    (LOG/'bundle-info.json').write_text(json.dumps(info,indent=2,default=str))
    print(f'PASS: {len(reports)} Mach-O files each contain arm64 and x86_64; file checksums intact; deployment target 12.0; system-only dynamic dependencies')

def ctest():
    inv=inventory(PAYLOAD/'tests.json')
    def q(s):
        s=str(s).replace('@RUNTIME@',str(PAYLOAD))
        assert ']==]' not in s
        return '[==['+s+']==]'
    lines=[]
    for t in inv['tests']:
        lines.append('add_test('+q(t['name'])+' '+' '.join(q(x) for x in t['command'])+')')
        props={p['name']:p['value'] for p in t.get('properties',[]) if not p['name'].startswith('_')}
        props.setdefault('WORKING_DIRECTORY',str(PAYLOAD)); props.setdefault('TIMEOUT',240)
        wd=str(props['WORKING_DIRECTORY']).replace('@RUNTIME@',str(PAYLOAD))
        pathlib.Path(wd).mkdir(parents=True,exist_ok=True)
        for k,v in props.items():
            if isinstance(v,list): v=';'.join(str(x) for x in v)
            if isinstance(v,bool): v='TRUE' if v else 'FALSE'
            lines.append('set_tests_properties('+q(t['name'])+' PROPERTIES '+q(k)+' '+q(v)+')')
    (PAYLOAD/'CTestTestfile.cmake').write_text('\n'.join(lines)+'\n')

if __name__=='__main__': {'stage':stage,'verify':verify,'ctest':ctest}[sys.argv[1]]()
