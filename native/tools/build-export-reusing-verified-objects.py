#!/usr/bin/env python3
"""Rebuild all native host units; reuse only unchanged JUCE/baseline modules.
Portable standalone builds use CMakeLists.txt. Commands/logs are retained.
"""
from pathlib import Path
import argparse, hashlib, shlex, subprocess, json, time
p=argparse.ArgumentParser();p.add_argument('target',choices=['app','test','worker-test','brand-test','control-test','lifecycle-test','take-test','chain-test','stem-test','workspace-test','binding-test','core-test','plugin-meter-test','external-host-test','external-session-test','external-integration-test','session-test']);p.add_argument('test_args',nargs='*');args=p.parse_args()
root=Path(__file__).resolve().parents[1];original=root.parent/'native-host-integration';old=original/'build-native';build=root/'build-export';build.mkdir(exist_ok=True)
for name in ['Engine.h','Engine.cpp']:
    if (root/'Source/CoreBaseline'/name).read_bytes()!=(original/'Source/CoreBaseline'/name).read_bytes():raise SystemExit('Baseline identity mismatch')
commands=[]
def run(command,name):
    commands.append(command)
    with (build/(name+'.log')).open('w') as log:r=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
    print(name,r.returncode,flush=True)
    if r.returncode:print((build/(name+'.log')).read_text());raise SystemExit(r.returncode)
flags={}
for line in (old/'CMakeFiles/HEMStudioSuite.dir/flags.make').read_text().splitlines():
    if ' = ' in line:
        k,v=line.split(' = ',1);flags[k]=v
assets=build/'BinaryData';assets.mkdir(exist_ok=True)
asset_files=[root/'Assets/studio_suite_master.png',root/'Assets/ghost_brand_reference.png']
asset_hash=hashlib.sha256(b''.join(f.read_bytes() for f in asset_files)).hexdigest()
asset_stamp=assets/'fingerprint'
if not asset_stamp.exists() or asset_stamp.read_text()!=asset_hash:
    listing=assets/'inputs';listing.write_text('\n'.join(map(str,asset_files)))
    juceaide=old/'JUCE/tools/extras/Build/juceaide/juceaide_artefacts/Custom/juceaide'
    run([str(juceaide),'binarydata','BinaryData','BinaryData.h',str(assets),str(listing)],'assets-generate')
    art_objects=[]
    for cpp in sorted(assets.glob('BinaryData*.cpp')):
        obj=cpp.with_suffix('.o');run(['/usr/bin/c++','-O1','-c',str(cpp),'-o',str(obj)],cpp.stem+'-compile');art_objects.append(str(obj))
    run(['/usr/bin/ar','rcs',str(build/'libStudioArtwork.a')]+art_objects,'assets-archive');asset_stamp.write_text(asset_hash)
base=['/usr/bin/c++','-DJUCE_PLUGINHOST_VST3=1','-I'+str(assets)]+shlex.split(flags['CXX_DEFINES'])+shlex.split(flags['CXX_INCLUDES'])+shlex.split(flags['CXX_FLAGS'])
def compile_source(source):
    key=(source.relative_to(root).as_posix().replace('/','_') if source.is_relative_to(root) else 'JUCE_'+source.name);obj=build/(key+'.o');stamp=build/(key+'.dependencies.json');dep=build/(key+'.d')
    command=base+['-MMD','-MF',str(dep),'-c',str(source),'-o',str(obj)]
    def fingerprint(files):
        h=hashlib.sha256(str(command).encode())
        for f in files:
            p=Path(f)
            if not p.is_file():return None
            h.update(f.encode());h.update(p.read_bytes())
        return h.hexdigest()
    cached=json.loads(stamp.read_text()) if stamp.exists() else None
    if not obj.exists() or not cached or cached.get('version')!=2 or cached['hash']!=fingerprint(cached['files']):
        started=time.time_ns();run(command,key+'-compile')
        text=dep.read_text().replace('\\\n',' ');files=sorted(set(shlex.split(text.split(':',1)[1])))
        if any(Path(f).stat().st_mtime_ns>started for f in files):raise SystemExit('A dependency changed during compilation; rerun after edits settle: '+str(source))
        stamp.write_text(json.dumps({'version':2,'files':files,'hash':fingerprint(files)}))
    else:commands.append(command)
    return str(obj)

link=shlex.split((old/'CMakeFiles/HEMStudioSuite.dir/link.txt').read_text());newlink=[]
for a in link:
    if a.startswith('-Wl,--dependency-file='):continue
    marker='CMakeFiles/HEMStudioSuite.dir/Source/'
    if a.startswith(marker):
        relative=a[len(marker):-2]
        if relative in ['Main.cpp','MainComponent.cpp']:continue
        a=compile_source(root/'Source'/relative)
    elif a.endswith('/juce_audio_processors/juce_audio_processors.cpp.o'):
        a=compile_source(root.parent/'limiter-evidence-audit/dependencies/JUCE-8.0.10/modules/juce_audio_processors/juce_audio_processors.cpp')
    elif a=='libStudioArtwork.a':a=str(build/'libStudioArtwork.a')
    elif a.endswith(('.o','.a')) and not a.startswith('/'):a=str(old/a)
    newlink.append(a)
newlink.insert(1,compile_source(root/'Source/Systems/OfflineMixRenderer.cpp'))
newlink.insert(1,compile_source(root/'Source/Plugins/ExternalPluginHost.cpp'))
if (root/'Source/UI/NativeWorkspace.cpp').exists():newlink.insert(1,compile_source(root/'Source/UI/NativeWorkspace.cpp'))
if args.target=='app':
    newlink[1:1]=[compile_source(root/'Source/Main.cpp'),compile_source(root/'Source/MainComponent.cpp')]
    name='Hybrid Elite Mafia - Studio Suite'
else:
    if args.target in ['lifecycle-test','external-integration-test']:newlink.insert(1,compile_source(root/'Source/MainComponent.cpp'))
    name={'test':'OfflineMixTests','worker-test':'OfflineMixWorkerTests','brand-test':'StudioBrandingTests','control-test':'HardwareLookAndFeelTests','lifecycle-test':'NativeHostLifecycleTests','take-test':'TakeRecorderTests','chain-test':'ChainTests','stem-test':'StemMixerTests','workspace-test':'NativeWorkspaceTests','binding-test':'ProcessorControlBindingTests','core-test':'CoreMasterStageTests','plugin-meter-test':'PluginMeterDisplayTests','external-host-test':'ExternalHostTests','external-session-test':'ExternalSessionTests','external-integration-test':'ExternalIntegrationTests','session-test':'SessionTests'}[args.target]
    newlink.insert(1,compile_source(root/'Tests'/(name+'.cpp')))
if args.target=='lifecycle-test':newlink.insert(1,'-rdynamic')
exe=build/name;newlink[newlink.index('-o')+1]=str(exe);run(newlink,args.target+'-link')
(build/(args.target+'.commands.txt')).write_text('\n'.join(shlex.join(c) for c in commands)+'\n')
print(exe,hashlib.sha256(exe.read_bytes()).hexdigest(),flush=True)
if args.target!='app':
    run([str(exe)]+args.test_args,name+'.run');print((build/(name+'.run.log')).read_text(),flush=True)
