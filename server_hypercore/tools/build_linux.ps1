$ErrorActionPreference = "Stop"

if (-not $env:ZIG_GLOBAL_CACHE_DIR -and (Test-Path "D:\环境\C++ 编译器")) {
    $env:ZIG_GLOBAL_CACHE_DIR = "D:\环境\C++ 编译器\zig-cache"
    $env:ZIG_LOCAL_CACHE_DIR  = "D:\环境\C++ 编译器\zig-local-cache"
}
$zig = $env:ZIG_EXE
if (-not $zig) {
    $cmd = Get-Command zig -ErrorAction SilentlyContinue
    if ($cmd) { $zig = $cmd.Source }
    elseif (Test-Path "D:\环境\C++ 编译器\ziglang\zig.exe") { $zig = "D:\环境\C++ 编译器\ziglang\zig.exe" }
    else { throw "zig compiler not found. Set ZIG_EXE or add zig to PATH." }
}

$root   = (Resolve-Path "$PSScriptRoot\..").Path
$np     = "$root\native_plugin"
$lnxDir = "$np\linux"
$ld     = "$root\third_party\libdeflate-1.23"
$mi     = "$root\third_party\mimalloc-2.1.7"
$obj    = "$lnxDir\obj"
New-Item -ItemType Directory -Force -Path $obj | Out-Null

$target = "x86_64-linux-gnu.2.17"
$commonC = @("-target", $target, "-O3", "-march=x86_64", "-fPIC", "-fvisibility=hidden", "-DNDEBUG")

Write-Host "[1/3] Cross-compiling libdeflate-1.23 for Linux x86_64 (glibc 2.17)..."
$ldFiles = @(
    @{ src = "$ld\lib\utils.c";              out = "$obj\ld_utils.o" },
    @{ src = "$ld\lib\x86\cpu_features.c";   out = "$obj\ld_cpu.o" },
    @{ src = "$ld\lib\adler32.c";            out = "$obj\ld_adler32.o" },
    @{ src = "$ld\lib\deflate_decompress.c"; out = "$obj\ld_decomp.o" },
    @{ src = "$ld\lib\zlib_decompress.c";    out = "$obj\ld_zdecomp.o" },
    @{ src = "$ld\lib\deflate_compress.c";   out = "$obj\ld_comp.o" },
    @{ src = "$ld\lib\zlib_compress.c";      out = "$obj\ld_zcomp.o" }
)
foreach ($f in $ldFiles) {
    & $zig cc @commonC "-DLIBDEFLATE_ASSEMBLER_DOES_NOT_SUPPORT_AVX512VNNI" "-I$ld" "-I$ld\lib" -c $f.src -o $f.out
    if ($LASTEXITCODE -ne 0) { throw "Failed compiling $($f.src)" }
}

Write-Host "[2/3] Cross-compiling hermetic mimalloc-2.1.7 (MI_OVERRIDE=0, MI_SKIP_COLLECT_ON_EXIT=1)..."
$miObj = "$obj\mimalloc_static.o"
& $zig cc @commonC "-I$mi\include" "-DMI_STATIC_LIB=1" "-DMI_SKIP_COLLECT_ON_EXIT=1" "-DMI_OVERRIDE=0" -c "$mi\src\static.c" -o $miObj
if ($LASTEXITCODE -ne 0) { throw "Failed compiling mimalloc static.c" }

Write-Host "[3/3] Cross-compiling & linking libhypercore.so (DST HyperCore Server Linux Engine v1.6.0-beta1)..."
$mainObj = "$obj\hypercore_linux.o"
& $zig c++ -target $target -std=c++20 -O3 -march=x86_64 -fPIC -fvisibility=hidden -DNDEBUG `
    -Wall -Wextra -Wno-unused-parameter `
    "-I$np" "-I$lnxDir" "-I$ld" "-I$mi\include" `
    -fno-exceptions -fno-rtti `
    -c "$lnxDir\hypercore_linux.cpp" -o $mainObj
if ($LASTEXITCODE -ne 0) { throw "Failed compiling hypercore_linux.cpp" }

$outSo = "$lnxDir\libhypercore.so"
$mapFile = $lnxDir.Replace('\', '/') + "/exports.map"
& $zig c++ -target $target -shared -O3 -s -fPIC `
    -fno-exceptions -fno-rtti `
    "-Wl,--version-script=$mapFile" `
    "-Wl,-Bsymbolic" `
    -o $outSo `
    $mainObj $miObj `
    "$obj\ld_utils.o" "$obj\ld_cpu.o" "$obj\ld_adler32.o" "$obj\ld_decomp.o" "$obj\ld_zdecomp.o" "$obj\ld_comp.o" "$obj\ld_zcomp.o" `
    -lpthread -ldl -lm
if ($LASTEXITCODE -ne 0) { throw "Failed linking libhypercore.so" }

$info = Get-Item $outSo
Write-Host "SUCCESS: Built $($info.FullName) ($($info.Length) bytes)"
