@echo off
setlocal
rem Build wuwapad.dll (x64) with MSVC. Adjust vcvars64 path if your VS differs.
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo [error] vcvars64.bat not found: %VCVARS%
  echo        edit build.bat and point VCVARS at your Visual Studio installation.
  exit /b 1
)
call "%VCVARS%" >nul 2>&1
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
cl /nologo /O2 /MT /LD "%~dp0src\wuwapad.c" /link /OUT:wuwapad.dll user32.lib
set RC=%ERRORLEVEL%
popd
if %RC% neq 0 ( echo [error] build failed & exit /b %RC% )
echo [ok] build\wuwapad.dll
endlocal
