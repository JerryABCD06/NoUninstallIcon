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

$signtool = @(
    'D:\Windows Kits\10\bin',
    'C:\Program Files (x86)\Windows Kits\10\bin'
) | Where-Object { Test-Path $_ } | ForEach-Object {
    Get-ChildItem $_ -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue
} | Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1

if (-not $signtool) {
    Write-Host "[sign] signtool.exe not found, skipping"
    exit 0
}

$password = (Get-Content $passwordFile -Raw).Trim()
& $signtool.FullName sign /f $pfx /p $password /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $Path
if ($LASTEXITCODE -ne 0) {
    Write-Host "[sign] signing failed; the artefact stays unsigned"
}
exit 0
