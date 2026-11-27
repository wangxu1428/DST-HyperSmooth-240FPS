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

$root = (Resolve-Path "$PSScriptRoot\..").Path
$np   = "$root\native_plugin"
$ld   = "$root\third_party\libdeflate-1.23"
$mi   = "$root\third_party\mimalloc-2.1.7"
$obj  = "$np\obj"
New-Item -ItemType Directory -Force -Path $obj | Out-Null

$target = "x86_64-windows-gnu"
$commonC = @("-target", $target, "-O3", "-march=x86_64", "-DNDEBUG")

Write-Host "[1/3] Compiling libdeflate-1.23 (auto-CPUID AVX-512/AVX2/SSE2 decompressor + zlib compressor)..."
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
    if (-not (Test-Path $f.out)) {
        & $zig cc @commonC "-DLIBDEFLATE_ASSEMBLER_DOES_NOT_SUPPORT_AVX512VNNI" "-I$ld" "-I$ld\lib" -c $f.src -o $f.out
        if ($LASTEXITCODE -ne 0) { throw "Failed compiling $($f.src)" }
    }
}

Write-Host "[2/3] Compiling mimalloc-2.1.7 (hermetic lockless static Lua heap)..."
$miObj = "$obj\mimalloc_static.o"
if (-not (Test-Path $miObj)) {
    & $zig cc @commonC "-I$mi\include" "-DMI_STATIC_LIB=1" -c "$mi\src\static.c" -o $miObj
    if ($LASTEXITCODE -ne 0) { throw "Failed compiling mimalloc static.c" }
}

Write-Host "[3/3] Compiling & linking dinput8.dll (DST HyperSmooth Client Native Engine v2.9.7-beta1)..."
$mainObj = "$obj\dinput8.o"
& $zig c++ -target $target -std=c++20 -O3 -march=x86_64 -DNDEBUG `
    -Wall -Wextra -Wno-unused-parameter `
    "-I$np" "-I$ld" "-I$mi\include" `
    -fno-exceptions -fno-rtti `
    -c "$np\dinput8.cpp" -o $mainObj
if ($LASTEXITCODE -ne 0) { throw "Failed compiling dinput8.cpp" }

$outDll = "$np\dinput8.dll"
& $zig c++ -target $target -shared -O3 -s `
    -o $outDll `
    $mainObj $miObj "$obj\ld_utils.o" "$obj\ld_cpu.o" "$obj\ld_adler32.o" "$obj\ld_decomp.o" "$obj\ld_zdecomp.o" "$obj\ld_comp.o" "$obj\ld_zcomp.o" `
    "$np\dinput8.def" `
    -lwinmm -lshell32 -lole32 -ladvapi32 -lbcrypt -lpsapi
if ($LASTEXITCODE -ne 0) { throw "Failed linking dinput8.dll" }

$info = Get-Item $outDll
Write-Host "SUCCESS: Built $($info.FullName) ($($info.Length) bytes)"
