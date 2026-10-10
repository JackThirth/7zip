# Builds and signs out\7zip-contextmenu.msix (the sparse package; contains only the manifest and logos).

$ErrorActionPreference = 'Stop'
$subject = 'CN=7-Zip Fork Dev'
$outDir = Join-Path $PSScriptRoot 'out'
$stage = Join-Path $outDir 'stage'
$msix = Join-Path $outDir '7zip-contextmenu.msix'

$sdkBin = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.*\x64\makeappx.exe" |
  Sort-Object { [version]$_.Directory.Parent.Name } | Select-Object -Last 1
if (-not $sdkBin) { throw 'makeappx.exe not found: install the Windows 10/11 SDK.' }
$makeappx = $sdkBin.FullName
$signtool = Join-Path $sdkBin.DirectoryName 'signtool.exe'

if (-not (Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject })) {
  throw "Signing certificate '$subject' not found: run make-cert.ps1 first."
}

Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $stage 'Assets') | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'AppxManifest.xml') $stage
Copy-Item (Join-Path $PSScriptRoot 'Assets\*.png') (Join-Path $stage 'Assets')

# /nv: the manifest references files (7zFM.exe, 7-zip.dll) that live in the external location
& $makeappx pack /o /nv /d $stage /p $msix
if ($LASTEXITCODE) { throw "makeappx failed ($LASTEXITCODE)" }
& $signtool sign /q /fd SHA256 /s My /n ($subject -replace '^CN=') $msix
if ($LASTEXITCODE) { throw "signtool failed ($LASTEXITCODE)" }
Write-Host "Packed $msix"
