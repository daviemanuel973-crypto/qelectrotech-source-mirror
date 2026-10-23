#!/usr/bin/env python3
"""Run the shipped executable with MSYS2/build-tool PATH removed."""
import json,os,pathlib,subprocess,sys,tempfile
root=pathlib.Path(sys.argv[1]).resolve()
exe=root/'bin'/'QElectroTech.exe'
for path in [exe,root/'bin/platforms/qwindows.dll',root/'elements',root/'lang',root/'Abrir QElectroTech.bat']:
    if not path.exists():raise SystemExit(f'Missing packaged dependency: {path}')
if not list((root/'elements').rglob('*.elmt')):raise SystemExit('Empty element collection')
if not list((root/'lang').glob('qet_pt*.qm')):raise SystemExit('Portuguese translation missing')
env=os.environ.copy()
env['PATH']=str(exe.parent)+os.pathsep+str(pathlib.Path(env.get('SystemRoot',r'C:\Windows'))/'System32')
for key in ['QT_PLUGIN_PATH','QML2_IMPORT_PATH','QML_IMPORT_PATH']:env.pop(key,None)
env['QT_QPA_PLATFORM']='windows'
fixture=root/'documentacao/QET-Industrial-Demonstracao.qet'
if not fixture.exists():fixture=root/'examples/industrial.qet'
if not fixture.exists():raise SystemExit('No actual QET project available for runtime test')
with tempfile.TemporaryDirectory(prefix='qet portable space ') as work:
    output=pathlib.Path(work)/'opened-project.json'
    command=[str(exe),'--info',str(fixture),'-o',str(output),'--common-elements-dir='+str(root/'elements'),
        '--common-tbt-dir='+str(root/'titleblocks'),'--lang-dir='+str(root/'lang'),'--config-dir='+work]
    result=subprocess.run(command,cwd=work,env=env,capture_output=True,text=True,timeout=180)
    if result.returncode or not output.exists():raise SystemExit(f'Packaged Windows run failed ({result.returncode}): {result.stdout}\n{result.stderr}')
    json.loads(output.read_text(encoding='utf-8-sig'))
(root/'validation').mkdir(exist_ok=True)
(root/'validation/portable-runtime.txt').write_text('PASS: packaged Windows executable opened an actual .qet with Qt windows platform and isolated PATH.\nInteractive GUI acceptance is not claimed by this check.\n',encoding='utf-8')
print('PASS packaged Windows runtime, Qt windows plugin, actual QET project, isolated PATH')
