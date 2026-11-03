@echo off
chcp 65001 >nul
mode con: cols=138 lines=48
title Lab3-B-%1
wsl.exe -d Ubuntu-22.04 -- python3 "/mnt/c/Users/13789/Desktop/操作系统/实验/lab3/提交_严苑毓_B/report/capture/run_case.py" %1
echo.
echo Fresh test finished. This window is held for a screenshot.
pause >nul
