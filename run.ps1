# run.ps1 — launch ArmorDetector (auto-build if needed)
$root = $PSScriptRoot
$exe = "$root\build\armor_detector.exe"
if (-not (Test-Path $exe)) {
    Write-Host "Not built yet, running build.ps1..." -ForegroundColor Yellow
    & "$root\build.ps1"
}
$env:PATH = "$root\build;C:\msys64\mingw64\bin;$env:PATH"
& $exe @args
