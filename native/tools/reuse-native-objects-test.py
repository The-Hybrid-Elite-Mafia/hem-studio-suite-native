#!/usr/bin/env python3
"""Compile one source test against the freshly built native app objects.
This avoids rebuilding the exact same JUCE modules for each console test.
Normal CMake test targets remain the portable release-build workflow.
"""
from pathlib import Path
import argparse,shlex,subprocess,os
p=argparse.ArgumentParser();p.add_argument('test',help='basename in Tests, e.g. AudioMetersTests');p.add_argument('--build',default='build-native');args=p.parse_args()
root=Path(__file__).resolve().parents[1];build=(root/args.build).resolve();source=root/'Tests'/f'{args.test}.cpp'
if not source.is_file():raise SystemExit(f'Unknown test {args.test}')
output=build/'reused-tests'/args.test;output.mkdir(parents=True,exist_ok=True)
flags={}
for line in (build/'CMakeFiles/HEMStudioSuite.dir/flags.make').read_text().splitlines():
 if ' = ' in line:
  name,value=line.split(' = ',1);flags[name]=value
compile_args=['/usr/bin/c++']+shlex.split(flags['CXX_DEFINES'])+shlex.split(flags['CXX_INCLUDES'])+shlex.split(flags['CXX_FLAGS'])+['-c',str(source),'-o',str(output/'test.o')]
link_args=shlex.split((build/'CMakeFiles/HEMStudioSuite.dir/link.txt').read_text())
link_args=[arg for arg in link_args if not arg.endswith('/Source/Main.cpp.o') and not arg.endswith('/Source/MainComponent.cpp.o')]
link_args.insert(1,str(output/'test.o'));link_args[link_args.index('-o')+1]=str(output/args.test)
(output/'commands.txt').write_text(shlex.join(compile_args)+'\n'+shlex.join(link_args)+'\n'+str(output/args.test)+'\n')
for phase,command in [('compile',compile_args),('link',link_args),('run',[str(output/args.test)])]:
 print(f'{phase.upper()} {args.test}',flush=True)
 with (output/f'{phase}.log').open('w') as log:
  result=subprocess.run(command,cwd=output if phase=='run' else build,stdout=log,stderr=subprocess.STDOUT)
 text=(output/f'{phase}.log').read_text();print(text if phase == 'run' else '\n'.join(text.splitlines()[-45:]),flush=True)
 if result.returncode:raise SystemExit(result.returncode)
