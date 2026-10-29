#!/usr/bin/env python3
"""Capture the shipped Windows editor and its menus on an interactive desktop."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from PIL import ImageGrab
from pywinauto import Desktop


def capture(root, output, style, language):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='qet menu visual ') as work:
        work = Path(work)
        settings = work / 'settings' / 'QElectroTech'
        settings.mkdir(parents=True)
        (settings / 'QElectroTech.ini').write_text(
            '[General]\nlang=' + language + '\n', encoding='utf-8')
        project = work / 'menus.qet'
        shutil.copyfile(root / 'examples' / '741.qet', project)
        env = os.environ.copy()
        env['PATH'] = str(root / 'bin') + os.pathsep + str(Path(env.get('SYSTEMROOT', r'C:\Windows')) / 'System32')
        for key in ('QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'QT_STYLE_OVERRIDE'):
            env.pop(key, None)
        env['QT_QPA_PLATFORM'] = 'windows'
        env['QET_SETTINGS_DIR'] = str(settings.parent)
        if style:
            env['QT_STYLE_OVERRIDE'] = style
        command = [str(root / 'bin' / 'QElectroTech.exe'), str(project),
                   '--common-elements-dir=' + str(root / 'elements'),
                   '--common-tbt-dir=' + str(root / 'titleblocks'),
                   '--lang-dir=' + str(root / 'lang'), '--config-dir=' + str(work),
                   '--data-dir=' + str(work)]
        with (output / 'process.log').open('w') as log:
            process = subprocess.Popen(command, cwd=work, env=env, stdout=log, stderr=log)
            try:
                desktop = Desktop(backend='win32')
                deadline = time.monotonic() + 90
                window = None
                while process.poll() is None and time.monotonic() < deadline:
                    for candidate in desktop.windows(process=process.pid):
                        if candidate.window_text() in ('Create a backup copy?', 'Criar uma cópia de segurança?'):
                            candidate.type_keys('{ESC}')
                        rect = candidate.rectangle()
                        if candidate.is_enabled() and rect.width() > 600 and rect.height() > 350:
                            window = candidate
                    if window:
                        time.sleep(2)
                        if window.is_enabled():
                            break
                    time.sleep(.2)
                if not window or process.poll() is not None:
                    raise RuntimeError('No ready packaged Windows editor')
                window.set_focus()
                window.maximize()
                time.sleep(2)
                rect = window.rectangle()
                def screenshot(name):
                    ImageGrab.grab(all_screens=True).save(output / (name + '-desktop.png'))
                    ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom),
                                   all_screens=True).save(output / (name + '.png'))
                screenshot('01-menubar')
                try:
                    uia = Desktop(backend='uia').window(handle=window.handle)
                    (output / 'uia-tree.txt').write_text(
                        '\n'.join(str(c.element_info) + ' ' + str(c.rectangle())
                                  for c in uia.descendants()), encoding='utf-8')
                except Exception as error:
                    (output / 'uia-tree.txt').write_text(str(error), encoding='utf-8')
                window.type_keys('{VK_MENU}{DOWN}')
                time.sleep(1)
                screenshot('02-keyboard-menu')
                window.type_keys('{ESC}{ESC}')
                # The menu row starts just below the native title bar. Record
                # both client and window geometry to make the click auditable.
                point = wintypes.POINT(0, 0)
                ctypes.windll.user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
                ctypes.windll.user32.ClientToScreen(window.handle, ctypes.byref(point))
                window.click_input(coords=(point.x - rect.left + 25, point.y - rect.top + 12))
                time.sleep(1)
                screenshot('03-mouse-menu')
                window.type_keys('{ESC}{ESC}')
                (output / 'capture.json').write_text(json.dumps({
                    'platform': 'Windows', 'style_override': style or 'default',
                    'language': language, 'window_title': window.window_text(),
                    'window_rect': [rect.left, rect.top, rect.right, rect.bottom],
                    'client_origin': [point.x, point.y],
                    'package_source_commit': (root / 'validation/source-commit.txt').read_text().strip(),
                }, indent=2), encoding='utf-8')
                window.close()
                process.wait(timeout=45)
                if process.returncode:
                    raise RuntimeError('Packaged editor crashed on close')
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    for style, language in [('', 'en'), ('Fusion', 'en'), ('', 'pt_BR')]:
        capture(args.package.resolve(), args.output / ((style or 'default') + '-' + language),
                style, language)
