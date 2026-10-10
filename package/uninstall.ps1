# Unregisters the Windows 11 context menu. The install folder is left in place.
$pkg = Get-AppxPackage -Name '7-Zip.Fork.ContextMenu'
if ($pkg) { Remove-AppxPackage $pkg.PackageFullName; Write-Host 'Context menu unregistered.' }
else { Write-Host 'Not registered.' }
