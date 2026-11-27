$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$inc = @("-I$root/src", "-I$root/host", "-I$root/lib/miniz", "-I$root/lib/tinyxml2", "-I$root/lib/stb_image")
$defs = @("-DHOST_GUI=1", '-DBOOKS_DIR=\"sd/books\"', '-DST_DIR=\"sd/books/.state\"')
$cfiles = @(
    "$root/src/util.c", "$root/src/htmltext.c", "$root/src/paginate.c",
    "$root/src/state.c", "$root/src/library.c", "$root/src/battery.c",
    "$root/src/app.c", "$root/src/boot_image.c",
    "$root/host/bridge_gui.c",
    "$root/host/host_main.c", "$root/lib/miniz/miniz.c"
)
$objs = @()
foreach ($f in $cfiles) {
    $o = Join-Path $root "host\host_$([IO.Path]::GetFileName($f)).o"
    & gcc -O2 -std=gnu99 @inc @defs -c $f -o $o
    if ($LASTEXITCODE -ne 0) { throw "gcc failed: $f" }
    $objs += $o
}
foreach ($f in @("$root/src/epub.cpp", "$root/lib/tinyxml2/tinyxml2.cpp")) {
    $o = Join-Path $root "host\host_$([IO.Path]::GetFileName($f)).o"
    & g++ -O2 -std=gnu++17 @inc @defs -c $f -o $o
    if ($LASTEXITCODE -ne 0) { throw "g++ failed: $f" }
    $objs += $o
}
& g++ -O2 -o (Join-Path $root "host\host_gui.exe") @objs
if ($LASTEXITCODE -ne 0) { throw "link failed" }
Write-Output "HOST BUILD OK"
