# Builds Ditherista Press on Windows with the toolchain from setup-toolchain.ps1.
#   .\scripts\build.ps1            -> libdither (if missing) + the app, into dist\ditherista\ditherista.exe
#   .\scripts\build.ps1 -Tests     -> also builds and runs the unit tests (tests\run_tests.ps1)
param([switch]$Tests, [string]$QtRoot = "C:\Qt", [string]$QtVersion = "6.9.3")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
# MinGW and Qt first. Git's sh.exe must NOT be on PATH: with it, make runs the Makefile's
# `SET PATH=... && gcc` recipes through sh instead of cmd, and they fail.
$env:PATH = "$QtRoot\Tools\mingw1310_64\bin;$QtRoot\$QtVersion\mingw_64\bin;" +
            (($env:PATH -split ';' | Where-Object { $_ -notmatch 'Git\\usr\\bin|Git\\bin' }) -join ';')
Push-Location $root
try {
    if (-not (Test-Path "libdither\dist\libdither.dll")) {
        Push-Location libdither
        make libdither
        if ($LASTEXITCODE -ne 0) { throw "libdither build failed" }
        Pop-Location
    }
    # a running ditherista.exe locks the file: the compile succeeds but the final copy fails
    if (Get-Process ditherista -ErrorAction SilentlyContinue) { Write-Warning "Close Ditherista Press first: dist\ditherista\ditherista.exe is in use." }
    make app
    $code = $LASTEXITCODE
    # the build regenerates the translation source; never commit that noise
    git checkout -- src/app/application_en_US.ts 2>$null
    if ($code -ne 0) { throw "app build failed" }
    if ($Tests) { & "$root\tests\run_tests.ps1" }
} finally {
    Pop-Location
}
