param([switch]$Sign)

# Builds the release artefacts. By default they stay unsigned: a self-signed
# binary shows the publisher "王云正" with no chain to a trusted root, which is no
# better than unsigned as far as SmartScreen is concerned. Pass -Sign only for a
# local test build.

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\NoUninstallIcon.exe'
$dist = Join-Path $root 'dist'

if (-not (Test-Path $exe)) {
    Write-Host '[package] build\NoUninstallIcon.exe is missing; run build.bat first'
    exit 1
}

$current = (Get-AuthenticodeSignature $exe).Status
if ($current -ne 'NotSigned' -and -not $Sign) {
    Write-Host "[package] the build is already signed ($current); rebuild with plain 'build.bat' for a release"
    exit 1
}

if ($Sign) {
    & (Join-Path $PSScriptRoot 'sign.ps1') -Path $exe
}

# Find the compiler: INNO_SETUP_DIR wins, then whatever the uninstall registry
# says, then the default location of a stock install.
$isccCandidates = @()
if ($env:INNO_SETUP_DIR) { $isccCandidates += (Join-Path $env:INNO_SETUP_DIR 'ISCC.exe') }
foreach ($uninstallRoot in @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
)) {
    Get-ChildItem $uninstallRoot -ErrorAction SilentlyContinue |
        Where-Object { $_.PSChildName -like 'Inno Setup*' } |
        ForEach-Object {
            $location = (Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue).InstallLocation
            if ($location) { $isccCandidates += (Join-Path $location 'ISCC.exe') }
        }
}
$isccCandidates += @(
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    Write-Host '[package] ISCC.exe not found. Install Inno Setup, or set INNO_SETUP_DIR to its folder.'
    exit 1
}

$setup = Join-Path $dist 'NoUninstallIcon-Setup-0.1.0.exe'
$zip = Join-Path $dist 'NoUninstallIcon-portable-0.1.0.zip'

& $iscc (Join-Path $root 'installer\setup.iss') | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host '[package] the installer failed to compile'
    exit 1
}

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $exe, (Join-Path $root 'Reset.cmd'), (Join-Path $root 'README.md'), (Join-Path $root 'LICENSE'), (Join-Path $root 'portable'), `
                 (Join-Path $root 'docs') `
                 -DestinationPath $zip -CompressionLevel Optimal

foreach ($artefact in @($exe, $setup)) {
    $status = (Get-AuthenticodeSignature $artefact).Status
    if ($status -ne 'NotSigned' -and -not $Sign) {
        Write-Host "[package] $artefact came out signed ($status)"
        exit 1
    }
}

$exeSize = [math]::Round((Get-Item $exe).Length / 1KB)
$setupSize = [math]::Round((Get-Item $setup).Length / 1KB)
$zipSize = [math]::Round((Get-Item $zip).Length / 1KB)
$state = if ($Sign) { 'signed (test build)' } else { 'unsigned' }
Write-Host "[package] ok ($state): exe ${exeSize} KB, setup ${setupSize} KB, zip ${zipSize} KB"
