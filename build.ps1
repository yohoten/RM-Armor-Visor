# build.ps1 — build ArmorDetector with MSYS2 MSVCRT GCC
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$root = $PSScriptRoot
cmake -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -B "$root/build" -S "$root"
if ($LASTEXITCODE) { throw "CMake failed" }
mingw32-make -C "$root/build" -j4
if ($LASTEXITCODE -eq 0) { Write-Host "BUILD OK" -ForegroundColor Green }
