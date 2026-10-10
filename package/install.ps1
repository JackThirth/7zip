# Installs the built fork to a folder and registers the Windows 11 context menu.
# Prerequisites: build.cmd has been run, and make-cert.ps1 has been run once as Administrator.

param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\7-Zip Fork'),
  [switch]$RestartExplorer   # needed when reinstalling while Explorer has the old 7-zip.dll loaded
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$cpp = Join-Path $root 'CPP\7zip'
$packageName = '7-Zip.Fork.ContextMenu'

& (Join-Path $PSScriptRoot 'pack.ps1')

$existing = Get-AppxPackage -Name $packageName
if ($existing) {
  Write-Host 'Removing previous registration...'
  Remove-AppxPackage $existing.PackageFullName
}
if ($RestartExplorer) { Stop-Process -Name explorer -Force; Start-Sleep -Seconds 1 }

$files = @(
  'Bundles\Fm\x64\7zFM.exe',
  'UI\GUI\x64\7zG.exe',
  'UI\Console\x64\7z.exe',
  'Bundles\Format7zF\x64\7z.dll',
  'UI\Explorer\x64\7-zip.dll'
)
New-Item -ItemType Directory -Force $InstallDir | Out-Null
foreach ($f in $files) {
  $src = Join-Path $cpp $f
  if (-not (Test-Path $src)) { throw "Missing $src - run build.cmd first." }
  try { Copy-Item $src $InstallDir -Force }
  catch { throw "Could not overwrite $(Split-Path $f -Leaf) (in use?). Close 7-Zip windows, or re-run with -RestartExplorer." }
}
Copy-Item (Join-Path $root 'DOC\License.txt') $InstallDir -Force -ErrorAction SilentlyContinue

Add-AppxPackage -Path (Join-Path $PSScriptRoot 'out\7zip-contextmenu.msix') -ExternalLocation $InstallDir
if ($RestartExplorer) { Start-Process explorer }
Write-Host "Installed to $InstallDir and registered the Windows 11 context menu."
