@echo off
title Copy Build Outputs
setlocal enabledelayedexpansion

set "OUTPUT_DIR=output"
set "PROJECT=TimeDisplay"

echo ========================================
echo  Copying build outputs to .\%OUTPUT_DIR%\
echo ========================================
echo.

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

for %%C in (Debug Release Release-Full) do (
    for %%P in (x86 x64) do (
        :: 复制可执行文件
        set "EXE_PATH=%%C\%%P\%%C-%PROJECT%-%%P-.exe"
        if exist "!EXE_PATH!" (
            copy /Y "!EXE_PATH!" "%OUTPUT_DIR%\" >nul
            echo   [OK] %%C-%PROJECT%-%%P-.exe
        ) else (
            echo   [--] %%C-%PROJECT%-%%P-.exe ^(not found^)
        )

        :: Debug 配置额外复制 PDB 调试符号
        if /I "%%C"=="Debug" (
            set "PDB_PATH=%%C\%%P\%%C-%PROJECT%-%%P-.pdb"
            if exist "!PDB_PATH!" (
                copy /Y "!PDB_PATH!" "%OUTPUT_DIR%\" >nul
                echo   [OK] %%C-%PROJECT%-%%P-.pdb
            )
        )
    )
)

echo.
echo ========================================
echo  Done! Output in .\%OUTPUT_DIR%\
echo ========================================
echo.
dir /b "%OUTPUT_DIR%"
echo.
pause
