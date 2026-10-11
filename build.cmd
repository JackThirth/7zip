@echo off
rem Builds the 7-Zip File Manager, GUI, shell extension, 7z.dll and console for x64.
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
  echo Visual Studio not found. Install Visual Studio 2022 with "Desktop development with C++".
  exit /b 1
)
set VSDIR=
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%I
if not defined VSDIR (
  echo Visual Studio C++ tools not found. Add the "Desktop development with C++" workload in Visual Studio Installer.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PLATFORM=x64
set ROOT=%~dp0CPP\7zip
for %%D in (Bundles\Format7zF Bundles\Fm UI\GUI UI\Explorer UI\Console) do (
  echo === %%D ===
  pushd "%ROOT%\%%D" || exit /b 1
  nmake /nologo %* || (popd & exit /b 1)
  popd
)
echo.
echo Build finished. Next: run package\install.ps1 (see README.md).
endlocal
