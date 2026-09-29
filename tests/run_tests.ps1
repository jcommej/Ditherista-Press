# Builds and runs the unit tests (Windows, MinGW). Requires libdither to be built first:
#   cd libdither; make libdither
param(
    [string]$QtBin = "C:\Qt\6.9.3\mingw_64\bin",
    [string]$MingwBin = "C:\Qt\Tools\mingw1310_64\bin"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
# Git's sh.exe on PATH makes mingw32-make run recipes through sh instead of cmd
$env:PATH = "$MingwBin;$QtBin;$root\libdither\dist;" + (($env:PATH -split ';' | Where-Object { $_ -notmatch 'Git\\usr\\bin|Git\\bin' }) -join ';')
$build = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force $build | Out-Null
Push-Location $build
try {
    & qmake "$PSScriptRoot\tests.pro" CONFIG+=release
    if ($LASTEXITCODE -ne 0) { throw "qmake failed" }
    & mingw32-make -s
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    Remove-Item results_*.txt -ErrorAction SilentlyContinue
    # reports go through files: QtTest's stdout is not always visible from PowerShell hosts
    & ".\release\tests.exe"
    $code = $LASTEXITCODE
    Get-ChildItem results_*.txt | ForEach-Object { Get-Content $_ }
    exit $code
} finally {
    Pop-Location
}
