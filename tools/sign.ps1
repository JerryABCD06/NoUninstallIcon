param([Parameter(Mandatory = $true)][string]$Path)

# Signs a build artefact with the local self-signed certificate, if one exists.
# A missing certificate or a blocked key store is not an error: the build keeps
# going and simply stays unsigned.

$root = Split-Path $PSScriptRoot -Parent
$pfx = Join-Path $root '.signing\NoUninstallIcon.pfx'
$passwordFile = Join-Path $root '.signing\password.txt'

if (-not (Test-Path $pfx) -or -not (Test-Path $passwordFile)) {
    Write-Host "[sign] no certificate in .signing, skipping"
    exit 0
}
if (-not (Test-Path $Path)) {
    Write-Host "[sign] $Path does not exist, skipping"
    exit 0
}

# SIGNTOOL wins; otherwise take the newest x64 tool from whatever Windows SDK is
# installed. Signing is a local convenience, so a missing tool is not an error.
$signtoolCandidates = @()
if ($env:SIGNTOOL) { $signtoolCandidates += $env:SIGNTOOL }
$kitsRoots = @()
foreach ($key in @(
    'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots'
)) {
    $root10 = (Get-ItemProperty $key -ErrorAction SilentlyContinue).KitsRoot10
    if ($root10) { $kitsRoots += (Join-Path $root10 'bin') }
}
$kitsRoots += (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin')
$kitsRoots += (Join-Path $env:ProgramFiles 'Windows Kits\10\bin')
foreach ($kitsRoot in $kitsRoots) {
    if (-not $kitsRoot -or -not (Test-Path $kitsRoot)) { continue }
    $signtoolCandidates += Get-ChildItem $kitsRoot -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -ExpandProperty FullName
}
$signtool = $signtoolCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $signtool) {
    Write-Host "[sign] signtool.exe not found, skipping (set SIGNTOOL to its path)"
    exit 0
}

$password = (Get-Content $passwordFile -Raw).Trim()
& $signtool sign /f $pfx /p $password /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $Path
if ($LASTEXITCODE -ne 0) {
    Write-Host "[sign] signing failed; the artefact stays unsigned"
}
exit 0
