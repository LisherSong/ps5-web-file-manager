@echo off
REM Generate real RAR fixtures for the v1.9 host test suite.
REM Requires WinRAR's command-line Rar.exe (ships with a normal WinRAR install).
REM Outputs into tests\fixtures\ — rerun any time you change fixture needs.
setlocal EnableDelayedExpansion

set RAREXE=
if exist "%ProgramFiles%\WinRAR\Rar.exe" set RAREXE=%ProgramFiles%\WinRAR\Rar.exe
if exist "%ProgramFiles(x86)%\WinRAR\Rar.exe" set RAREXE=%ProgramFiles(x86)%\WinRAR\Rar.exe
if exist "%~dp0Rar.exe" set RAREXE=%~dp0Rar.exe
if "%RAREXE%"=="" (
  echo [ERROR] Rar.exe not found. Install WinRAR or drop Rar.exe next to this script.
  exit /b 1
)
echo Using: %RAREXE%
"%RAREXE%" 2>nul | findstr /c:"RAR " >nul || (echo [ERROR] %RAREXE% does not look like WinRAR & exit /b 1)

set FIX=%~dp0fixtures
set STAGE=%~dp0fixture-stage
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\dir" 2>nul

echo ############### > "%STAGE%\root.txt"
echo rar v1.9 fixture root content >> "%STAGE%\root.txt"
echo nested payload line one > "%STAGE%\dir\nested.txt"
echo nested payload line two >> "%STAGE%\dir\nested.txt"
echo file-b 0102030405060708090a > "%STAGE%\b.bin"

echo.
echo [1/4] RAR5 v6 single volume - basic-v6.rar
"%RAREXE%" a -ep1 -m2 -ma5 -idq "%FIX%\basic-v6.rar" "%STAGE%\root.txt" "%STAGE%\dir\nested.txt" "%STAGE%\b.bin"
if errorlevel 1 echo   [FAIL] & goto :bad

echo [2/4] RAR5 v6 multi-volume - vol.part1.rar + vol.part2.rar
"%RAREXE%" a -ep1 -m2 -ma5 -v200k -idq "%FIX%\vol.part1.rar" "%STAGE%\root.txt" "%STAGE%\dir\nested.txt" "%STAGE%\b.bin"
if errorlevel 1 echo   [FAIL] & goto :bad

echo [3/4] RAR5 v6 encrypted (password: secret123) - enc-v6.rar
"%RAREXE%" a -ep1 -m2 -ma5 -psecret123 -idq "%FIX%\enc-v6.rar" "%STAGE%\root.txt" "%STAGE%\dir\nested.txt"
if errorlevel 1 echo   [FAIL] & goto :bad

echo [4/4] RAR4 legacy - basic-rar4.rar
"%RAREXE%" a -ep1 -m2 -ma4 -idq "%FIX%\basic-rar4.rar" "%STAGE%\root.txt" "%STAGE%\dir\nested.txt"
if errorlevel 1 echo   [FAIL] & goto :bad

if exist "%STAGE%" rmdir /s /q "%STAGE%"
echo.
echo OK. Fixtures written to %FIX%:
dir /b "%FIX%\*.rar" 2>nul
exit /b 0

:bad
echo [ERROR] WinRAR command failed. Is this WinRAR 5+ with command line support?
exit /b 1
