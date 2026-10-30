@echo off
setlocal
cd /d "%~dp0"
start "" "%~dp0bin\QElectroTech.exe" --common-elements-dir="%~dp0elements" --common-tbt-dir="%~dp0titleblocks" --lang-dir="%~dp0lang" --config-dir="%~dp0conf" --data-dir="%~dp0conf" %*
