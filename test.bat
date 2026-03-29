@echo off
setlocal

set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "TESTS=%ROOT%tests"
set "BUILD=%ROOT%build-tests"
set "GXX=C:\ProgramData\mingw64\mingw64\bin\g++.exe"

if not exist "%BUILD%" mkdir "%BUILD%"

if not exist "%GXX%" (
    echo g++.exe not found: "%GXX%"
    exit /b 1
)

"%GXX%" -std=c++17 -Wall -Wextra -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS ^
  -I"%SRC%" ^
  -o "%BUILD%\test_core.exe" ^
  "%TESTS%\test_core.cpp" ^
  "%SRC%\affinity.cpp" ^
  "%SRC%\audio.cpp" ^
  "%SRC%\crash.cpp" ^
  "%SRC%\framerate.cpp" ^
  "%SRC%\input.cpp" ^
  "%SRC%\startup.cpp" ^
  "%SRC%\timing.cpp" ^
  "%SRC%\version.cpp" ^
  "%SRC%\config.cpp" ^
  "%SRC%\iat_hook.cpp" ^
  "%SRC%\logger.cpp" ^
  "%SRC%\platform.cpp" ^
  "%SRC%\render.cpp" ^
  -lkernel32 -luser32 -ldbghelp

if errorlevel 1 (
    echo Tests failed to build or run.
    exit /b 1
)

"%BUILD%\test_core.exe"
