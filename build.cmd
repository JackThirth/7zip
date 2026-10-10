@echo off
rem Builds the 7-Zip File Manager, GUI, shell extension, 7z.dll and console for x64.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PLATFORM=x64
set ROOT=%~dp0CPP\7zip
for %%D in (Bundles\Format7zF Bundles\Fm UI\GUI UI\Explorer UI\Console) do (
  echo === %%D ===
  pushd "%ROOT%\%%D" || exit /b 1
  nmake /nologo %* || (popd & exit /b 1)
  popd
)
endlocal
