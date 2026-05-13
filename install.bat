@echo off
setlocal enabledelayedexpansion

set "INSTALL_DIR=%LOCALAPPDATA%\Programs\Luna"

:parse_args
if "%~1"=="" goto end_args
if /i "%~1"=="--prefix" (
    if "%~2"=="" (
        echo Error: --prefix requires a directory argument.
        exit /b 1
    )
    set "INSTALL_DIR=%~2"
    shift
    shift
    goto parse_args
)
echo Usage: install.bat [--prefix INSTALL_DIR]
exit /b 1
:end_args

echo Building Luna...
zig build
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b 1
)

echo Installing to %INSTALL_DIR%...
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

copy /Y "zig-out\bin\luna.exe" "%INSTALL_DIR%\"
copy /Y "zig-out\bin\lunarun.dll" "%INSTALL_DIR%\"

if exist "zig-out\bin\luna.pdb" copy /Y "zig-out\bin\luna.pdb" "%INSTALL_DIR%\"
if exist "zig-out\bin\lunarun.pdb" copy /Y "zig-out\bin\lunarun.pdb" "%INSTALL_DIR%\"

echo.
echo Luna installed to %INSTALL_DIR%\
echo.
echo Add to your Environment Variables:
