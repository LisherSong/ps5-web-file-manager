@echo off
REM Generate real encrypted ZIP fixtures for the host test suite.
REM Requires a 7-Zip compatible command line tool: 7-Zip, or the NanaZip
REM console alias that ships with the Store package (both accept the same
REM switches used here).
REM Outputs into tests\fixtures-real\ - kept OUT of tests\fixtures\ because
REM tests\make_fixtures.py wipes that directory on every test run. Only the
REM *.zip files are rewritten, so the RAR fixtures produced by
REM make-rar-fixtures.bat survive.
REM Rerun any time fixture needs change. NOTE: keep this file pure ASCII.
setlocal EnableDelayedExpansion

set SZEXE=
if exist "%ProgramFiles%\7-Zip\7z.exe" set SZEXE=%ProgramFiles%\7-Zip\7z.exe
if exist "%ProgramFiles(x86)%\7-Zip\7z.exe" set SZEXE=%ProgramFiles(x86)%\7-Zip\7z.exe
if exist "%~dp07z.exe" set SZEXE=%~dp07z.exe
if "%SZEXE%"=="" (
  for /f "delims=" %%I in ('where 7z 2^>nul') do if "!SZEXE!"=="" set SZEXE=%%I
)
if "%SZEXE%"=="" (
  echo [ERROR] No 7z.exe found. Install 7-Zip or NanaZip, or drop 7z.exe next to this script.
  exit /b 1
)
echo Using: %SZEXE%

set FIX=%~dp0fixtures-real
if not exist "%FIX%" mkdir "%FIX%"
del "%FIX%\enc-*.zip" 2>nul

set STAGE=%~dp0fixture-stage-zip
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\dir" 2>nul

REM Same payload shape as the RAR fixtures so both suites assert the same
REM file list and the same bytes.
echo ############### > "%STAGE%\root.txt"
echo zip v1.9 fixture root content >> "%STAGE%\root.txt"
echo nested payload line one > "%STAGE%\dir\nested.txt"
echo nested payload line two >> "%STAGE%\dir\nested.txt"

REM Work from inside the stage dir with RELATIVE names so the archive keeps
REM the dir\ structure (no -spf full paths).
pushd "%STAGE%"

echo.
echo [1/3] traditional PKWARE / ZipCrypto (password: secret123) - enc-zipcrypto.zip
"%SZEXE%" a -tzip -psecret123 -mem=ZipCrypto -y -bso0 -bsp0 "%FIX%\enc-zipcrypto.zip" root.txt dir\nested.txt
if errorlevel 1 echo   [FAIL] & goto :badpop

echo [2/3] WinZip AES-256 (password: secret123) - enc-aes256.zip
"%SZEXE%" a -tzip -psecret123 -mem=AES256 -y -bso0 -bsp0 "%FIX%\enc-aes256.zip" root.txt dir\nested.txt
if errorlevel 1 echo   [FAIL] & goto :badpop

echo [3/3] WinZip AES-256 with stored (uncompressed) entries - enc-aes256-store.zip
"%SZEXE%" a -tzip -psecret123 -mem=AES256 -mx0 -y -bso0 -bsp0 "%FIX%\enc-aes256-store.zip" root.txt dir\nested.txt
if errorlevel 1 echo   [FAIL] & goto :badpop

popd
rmdir /s /q "%STAGE%" 2>nul
echo.
echo OK. Fixtures written to %FIX%:
dir /b "%FIX%\enc-*.zip" 2>nul
exit /b 0

:badpop
popd
echo [ERROR] 7z command failed. Is this 7-Zip 21+ (or NanaZip) with ZIP encryption support?
exit /b 1
