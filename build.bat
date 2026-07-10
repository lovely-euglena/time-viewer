@echo off
title Collect Build Outputs
setlocal enabledelayedexpansion

set OUTPUT_DIR=output
set PROJECT=TimeDisplay

echo ========================================
echo  Collecting build outputs to .\%OUTPUT_DIR%
echo ========================================
echo.

set /p VERSION=Enter version number (e.g. 2.0.2):
if "%VERSION%"=="" (
    echo Version cannot be empty!
    pause
    exit /b 1
)
echo.

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

for %%C in (Debug Release Release-Full) do (
    for %%P in (x86 x64) do (
        set SRC=%%C\%%P\%PROJECT%-%%C-%%P-.exe
        set DST=%PROJECT%-%%C-%%P-%VERSION%.exe
        if exist "!SRC!" (
            copy /Y "!SRC!" "%OUTPUT_DIR%\!DST!" >nul
            echo   [OK] !DST!
        ) else (
            echo   [--] %PROJECT%-%%C-%%P-.exe
        )

        if /I "%%C"=="Debug" (
            set SRC_PDB=%%C\%%P\%PROJECT%-%%C-%%P-.pdb
            set DST_PDB=%PROJECT%-%%C-%%P-%VERSION%.pdb
            if exist "!SRC_PDB!" (
                copy /Y "!SRC_PDB!" "%OUTPUT_DIR%\!DST_PDB!" >nul
                echo   [OK] !DST_PDB!
            )
        )
    )
)

echo.
echo ========================================
echo  Done! Files in .\%OUTPUT_DIR%\
echo ========================================
echo.
dir /b "%OUTPUT_DIR%"
echo.
pause
