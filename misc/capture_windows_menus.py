#!/usr/bin/env python3
"""Capture the shipped Windows editor and its menus on an interactive desktop."""
import argparse
from collections import Counter
import ctypes
from ctypes import wintypes
import json
import os
import platform
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from PIL import ImageGrab
from pywinauto import Desktop


def contrast(a, b):
    def luminance(rgb):
        def linear(c):
            c /= 255
            return c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4
        return sum(w * linear(c) for w, c in zip((.2126, .7152, .0722), rgb))
    a, b = sorted((luminance(a), luminance(b)))
    return (b + .05) / (a + .05)


def verify_labels(screen, menus):
    """Measure the actual desktop pixels inside the accessible menu cells."""
    result = []
    for menu in menus:
        rect = menu.rectangle()
        crop = screen.crop((max(0, rect.left + 4), max(0, rect.top + 4),
                            min(screen.width, rect.right - 4), min(screen.height, rect.bottom - 4))).convert('RGB')
        colors = Counter(crop.getdata())
        if not colors:
            raise RuntimeError('Empty visible menu cell: ' + menu.window_text())
        background = colors.most_common(1)[0][0]
        readable = sum(count for color, count in colors.items() if contrast(color, background) >= 4.5)
        if readable < 10:
            raise RuntimeError('Blank or unreadable Windows menu title: ' + menu.window_text())
        result.append({'title': menu.window_text(), 'readable_pixels': readable,
                       'rect': [rect.left, rect.top, rect.right, rect.bottom]})
    if len(result) < 4:
        raise RuntimeError('The editor did not expose at least four visible menu titles')
    return result


def process_windows(pid):
    user32 = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    handles = []
    @callback_type
    def visit(handle, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(handle):
            handles.append(handle)
        return True
    if not user32.EnumWindows(visit, 0):
        raise ctypes.WinError()
    desktop = Desktop(backend='win32')
    return [desktop.window(handle=handle).wrapper_object() for handle in handles]


def verify_popup_text(screen, popups, output, scale):
    rows = []
    tree = []
    for popup in popups:
        accessible = Desktop(backend='uia').window(handle=popup.handle)
        for item in accessible.descendants():
            rect = item.rectangle()
            tree.append(str(item.element_info) + ' ' + str(rect))
            if not item.window_text() or not (8 < rect.height() < 80 * float(scale)):
                continue
            if item.element_info.control_type != 'MenuItem' and item.element_info.class_name != 'QAction':
                continue
            # Only the label column: an icon, submenu arrow or shortcut must
            # never make a completely blank item label pass this check.
            left = max(0, rect.left + round(32 * float(scale)))
            right = min(screen.width, rect.left + round(rect.width() * .55))
            if right <= left:
                continue
            crop = screen.crop((left, max(0, rect.top + 3), right,
                                min(screen.height, rect.bottom - 3))).convert('RGB')
            colors = Counter(crop.getdata())
            background = colors.most_common(1)[0][0]
            minimum = 4.5 if item.is_enabled() else 1.5
            pixels = sum(n for color, n in colors.items() if contrast(color, background) >= minimum)
            rows.append({'title': item.window_text(), 'enabled': item.is_enabled(),
                         'readable_label_pixels': pixels,
                         'label_rect': [left, rect.top + 3, right, rect.bottom - 3]})
    output.write_text('\n'.join(tree), encoding='utf-8')
    (output.with_suffix('.json')).write_text(json.dumps(rows, indent=2), encoding='utf-8')
    if len(rows) < 5:
        raise RuntimeError('Expected at least five actual popup menu labels')
    invisible = [row['title'] for row in rows if row['readable_label_pixels'] < 10]
    if invisible:
        raise RuntimeError('Blank popup item labels: ' + ', '.join(invisible))
    return rows


def capture(root, output, style, language, scale, application_color=''):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='qet menu visual ') as work:
        work = Path(work)
        settings = work / 'settings' / 'QElectroTech'
        settings.mkdir(parents=True)
        ini = '[General]\nlang=' + language + '\n'
        if application_color:
            ini += 'usesystemcolors=false\ncustomapplicationcolor=' + application_color + '\n'
        (settings / 'QElectroTech.ini').write_text(ini, encoding='utf-8')
        project = work / 'menus.qet'
        shutil.copyfile(root / 'examples' / '741.qet', project)
        env = os.environ.copy()
        env['PATH'] = str(root / 'bin') + os.pathsep + str(Path(env.get('SYSTEMROOT', r'C:\Windows')) / 'System32')
        for key in ('QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'QT_STYLE_OVERRIDE'):
            env.pop(key, None)
        env['QT_QPA_PLATFORM'] = 'windows'
        env['QT_SCALE_FACTOR'] = scale
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
                deadline = time.monotonic() + 90
                window = None
                backup_answered = False
                while process.poll() is None and time.monotonic() < deadline:
                    window = None
                    for candidate in process_windows(process.pid):
                        title = candidate.window_text()
                        if title in ('Create a backup copy?', 'Criar uma cópia de segurança?'):
                            candidate.post_message(0x0100, 0x1B, 1)
                            backup_answered = True
                            continue
                        rect = candidate.rectangle()
                        # Qt also has an enabled, untitled helper window. It
                        # must never be maximized or mistaken for the editor.
                        if ('QElectroTech' in title and candidate.is_enabled()
                                and candidate.style() & 0x00040000
                                and rect.width() > 600 and rect.height() > 350):
                            window = candidate
                    if window and backup_answered:
                        time.sleep(1)
                        if window.is_enabled():
                            break
                    time.sleep(.2)
                if not window or not backup_answered or process.poll() is not None:
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
                uia = Desktop(backend='uia').window(handle=window.handle)
                descendants = uia.descendants()
                (output / 'uia-tree.txt').write_text(
                    '\n'.join(str(c.element_info) + ' ' + str(c.rectangle())
                              for c in descendants), encoding='utf-8')
                bars = [c for c in descendants if c.element_info.class_name == 'QMenuBar']
                if len(bars) != 1:
                    raise RuntimeError('Expected one actual QET menu bar')
                menus = [c for c in bars[0].children() if c.window_text() and c.is_enabled()
                         and c.rectangle().width() > 8 and c.rectangle().height() > 8]
                labels = verify_labels(ImageGrab.grab(all_screens=True), menus)
                def popup_windows():
                    return [c for c in process_windows(process.pid) if 'Popup' in c.class_name()]
                window.type_keys('{VK_MENU}{DOWN}')
                time.sleep(1)
                screenshot('02-keyboard-menu')
                keyboard_popups = len(popup_windows())
                if not keyboard_popups:
                    raise RuntimeError('Alt followed by Down did not open a Windows menu')
                keyboard_labels = verify_popup_text(ImageGrab.grab(all_screens=True), popup_windows(),
                                                     output / '02-popup-uia.txt', scale)
                window.type_keys('{ESC}{ESC}')
                # The menu row starts just below the native title bar. Record
                # both client and window geometry to make the click auditable.
                point = wintypes.POINT(0, 0)
                ctypes.windll.user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
                ctypes.windll.user32.ClientToScreen(window.handle, ctypes.byref(point))
                first = menus[0].rectangle()
                window.click_input(coords=((first.left + first.right) // 2 - rect.left,
                                           (first.top + first.bottom) // 2 - rect.top))
                time.sleep(1)
                screenshot('03-mouse-menu')
                mouse_popups = len(popup_windows())
                if not mouse_popups:
                    raise RuntimeError('Clicking the visible first title did not open a Windows menu')
                mouse_labels = verify_popup_text(ImageGrab.grab(all_screens=True), popup_windows(),
                                                  output / '03-popup-uia.txt', scale)
                window.type_keys('{ESC}{ESC}')
                details = {
                    'platform': 'Windows', 'style_override': style or 'default',
                    'qt_scale_factor': scale,
                    'application_color': application_color or 'system',
                    'os_version': platform.platform(),
                    'menu_labels': labels, 'keyboard_popup_windows': keyboard_popups,
                    'mouse_popup_windows': mouse_popups,
                    'keyboard_popup_labels': keyboard_labels, 'mouse_popup_labels': mouse_labels,
                    'language': language, 'window_title': window.window_text(),
                    'window_rect': [rect.left, rect.top, rect.right, rect.bottom],
                    'client_origin': [point.x, point.y],
                    'package_source_commit': (root / 'validation/source-commit.txt').read_text().strip(),
                }
                print(json.dumps(details), flush=True)
                (output / 'capture.json').write_text(json.dumps(details, indent=2), encoding='utf-8')
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
    parser.add_argument('--scale', default='1.25')
    parser.add_argument('--allow-unreadable', action='store_true',
                        help='Diagnostic baseline only: retain failed captures without passing them')
    args = parser.parse_args()
    ctypes.windll.user32.SetProcessDPIAware()
    failures = []
    cases = [('', 'en', ''), ('Fusion', 'en', ''), ('', 'pt_BR', ''),
             ('', 'pt_BR', '#ffffff'), ('Fusion', 'pt_BR', '#ffffff'),
             ('', 'pt_BR', '#303030')]
    for style, language, color in cases:
        name = (style or 'default') + '-' + language + ('-' + color[1:] if color else '')
        output = args.output / name
        try:
            capture(args.package.resolve(), output, style, language, args.scale, color)
        except Exception as error:
            failure = {'case': name, 'error': str(error)}
            failures.append(failure)
            output.mkdir(parents=True, exist_ok=True)
            (output / 'failure.json').write_text(json.dumps(failure, indent=2), encoding='utf-8')
            print(json.dumps(failure), flush=True)
    info_path = args.package / 'validation' / 'build-info.json'
    if info_path.exists():
        info = json.loads(info_path.read_text(encoding='utf-8'))
        info['packaged_windows_popup_text'] = 'failed' if failures else 'passed'
        info['menu_validation_scale_factor'] = args.scale
        info_path.write_text(json.dumps(info, indent=2), encoding='utf-8')
    if failures and not args.allow_unreadable:
        raise SystemExit('Popup text validation failed in ' + str(len(failures)) + ' cases')
