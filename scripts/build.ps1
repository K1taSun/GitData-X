# Ładuje środowisko MSVC, a następnie konfiguruje, buduje i testuje projekt (Windows).
# Bez 'Stop': vcvars64 wypisuje na stderr nieszkodliwe ostrzeżenia, a błędy sprawdzamy przez $LASTEXITCODE.
# Szukamy najnowszej instalacji Visual Studio zgodnej z binariami zbudowanymi przez vcpkg.
$vcvarsCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) {
    Write-Error "Nie znaleziono vcvars64.bat w zadnej ze standardowych lokalizacji Visual Studio."
    exit 1
}
$envDump = Join-Path $env:TEMP 'gitdatax_vcvars.bat'
Set-Content -Path $envDump -Value "@echo off`r`ncall `"$vcvars`" >nul`r`nset" -Encoding ascii
& $env:ComSpec /c $envDump | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}
# vcvars64 ustawia VCPKG_ROOT na vcpkg dołączony do Visual Studio - nadpisujemy go własnym.
$env:VCPKG_ROOT = if ($env:GITDATAX_VCPKG_ROOT) { $env:GITDATAX_VCPKG_ROOT } else { 'C:\vcpkg' }
$env:Path += ";$env:LOCALAPPDATA\Microsoft\WinGet\Links"

Set-Location (Split-Path $PSScriptRoot -Parent)
cmake --preset default
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir build --output-on-failure
exit $LASTEXITCODE
