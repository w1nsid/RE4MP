@echo off
setlocal

REM RE4MP Build Script
REM Run from a Developer Command Prompt for VS 2022, or ensure cl.exe and link.exe are on PATH.

echo ============================================
echo  RE4MP Build Script
echo ============================================

REM Check for cl.exe
where cl.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: cl.exe not found. Run this from a Developer Command Prompt for VS 2022.
    echo   Or run: "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
    exit /b 1
)

REM Create output directory
if not exist "build" mkdir build

echo.
echo [1/3] Building RE4MP.dll ...
cl.exe /nologo /EHsc /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_WINDLL" /I "RE4MP" ^
    /Fo"build\\" ^
    RE4MP\RE4MP.cpp RE4MP\detours.cpp ^
    /link /DLL /OUT:"build\RE4MP.dll" ^
    "RE4MP\lib\detours.lib" ws2_32.lib kernel32.lib user32.lib advapi32.lib

if %ERRORLEVEL% neq 0 (
    echo ERROR: RE4MP.dll build failed.
    exit /b 1
)
echo RE4MP.dll built successfully.

echo.
echo [2/3] Building RE4MPInjector.exe ...
cl.exe /nologo /EHsc /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" ^
    /Fo"build\\" ^
    RE4MPInjector\RE4MPInjector.cpp ^
    /link /OUT:"build\RE4MPInjector.exe" ^
    kernel32.lib user32.lib advapi32.lib
if %ERRORLEVEL% neq 0 (
    echo ERROR: RE4MPInjector.exe build failed.
    exit /b 1
)
echo RE4MPInjector.exe built successfully.

echo.
echo [3/3] Building RE4MPServer.exe (Windows) ...
cl.exe /nologo /O2 /W3 /D "WIN32" ^
    /Fo"build\\" ^
    RE4MPServer\re4mp_server.c ^
    /link /OUT:"build\RE4MPServer.exe" ^
    ws2_32.lib
if %ERRORLEVEL% neq 0 (
    echo WARNING: RE4MPServer.exe build failed. Use Makefile on Linux instead.
) else (
    echo RE4MPServer.exe built successfully.
)

echo.
echo ============================================
echo  Build complete! Output in build\
echo    build\RE4MP.dll
echo    build\RE4MPInjector.exe
echo    build\RE4MPServer.exe
echo ============================================
echo.
echo Server (Linux): cd RE4MPServer ^&^& make
echo Place RE4MP.dll next to RE4MPInjector.exe, then run RE4MPInjector.exe as Admin with the game running.

endlocal
