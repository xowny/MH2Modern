@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "BUILD=%ROOT%build"
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC"
set "WINSDKROOT=C:\Program Files (x86)\Windows Kits\10"

if not exist "%VSROOT%" (
    echo MSVC toolchain root not found: "%VSROOT%"
    exit /b 1
)

if not exist "%WINSDKROOT%\Include" (
    echo Windows SDK include root not found: "%WINSDKROOT%\Include"
    exit /b 1
)

set "MSVCVER="
for /f "delims=" %%I in ('dir /b /ad /o-n "%VSROOT%"') do (
    if not defined MSVCVER set "MSVCVER=%%I"
)

if not defined MSVCVER (
    echo No MSVC toolset versions found under "%VSROOT%"
    exit /b 1
)

set "MSVC=%VSROOT%\%MSVCVER%"

set "SDKVER="
for /f "delims=" %%I in ('dir /b /ad /o-n "%WINSDKROOT%\Include"') do (
    if not defined SDKVER (
        if exist "%WINSDKROOT%\Include\%%I\ucrt\corecrt.h" (
            if exist "%WINSDKROOT%\Include\%%I\um\Windows.h" (
                if exist "%WINSDKROOT%\Lib\%%I\ucrt\x86\ucrt.lib" (
                    if exist "%WINSDKROOT%\Lib\%%I\um\x86\kernel32.lib" (
                        set "SDKVER=%%I"
                    )
                )
            )
        )
    )
)

if not defined SDKVER (
    echo No complete x86 Windows SDK was found under "%WINSDKROOT%"
    exit /b 1
)

if not exist "%BUILD%" mkdir "%BUILD%"

set "PATH=%MSVC%\bin\Hostx64\x86;%PATH%"
set "INCLUDE=%MSVC%\include;%WINSDKROOT%\Include\%SDKVER%\ucrt;%WINSDKROOT%\Include\%SDKVER%\shared;%WINSDKROOT%\Include\%SDKVER%\um;%WINSDKROOT%\Include\%SDKVER%\winrt;%WINSDKROOT%\Include\%SDKVER%\cppwinrt"
set "LIB=%MSVC%\lib\x86;%WINSDKROOT%\Lib\%SDKVER%\ucrt\x86;%WINSDKROOT%\Lib\%SDKVER%\um\x86"

echo Using MSVC %MSVCVER%
echo Using Windows SDK %SDKVER%

cl /nologo /std:c++17 /EHsc /W4 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%BUILD%\\" ^
  /LD ^
  "%SRC%\affinity.cpp" ^
  "%SRC%\audio.cpp" ^
  "%SRC%\dllmain.cpp" ^
  "%SRC%\config.cpp" ^
  "%SRC%\crash.cpp" ^
  "%SRC%\framerate.cpp" ^
  "%SRC%\hooks.cpp" ^
  "%SRC%\iat_hook.cpp" ^
  "%SRC%\input.cpp" ^
  "%SRC%\logger.cpp" ^
  "%SRC%\platform.cpp" ^
  "%SRC%\render.cpp" ^
  "%SRC%\startup.cpp" ^
  "%SRC%\timing.cpp" ^
  "%SRC%\version.cpp" ^
  /link user32.lib /OUT:"%BUILD%\MH2Modern.asi" /IMPLIB:"%BUILD%\MH2Modern.lib" /PDB:"%BUILD%\MH2Modern.pdb"

if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

copy /Y "%ROOT%MH2Modern.ini" "%BUILD%\MH2Modern.ini" >nul
echo Built "%BUILD%\MH2Modern.asi"
