# One-time setup: creates a self-signed code-signing certificate for the sparse package
# and trusts it on this PC. Run from an elevated (Administrator) PowerShell,
# because trusting the certificate writes to the LocalMachine\TrustedPeople store.

$ErrorActionPreference = 'Stop'
$subject = 'CN=7-Zip Fork Dev'   # must match Publisher in AppxManifest.xml
$outDir = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $outDir | Out-Null

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject } | Select-Object -First 1
if (-not $cert) {
  $cert = New-SelfSignedCertificate -Type Custom -Subject $subject `
    -KeyUsage DigitalSignature -FriendlyName '7-Zip Fork Dev (sparse package signing)' `
    -CertStoreLocation Cert:\CurrentUser\My `
    -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
  Write-Host "Created certificate $($cert.Thumbprint)"
} else {
  Write-Host "Using existing certificate $($cert.Thumbprint)"
}

$cerPath = Join-Path $outDir '7zip-fork-dev.cer'
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
  [Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
  Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
  Write-Host 'Certificate trusted (LocalMachine\TrustedPeople).'
} else {
  Write-Warning 'Not elevated: certificate created but NOT trusted yet. Re-run this script as Administrator.'
}
