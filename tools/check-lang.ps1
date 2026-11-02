# Compares every language file against en.lang and reports missing or extra keys.
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot '..\res\lang'
$en = Get-Content (Join-Path $dir 'en.lang') -Encoding UTF8 |
      Where-Object { $_ -match '^[A-Za-z0-9_.]+=' } |
      ForEach-Object { ($_ -split '=', 2)[0] }

$bad = 0
Get-ChildItem $dir -Filter *.lang | Sort-Object Name | ForEach-Object {
    $keys = Get-Content $_.FullName -Encoding UTF8 |
            Where-Object { $_ -match '^[A-Za-z0-9_.]+=' } |
            ForEach-Object { ($_ -split '=', 2)[0] }
    $missing = @($en | Where-Object { $keys -notcontains $_ })
    $extra = @($keys | Where-Object { $en -notcontains $_ })
    if ($missing.Count -or $extra.Count) {
        $bad++
        Write-Host ("[lang] {0}: {1} keys" -f $_.Name, $keys.Count)
        if ($missing.Count) { Write-Host ("[lang]   missing: " + ($missing -join ', ')) }
        if ($extra.Count)   { Write-Host ("[lang]   extra:   " + ($extra -join ', ')) }
    }
}
if ($bad -eq 0) { Write-Host ("[lang] ok: {0} languages in sync" -f (Get-ChildItem $dir -Filter *.lang).Count) }
exit 0