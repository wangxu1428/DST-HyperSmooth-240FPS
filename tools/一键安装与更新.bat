@echo off
chcp 65001 >nul
title DST HyperShade (HyperSmooth) / HyperCore Installer
set "HS_SELF=%~f0"
set "HS_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$c=[IO.File]::ReadAllText($env:HS_SELF,[Text.Encoding]::UTF8); $m='#===PS_'+'START==='; $i=$c.IndexOf($m); if($i -lt 0){exit 1}; &([scriptblock]::Create($c.Substring($i)))"
exit /b %ERRORLEVEL%
#===PS_START===
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$hsLang = $env:HS_LANG
if ($hsLang) {
    $script:IsZh = $hsLang -like "zh*"
} else {
    $script:IsZh = ((Get-UICulture).Name -like "zh*") -or ([Globalization.CultureInfo]::CurrentCulture.Name -like "zh*")
}
function Tr([string]$Zh, [string]$En) {
    if ($script:IsZh) { return $Zh }
    return $En
}

function Write-Banner {
    Write-Host ""
    Write-Host "==================================================================" -ForegroundColor Cyan
    Write-Host (Tr "  饥荒联机版 (DST) HyperShade（原 HyperSmooth）& HyperCore 一键安装工具" "  DST HyperShade (HyperSmooth) & HyperCore One-Click Installer") -ForegroundColor Yellow
    Write-Host (Tr "  纯开源透明脚本 · 自动识别 Steam/WeGame 路径 · SHA256 逐字节校验" "  Open-Source Transparent Script | Auto-Detect Steam/WeGame | SHA256 Verified") -ForegroundColor Gray
    Write-Host "==================================================================" -ForegroundColor Cyan
    Write-Host ""
}

function Get-FileSha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Find-BundledPayload([string]$BaseDir) {
    $items = @()
    $searchDirs = @(
        (Join-Path $BaseDir "bin64"),
        (Join-Path $BaseDir "native_plugin"),
        (Join-Path (Split-Path $BaseDir -Parent) "native_plugin"),
        $BaseDir
    ) | Select-Object -Unique

    $specs = @(
        @{ Name = "dinput8.dll";       Label = (Tr "HyperSmooth 客户端原生引擎 (240Hz/低延迟/极速加载)" "HyperSmooth Client Native Engine (240Hz/Low-Latency/FastLoad)"); KeepExisting = $false },
        @{ Name = "iphlpapi.dll";      Label = (Tr "HyperCore 服务端原生引擎 (60Hz/寻路/存档/极速加载)" "HyperCore Server Native Engine (60Hz/AStar/AsyncSave/FastLoad)"); KeepExisting = $false },
        @{ Name = "dst_hypercore.ini"; Label = (Tr "HyperCore 服务端默认配置文件 (已存在则保留用户设置)" "HyperCore Server Config (preserves existing user settings)"); KeepExisting = $true }
    )
    foreach ($sp in $specs) {
        foreach ($d in $searchDirs) {
            if (-not $d) { continue }
            $cand = Join-Path $d $sp.Name
            if (Test-Path -LiteralPath $cand -PathType Leaf) {
                $fi = Get-Item -LiteralPath $cand
                $items += [PSCustomObject]@{
                    Name         = $sp.Name
                    Label        = $sp.Label
                    SourcePath   = $fi.FullName
                    Size         = $fi.Length
                    Sha256       = Get-FileSha256 $fi.FullName
                    KeepExisting = $sp.KeepExisting
                }
                break
            }
        }
    }
    return $items
}

function Test-ValidDstBin64([string]$Dir) {
    if (-not $Dir -or -not (Test-Path -LiteralPath $Dir -PathType Container)) { return $false }
    $c1 = Join-Path $Dir "dontstarve_steam_x64.exe"
    $c2 = Join-Path $Dir "dontstarve_dedicated_server_nullrenderer_x64.exe"
    $c3 = Join-Path $Dir "dontstarve_rail_x64.exe"
    return (Test-Path -LiteralPath $c1) -or (Test-Path -LiteralPath $c2) -or (Test-Path -LiteralPath $c3)
}

function Discover-DstBin64Dirs {
    $found = New-Object System.Collections.Generic.List[string]
    $addIfValid = {
        param([string]$cand)
        if (-not $cand) { return }
        $norm = $cand.Trim().Trim('"')
        if ((Test-Path -LiteralPath (Join-Path $norm "bin64")) -and (Test-ValidDstBin64 (Join-Path $norm "bin64"))) {
            $norm = Join-Path $norm "bin64"
        }
        if (Test-ValidDstBin64 $norm) {
            $full = (Get-Item -LiteralPath $norm).FullName
            if (-not ($found | Where-Object { $_.Equals($full, [System.StringComparison]::OrdinalIgnoreCase) })) {
                $found.Add($full)
            }
        }
    }

    # 1. Steam Registry + libraryfolders.vdf multi-drive scan
    $steamRoots = @()
    foreach ($regPath in @(
        "HKCU:\Software\Valve\Steam",
        "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam",
        "HKLM:\SOFTWARE\Valve\Steam"
    )) {
        try {
            $props = Get-ItemProperty -LiteralPath $regPath -ErrorAction SilentlyContinue
            if ($props.SteamPath)   { $steamRoots += ($props.SteamPath -replace '/', '\') }
            if ($props.InstallPath) { $steamRoots += ($props.InstallPath -replace '/', '\') }
        } catch {}
    }

    $steamLibs = New-Object System.Collections.Generic.List[string]
    foreach ($sr in ($steamRoots | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $sr) {
            $steamLibs.Add($sr)
            $vdf = Join-Path $sr "steamapps\libraryfolders.vdf"
            if (Test-Path -LiteralPath $vdf) {
                $txt = Get-Content -LiteralPath $vdf -Raw -ErrorAction SilentlyContinue
                foreach ($m in [regex]::Matches($txt, '"path"\s+"([^"]+)"')) {
                    $p = $m.Groups[1].Value -replace '\\\\', '\'
                    if (Test-Path -LiteralPath $p) { $steamLibs.Add($p) }
                }
            }
        }
    }

    foreach ($lib in ($steamLibs | Select-Object -Unique)) {
        & $addIfValid (Join-Path $lib "steamapps\common\Don't Starve Together\bin64")
        & $addIfValid (Join-Path $lib "steamapps\common\Don't Starve Together Dedicated Server\bin64")
    }

    # 2. Common Steam / WeGame fallback roots across local drives
    $drives = Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -match '^[A-Z]:\\$' }
    foreach ($dr in $drives) {
        $r = $dr.Root
        foreach ($sub in @(
            "Steam\steamapps\common\Don't Starve Together\bin64",
            "SteamLibrary\steamapps\common\Don't Starve Together\bin64",
            "Program Files (x86)\Steam\steamapps\common\Don't Starve Together\bin64",
            "WeGameApps\rail_apps\Don't Starve Together\bin64",
            "WeGameApps\rail_apps\饥荒：联机版(2000013)\bin64"
        )) {
            & $addIfValid (Join-Path $r $sub)
        }
    }

    return $found.ToArray()
}

function Get-RunningDstProcesses {
    return @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -like "dontstarve*"
    })
}

function Show-InstallStatus([string]$Bin64Dir, $Payloads) {
    Write-Host ((Tr "[目标目录] " "[Target Dir] ") + $Bin64Dir) -ForegroundColor Cyan
    if (Test-Path -LiteralPath (Join-Path $Bin64Dir "Winmm.dll")) {
        Write-Host (Tr "[兼容检测] 检测到 bin64\Winmm.dll (DontStarveLuaJIT2)：新版 dinput8.dll 与 iphlpapi.dll 已内置自动退让全部 Lua 5.1 ABI 钩子 (mimalloc/GC/LuaBridge)，纯 C++ 优化照常生效！" "[Compat] Detected bin64\Winmm.dll (DontStarveLuaJIT2): dinput8.dll & iphlpapi.dll automatically yield all PUC Lua 5.1 ABI hooks (mimalloc/GC/LuaBridge) while keeping pure C++ optimizations active!") -ForegroundColor Green
    }
    Write-Host ""
    foreach ($it in $Payloads) {
        $dst = Join-Path $Bin64Dir $it.Name
        if (Test-Path -LiteralPath $dst) {
            $dstFi = Get-Item -LiteralPath $dst
            $dstHash = Get-FileSha256 $dst
            if ($dstHash -eq $it.Sha256) {
                Write-Host (Tr ("  [最新] {0,-18} | 已同步 ({1:N0} B | {2}...)" -f $it.Name, $dstFi.Length, $dstHash.Substring(0, 12)) ("  [UP-TO-DATE] {0,-18} | Synced ({1:N0} B | {2}...)" -f $it.Name, $dstFi.Length, $dstHash.Substring(0, 12))) -ForegroundColor Green
            } elseif ($it.KeepExisting) {
                Write-Host (Tr ("  [保留] {0,-18} | 已有自定义配置 ({1:N0} B，更新时不会覆盖)" -f $it.Name, $dstFi.Length) ("  [KEEP]       {0,-18} | Custom config kept ({1:N0} B, preserved on update)" -f $it.Name, $dstFi.Length)) -ForegroundColor Cyan
            } else {
                Write-Host (Tr ("  [旧版] {0,-18} | 当前 {1:N0} B ({2}...) -> 包内新版 {3:N0} B ({4}...)" -f $it.Name, $dstFi.Length, $dstHash.Substring(0, 8), $it.Size, $it.Sha256.Substring(0, 8)) ("  [OUTDATED]   {0,-18} | Installed {1:N0} B ({2}...) -> New {3:N0} B ({4}...)" -f $it.Name, $dstFi.Length, $dstHash.Substring(0, 8), $it.Size, $it.Sha256.Substring(0, 8))) -ForegroundColor Yellow
            }
        } else {
            Write-Host (Tr ("  [未装] {0,-18} | 包内可用 ({1:N0} B) - {2}" -f $it.Name, $it.Size, $it.Label) ("  [MISSING]    {0,-18} | Available ({1:N0} B) - {2}" -f $it.Name, $it.Size, $it.Label)) -ForegroundColor Gray
        }
    }
    Write-Host ""
}

function Invoke-InstallUpdate([string]$Bin64Dir, $Payloads, [bool]$NonInteractive) {
    $procs = Get-RunningDstProcesses
    if ($procs.Count -gt 0 -and -not $env:HS_TEST_DST_DIR) {
        Write-Host (Tr "[提示] 检测到饥荒进程正在运行，无法直接覆盖被占用的 DLL：" "[Notice] DST process is currently running; locked DLLs cannot be overwritten:") -ForegroundColor Yellow
        foreach ($p in $procs) { Write-Host ("  - {0} (PID={1})" -f $p.Name, $p.Id) -ForegroundColor Yellow }
        Write-Host (Tr "请先手动退出游戏（本工具绝不强杀游戏进程，防止丢档），退出后按回车重试..." "Please exit DST manually (this installer never force-kills processes), then press Enter to retry...") -ForegroundColor Yellow
        if ($NonInteractive) { throw "DST process running" }
        [void](Read-Host)
        $procs = Get-RunningDstProcesses
        if ($procs.Count -gt 0) {
            Write-Host (Tr "[中止] 游戏仍在运行，已取消本次覆盖。" "[Aborted] DST is still running; cancelled.") -ForegroundColor Red
            return $false
        }
    }

    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $okAll = $true
    foreach ($it in $Payloads) {
        $dst = Join-Path $Bin64Dir $it.Name
        if ($it.KeepExisting -and (Test-Path -LiteralPath $dst)) {
            Write-Host (Tr ("  [保留] {0} 已存在，保留您的自定义配置。" -f $it.Name) ("  [KEEP]   {0} already exists; keeping your custom settings." -f $it.Name)) -ForegroundColor Cyan
            continue
        }
        if (Test-Path -LiteralPath $dst) {
            $curHash = Get-FileSha256 $dst
            if ($curHash -eq $it.Sha256) {
                Write-Host (Tr ("  [跳过] {0} 已经是最新版本 (SHA256 一致)。" -f $it.Name) ("  [SKIP]   {0} is already up-to-date (SHA256 matches)." -f $it.Name)) -ForegroundColor Green
                continue
            }
            $bak = "$dst.bak_$stamp"
            Copy-Item -LiteralPath $dst -Destination $bak -Force
            Write-Host (Tr ("  [备份] 旧版 {0} 已备份至 {1}" -f $it.Name, (Split-Path $bak -Leaf)) ("  [BACKUP] Old {0} backed up to {1}" -f $it.Name, (Split-Path $bak -Leaf))) -ForegroundColor DarkGray
        }
        Copy-Item -LiteralPath $it.SourcePath -Destination $dst -Force
        $newHash = Get-FileSha256 $dst
        if ($newHash -ne $it.Sha256) {
            Write-Host (Tr ("  [失败] {0} 写入后 SHA256 校验不一致！" -f $it.Name) ("  [FAILED] {0} SHA256 mismatch after copy!" -f $it.Name)) -ForegroundColor Red
            $okAll = $false
        } else {
            Write-Host (Tr ("  [成功] {0} 安装/更新完成 ({1:N0} B | SHA256={2}...)" -f $it.Name, $it.Size, $newHash.Substring(0, 16)) ("  [OK]     {0} installed/updated ({1:N0} B | SHA256={2}...)" -f $it.Name, $it.Size, $newHash.Substring(0, 16))) -ForegroundColor Green
        }
    }
    if ($okAll) {
        Write-Host ""
        Write-Host (Tr "全部文件安装与校验通过！现在可以直接启动《饥荒联机版》体验啦。" "All files installed & verified! You can launch Don't Starve Together now.") -ForegroundColor Green
        if (Test-Path -LiteralPath (Join-Path $Bin64Dir "Winmm.dll")) {
            Write-Host (Tr "温馨提示：检测到 LuaJIT2 (Winmm.dll)，新版 dinput8.dll 与 iphlpapi.dll 已内置全链路自动避让，可零冲突共存运行。" "Note: LuaJIT2 (Winmm.dll) detected; dinput8.dll & iphlpapi.dll automatically yield conflicting Lua 5.1 ABI hooks for crash-free coexistence.") -ForegroundColor Cyan
        }
    }
    return $okAll
}

function Invoke-CleanUninstall([string]$Bin64Dir, [bool]$NonInteractive) {
    $procs = Get-RunningDstProcesses
    if ($procs.Count -gt 0 -and -not $env:HS_TEST_DST_DIR) {
        Write-Host (Tr "[提示] 请先手动退出饥荒游戏再执行卸载，避免 DLL 占用报错。" "[Notice] Please exit DST before uninstalling so DLL files are not locked.") -ForegroundColor Yellow
        return $false
    }
    $removed = 0
    foreach ($fn in @("dinput8.dll", "iphlpapi.dll")) {
        $p = Join-Path $Bin64Dir $fn
        if (Test-Path -LiteralPath $p) {
            Remove-Item -LiteralPath $p -Force
            Write-Host (Tr ("  [已移除] {0}（原版游戏文件与存档 100% 完好未动）" -f $fn) ("  [REMOVED] {0} (vanilla files and saves 100% untouched)" -f $fn)) -ForegroundColor Green
            $removed++
        }
    }
    if ($removed -eq 0) {
        Write-Host (Tr "  [提示] 目录内未发现已安装的 dinput8.dll / iphlpapi.dll，当前已是纯净原版状态。" "  [INFO] No dinput8.dll / iphlpapi.dll found; directory is already clean.") -ForegroundColor Cyan
    } else {
        Write-Host ""
        Write-Host (Tr "卸载完成！游戏已 100% 恢复原版状态。" "Uninstall complete! Game restored to clean vanilla state.") -ForegroundColor Green
    }
    return $true
}

# === Main Entry ===
Write-Banner
$baseDir = ($env:HS_DIR).TrimEnd('\')
$payloads = @(Find-BundledPayload $baseDir)
if ($payloads.Count -eq 0) {
    Write-Host (Tr "[错误] 未在当前目录或 bin64/、native_plugin/ 中找到 dinput8.dll 或 iphlpapi.dll！" "[ERROR] Neither dinput8.dll nor iphlpapi.dll found in current folder or bin64/ / native_plugin/!") -ForegroundColor Red
    Write-Host (Tr "请确保将整个压缩包解压后再双击运行本工具（不要在压缩包预览窗口里直接双击）。" "Please extract the entire archive before running this script (do not run from inside a zip preview).") -ForegroundColor Yellow
    if (-not $env:HS_TEST_ACTION) { [void](Read-Host (Tr "按回车退出" "Press Enter to exit")) }
    exit 1
}

$targetDir = $null
$isTest = [bool]$env:HS_TEST_ACTION
if ($env:HS_TEST_DST_DIR) {
    $targetDir = $env:HS_TEST_DST_DIR
} else {
    $cands = @(Discover-DstBin64Dirs)
    if ($cands.Count -eq 1) {
        $targetDir = $cands[0]
        Write-Host ((Tr "[自动识别] 已找到饥荒目录：" "[Detected] Found DST bin64 directory: ") + $targetDir) -ForegroundColor Green
    } elseif ($cands.Count -gt 1) {
        Write-Host (Tr "[自动识别] 发现多个饥荒目录，请选择序号：" "[Detected] Multiple DST directories found; select a number:") -ForegroundColor Cyan
        for ($i = 0; $i -lt $cands.Count; $i++) {
            Write-Host ("  [{0}] {1}" -f ($i + 1), $cands[$i])
        }
        $sel = Read-Host (Tr "请输入序号 (默认 1)" "Enter choice (default 1)")
        $idx = 0
        if ([int]::TryParse($sel, [ref]$idx) -and $idx -ge 1 -and $idx -le $cands.Count) {
            $targetDir = $cands[$idx - 1]
        } else {
            $targetDir = $cands[0]
        }
    } else {
        Write-Host (Tr "[未自动找到] 请将游戏目录或 bin64 文件夹直接拖入本窗口后按回车：" "[Not Found] Drag & drop your DST install or bin64 folder here and press Enter:") -ForegroundColor Yellow
        $manual = (Read-Host (Tr "目录路径" "Folder path")).Trim().Trim('"')
        if (Test-Path -LiteralPath (Join-Path $manual "bin64")) { $manual = Join-Path $manual "bin64" }
        if (-not (Test-ValidDstBin64 $manual)) {
            Write-Host (Tr "[错误] 该目录下未找到饥荒主程序 (dontstarve_steam_x64.exe / dedicated_server)，已退出。" "[ERROR] No DST executable found in that directory. Exiting.") -ForegroundColor Red
            [void](Read-Host (Tr "按回车退出" "Press Enter to exit"))
            exit 1
        }
        $targetDir = (Get-Item -LiteralPath $manual).FullName
    }
}

if ($isTest) {
    $act = $env:HS_TEST_ACTION.ToLowerInvariant()
    Show-InstallStatus $targetDir $payloads
    if ($act -eq "install") {
        $ok = Invoke-InstallUpdate $targetDir $payloads $true
        exit ($(if ($ok) { 0 } else { 1 }))
    } elseif ($act -eq "uninstall") {
        $ok = Invoke-CleanUninstall $targetDir $true
        exit ($(if ($ok) { 0 } else { 1 }))
    } else {
        exit 0
    }
}

while ($true) {
    Show-InstallStatus $targetDir $payloads
    Write-Host (Tr "请选择操作：" "Select an action:") -ForegroundColor White
    Write-Host (Tr "  [1] 一键安装 / 覆盖更新到最新配套版本 (推荐)" "  [1] Install / Update to latest version (Recommended)") -ForegroundColor Green
    Write-Host (Tr "  [2] 一键卸载 / 恢复纯净原版 (仅移除本插件 DLL)" "  [2] Clean Uninstall / Restore Vanilla (removes plugin DLLs only)") -ForegroundColor Yellow
    Write-Host (Tr "  [3] 刷新检测当前安装状态" "  [3] Refresh Status") -ForegroundColor Cyan
    Write-Host (Tr "  [0] 退出" "  [0] Exit") -ForegroundColor Gray
    Write-Host ""
    $choice = (Read-Host (Tr "请输入数字 [默认 1]" "Enter number [default 1]")).Trim()
    if ($choice -eq "" -or $choice -eq "1") {
        [void](Invoke-InstallUpdate $targetDir $payloads $false)
        Write-Host ""
        [void](Read-Host (Tr "操作完成，按回车返回菜单" "Done. Press Enter to return to menu"))
    } elseif ($choice -eq "2") {
        [void](Invoke-CleanUninstall $targetDir $false)
        Write-Host ""
        [void](Read-Host (Tr "操作完成，按回车返回菜单" "Done. Press Enter to return to menu"))
    } elseif ($choice -eq "3") {
        continue
    } elseif ($choice -eq "0") {
        break
    }
}
exit 0
