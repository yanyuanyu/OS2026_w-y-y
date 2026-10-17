@echo off
chcp 65001 >nul
setlocal
if not exist "%~dp0..\images_terminal" mkdir "%~dp0..\images_terminal"
pushd "%~dp0..\..\code"
if errorlevel 1 exit /b 1
mode con: cols=145 lines=44
for %%S in (01-build 02-grade 03-bestfit-host 04-buddy-host 05-buddy-qemu 06-boundaries 07-guest-gdb 08-source-audit 09-bestfit-qemu) do (
    cls
    title LAB2-%%S
    echo Running real commands for screenshot: %%S
    echo wsl.exe -d Ubuntu-22.04 -- python3 ../report/capture/run_terminal_case.py %%S
    wsl.exe -d Ubuntu-22.04 -- python3 ../report/capture/run_terminal_case.py %%S
    if errorlevel 1 (
        echo ERROR: command failed. Keep this output for diagnosis.
        pause
        popd
        exit /b 1
    )
    echo.
    echo Capture THIS terminal window and save as %%S.png in images_terminal.
    echo Keep the title, command and success output visible. Do not use a browser image.
    echo After saving the screenshot, press any key for the next case.
    pause >nul
)
echo All nine command cases completed. Screenshots must be saved manually.
pause
popd
endlocal
