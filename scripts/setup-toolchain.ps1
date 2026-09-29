# Installs the Windows toolchain this fork is built with, without a Qt account:
#   Qt 6.9.3 (MinGW 64-bit) + qtimageformats (TIFF reading) + MinGW 13.1, all under C:\Qt, via aqtinstall.
# Requires Python 3 (the `py` launcher). Takes a few minutes and about 3 GB.
param([string]$QtRoot = "C:\Qt", [string]$QtVersion = "6.9.3")
$ErrorActionPreference = "Stop"
py -m pip install --user --quiet aqtinstall
py -m aqt install-qt windows desktop $QtVersion win64_mingw -m qtimageformats -O $QtRoot
py -m aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 -O $QtRoot
# the Makefiles call `make` by that name; MinGW ships it as mingw32-make
$bin = Join-Path $QtRoot "Tools\mingw1310_64\bin"
Copy-Item (Join-Path $bin "mingw32-make.exe") (Join-Path $bin "make.exe") -Force
"Toolchain ready: $QtRoot\$QtVersion\mingw_64 and $bin"
