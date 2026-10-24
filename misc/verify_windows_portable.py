#!/usr/bin/env python3
"""Run the shipped executable with MSYS2/build-tool PATH removed."""
import ctypes,json,os,pathlib,shutil,subprocess,sys,tempfile,time
from ctypes import wintypes

def visible_editor_windows(pid):
    """Find the packaged process's resizable top-level editor, not its splash."""
    user32=ctypes.windll.user32
    callback_type=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user32.EnumWindows.argtypes=[callback_type,wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user32.IsWindowVisible.argtypes=[wintypes.HWND]
    user32.GetWindowLongW.argtypes=[wintypes.HWND,ctypes.c_int]
    user32.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
    windows=[]
    @callback_type
    def visit(window,_):
        owner=wintypes.DWORD()
        user32.GetWindowThreadProcessId(window,ctypes.byref(owner))
        if owner.value!=pid or not user32.IsWindowVisible(window):return True
        style=user32.GetWindowLongW(window,-16)  # GWL_STYLE
        rect=wintypes.RECT()
        user32.GetClientRect(window,ctypes.byref(rect))
        if style & 0x00040000 and style & 0x00080000 and rect.right>=400 and rect.bottom>=250:
            windows.append(window)  # WS_THICKFRAME and WS_SYSMENU exclude a splash.
        return True
    if not user32.EnumWindows(visit,0):raise ctypes.WinError()
    return windows

def verify_gui(command,work,environment):
    # A file cannot fill up like a pipe while Qt loads a large project.
    with tempfile.TemporaryFile() as log:
        process=subprocess.Popen(command,cwd=work,env=environment,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+90
            windows=[]
            while time.monotonic()<deadline and process.poll() is None:
                windows=visible_editor_windows(process.pid)
                if windows:break
                time.sleep(.2)
            if not windows:raise RuntimeError('Packaged Windows GUI did not show an editor window')
            time.sleep(1)
            if process.poll() is not None:raise RuntimeError('Packaged Windows GUI exited immediately')
            post=ctypes.windll.user32.PostMessageW
            post.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
            for window in windows:
                if not post(window,0x0010,0,0):raise ctypes.WinError()  # WM_CLOSE
            process.wait(timeout=45)
            if process.returncode:
                log.seek(0)
                raise RuntimeError(f'Packaged GUI crashed on close ({process.returncode}): {log.read()[-4000:]!r}')
        finally:
            if process.poll() is None:
                process.kill();process.wait()

if sys.platform!='win32':raise SystemExit('This validation requires an actual Windows runner')
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
    command=[str(exe),'--info',str(fixture),str(output),'--common-elements-dir='+str(root/'elements'),
        '--common-tbt-dir='+str(root/'titleblocks'),'--lang-dir='+str(root/'lang'),'--config-dir='+work]
    result=subprocess.run(command,cwd=work,env=env,capture_output=True,text=True,timeout=180)
    if result.returncode or not output.exists():raise SystemExit(f'Packaged Windows run failed ({result.returncode}): {result.stdout}\n{result.stderr}')
    info=json.loads(output.read_text(encoding='utf-8-sig'))
    if not isinstance(info,dict) or info.get('diagrams',0)<1:
        raise SystemExit('Packaged executable did not report a real project with sheets')
    # Exercise a real native editor window and normal shutdown, with no build
    # directories or MSYS2 DLLs in PATH. Copying avoids changing packaged files.
    gui_project=pathlib.Path(work)/'portable-gui.qet'
    # The large legacy industrial example contains its own undersized native
    # nomenclature tables, which raise modal data-review warnings. Validate
    # the complete generated project above, and use a known single-sheet
    # project for the independent native-window/shutdown smoke test.
    gui_fixture=root/'examples/741.qet'
    if not gui_fixture.exists():gui_fixture=fixture
    shutil.copyfile(gui_fixture,gui_project)
    verify_gui([str(exe),str(gui_project),'--common-elements-dir='+str(root/'elements'),
        '--common-tbt-dir='+str(root/'titleblocks'),'--lang-dir='+str(root/'lang'),
        '--config-dir='+work,'--data-dir='+work],work,env)
(root/'validation').mkdir(exist_ok=True)
(root/'validation/portable-runtime.txt').write_text('PASS: packaged Windows executable opened an actual .qet with Qt windows platform and isolated PATH.\nPASS: native Windows editor window became visible and exited normally after WM_CLOSE.\nManual acceptance of the complete industrial workflow is not claimed by these checks.\n',encoding='utf-8')
(root/'validation/build-info.json').write_text(json.dumps({
    'source_commit':(root/'validation/source-commit.txt').read_text().strip(),
    'project_open_fixture':str(fixture.relative_to(root)),
    'gui_fixture':str(gui_fixture.relative_to(root)),
    'native_integration':os.environ.get('QET_NATIVE_TESTS','not-reported'),
    'ctest':os.environ.get('QET_CTEST','not-reported'),
    'packaged_windows_project_open':'passed',
    'packaged_windows_gui_open_and_close':'passed',
    'manual_acceptance':'not-run'},indent=2),encoding='utf-8')
print('PASS packaged Windows runtime, Qt windows plugin, actual QET project, isolated PATH')
