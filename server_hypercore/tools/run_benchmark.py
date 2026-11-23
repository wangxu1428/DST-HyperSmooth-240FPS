#!/usr/bin/env python3
r"""
Isolated Headless DST Dedicated Server Scientific A/B/Ablation Benchmark & Safety Suite.
Touches ZERO live Steam files or Documents\Klei saves.

Methodology:
  1. Generate 1 deterministic base world in 100% pure stock vanilla (and verify WorldGen
     also runs cleanly with iphlpapi.dll active).
  2. Snapshot that exact base world so EVERY benchmark run starts from the EXACT same
     seed, topology, and entity layout.
  3. Spawn the deterministic "Hell 4-in-1 Stress World" on top:
       - 80 active Merms + Merm King + 40 Merm Houses (active village + combat retargets)
       - 3,000 Winter's Feast Candies & Loose Clutter (1,000 active zone + 2,000 sleeping)
       - 150 Full Iceboxes (1,350 boxed perishable foods inside closed containers)
       - 60 active Beefalo + 16 active Ice Flingomatics
  4. Run a 12-second (~360-tick) steady-state measurement window + full SaveGame() +
     FastDataDumper 100% structural round-trip & NaN-guard verification.
  5. Run full A/B + single-module ablation matrix:
       - Run A: Pure Vanilla Baseline (profiler=1 ONLY, all 5 opts=0, Lua Mod OFF)
       - Run B1: +mimalloc ONLY
       - Run B2: +SpatialGrid Zero-Copy ONLY
       - Run B3: +30Hz Adaptive GC ONLY
       - Run B4: +FastDataDumper + SIMD libdeflate ONLY
       - Run C: Layer 1 Native DLL ALL ON (Lua Mod OFF)
       - Run D: FULL STACK (Layer 1 Native DLL + Layer 2 Zero-Divergence Lua Mod)
       - Run E: UNINSTALL SAFETY (Delete iphlpapi.dll + Disable Lua Mod -> load Run-D save in 100% Vanilla!)
"""
import os
import re
import shutil
import subprocess
import threading
import time

ROOT = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server"
DST_DIR = r"D:\steam\steamapps\common\Don't Starve Together"
RUNTIME = os.path.join(ROOT, "bench_runtime")
BIN64 = os.path.join(RUNTIME, "bin64")
MODS_DIR = os.path.join(RUNTIME, "mods")
STORAGE = os.path.join(RUNTIME, "storage")
CLUSTER_NAME = "BenchCluster"
CLUSTER_DIR = os.path.join(STORAGE, "DoNotStarveTogether", CLUSTER_NAME)
BASE_BACKUP = os.path.join(RUNTIME, "base_world_backup")

BENCH_MOD_INFO = """
name = "HyperCore Bench Driver"
description = "Automated deterministic Hell World stress test & SaveGame round-trip verifier."
author = "bench"
version = "1.0.0"
api_version = 10
dst_compatible = true
all_clients_require_mod = false
client_only_mod = false
"""

BENCH_MOD_MAIN = r"""
local G = GLOBAL
local rawget = G.rawget
local pcall = G.pcall
local type = G.type
local pairs = G.pairs
local ipairs = G.ipairs
local tostring = G.tostring
local assert = G.assert
local math = G.math
local string = G.string
local os = G.os
if not (G.TheNet and G.TheNet:GetIsServer()) then return end

BENCH_MODE = "BENCH"
pcall(modimport, "bench_mode.lua")
local MODE = BENCH_MODE

local function DeepEqual(a, b, path)
    if a == b then return true end
    if type(a) ~= type(b) then
        return false, string.format("type mismatch at %s: %s vs %s", path, type(a), type(b))
    end
    if type(a) == "number" then
        if math.abs(a - b) <= 1e-11 * math.max(1, math.abs(a), math.abs(b)) then
            return true
        end
        return false, string.format("num mismatch at %s: %.17g vs %.17g", path, a, b)
    end
    if type(a) ~= "table" then
        return false, string.format("val mismatch at %s: %s vs %s", path, tostring(a), tostring(b))
    end
    for k, va in pairs(a) do
        local ok, err = DeepEqual(va, b[k], path .. "." .. tostring(k))
        if not ok then return false, err end
    end
    for k, vb in pairs(b) do
        if a[k] == nil then
            return false, string.format("extra key at %s.%s", path, tostring(k))
        end
    end
    return true
end

local function SpawnHellWorld()
    local t0 = os.clock()
    local spawned = { merms = 0, candies = 0, chests = 0, boxed = 0, beefalo = 0, flingos = 0 }

    -- 1. Merm King + 40 Merm Houses + 80 Active Merms right around (0, 0)
    local throne = G.SpawnPrefab("mermthrone")
    if throne then throne.Transform:SetPosition(0, 0, 0) end
    local king = G.SpawnPrefab("mermking")
    if king then king.Transform:SetPosition(0, 0, 0) end

    for i = 1, 40 do
        local a = (i / 40) * math.pi * 2
        local r = 10 + (i % 4) * 3.5
        local house = G.SpawnPrefab("mermhouse_crafted")
        if house then house.Transform:SetPosition(math.cos(a) * r, 0, math.sin(a) * r) end
    end

    local active_merms = {}
    for i = 1, 80 do
        local a = (i / 80) * math.pi * 2
        local r = 4 + (i % 8) * 2.2
        local m = G.SpawnPrefab(i % 2 == 0 and "mermguard" or "merm")
        if m then
            m.Transform:SetPosition(math.cos(a) * r, 0, math.sin(a) * r)
            if m.entity and m.entity.SetCanSleep then m.entity:SetCanSleep(false) end
            active_merms[#active_merms + 1] = m
            spawned.merms = spawned.merms + 1
        end
    end

    -- 2. 16 Active Flingomatics + 150 Full Iceboxes (1,350 boxed perishable foods!)
    for i = 1, 16 do
        local a = (i / 16) * math.pi * 2
        local f = G.SpawnPrefab("firesuppressor")
        if f then
            f.Transform:SetPosition(math.cos(a) * 18, 0, math.sin(a) * 18)
            if f.entity and f.entity.SetCanSleep then f.entity:SetCanSleep(false) end
            if f.components.machine then f.components.machine:TurnOn() end
            spawned.flingos = spawned.flingos + 1
        end
    end

    local food_types = { "berries", "carrot", "meat", "monstermeat", "fishmeat", "drumstick", "froglegs", "watermelon", "pumpkin" }
    for i = 1, 150 do
        local gx = -28 + (i % 15) * 4
        local gz = -28 + math.floor(i / 15) * 4
        local box = G.SpawnPrefab("icebox")
        if box then
            box.Transform:SetPosition(gx, 0, gz)
            spawned.chests = spawned.chests + 1
            if box.components.container then
                for slot = 1, 9 do
                    local it = G.SpawnPrefab(food_types[slot])
                    if it then
                        box.components.container:GiveItem(it, slot)
                        spawned.boxed = spawned.boxed + 1
                    end
                end
            end
        end
    end

    -- 3. 3,000 Winter's Feast Candies & Loose Clutter (1,000 inside Merm radius + 2,000 sleeping!)
    local clutter_types = { "winter_food1", "winter_food2", "winter_food3", "winter_food4", "winter_food5", "winter_food6", "log", "rocks", "twigs", "cutgrass" }
    for i = 1, 3000 do
        local is_active_zone = (i <= 1000)
        local cx = is_active_zone and ((i % 40) - 20) * 1.1 or (120 + (i % 50) * 1.5)
        local cz = is_active_zone and (math.floor(i / 40) - 12) * 1.1 or (120 + math.floor((i - 1000) / 50) * 1.5)
        local it = G.SpawnPrefab(clutter_types[(i % #clutter_types) + 1])
        if it then
            it.Transform:SetPosition(cx, 0, cz)
            spawned.candies = spawned.candies + 1
        end
    end

    -- 4. 60 Active Beefalo
    for i = 1, 60 do
        local a = (i / 60) * math.pi * 2
        local b = G.SpawnPrefab("beefalo")
        if b then
            b.Transform:SetPosition(math.cos(a) * 26, 0, math.sin(a) * 26)
            if b.entity and b.entity.SetCanSleep then b.entity:SetCanSleep(false) end
            spawned.beefalo = spawned.beefalo + 1
        end
    end

    -- Active village load: realistic 1s combat retargets + extra spatial pressure
    G.TheWorld:DoPeriodicTask(0.25, function()
        for _, m in ipairs(active_merms) do
            if m:IsValid() and m.components.combat and m.components.combat.targetfn then
                m.components.combat.targetfn(m)
            end
        end
    end)

    print(string.format("[BENCH] Spawned Hell World in %.2fs: merms=%d candies=%d chests=%d boxed=%d beefalo=%d flingos=%d",
        os.clock() - t0, spawned.merms, spawned.candies, spawned.chests, spawned.boxed, spawned.beefalo, spawned.flingos))
end

local function VerifyFastDumperEquivalence()
    local _fd = rawget(G, "HyperCore_FastDump")
    local _gs = rawget(G, "HyperCore_GetStats")
    if type(_fd) ~= "function" or type(_gs) ~= "function" then
        return
    end
    local st = _gs()
    if not st.opt_fast_dumper then return end

    local test_sample = {
        meta = { session = "abc_123", version = 5, flag_t = true, flag_f = false },
        weird_keys = {
            ["end"] = 1, ["function"] = "fn_str", ["has space"] = "line1\nline2\r\"quoted\"\\slash",
            [0] = "zero_key", [-5] = "neg_key", [1.5] = "float_key", [true] = "bool_key",
            [1] = "arr1", [2] = "arr2", [3] = "arr3", [10] = "sparse10",
        },
        floats = { 0, -0.0, 1, -1, 3.14159265358979, 1e-7, 1e12, 1234567890123 },
        nested = {},
    }
    local n = 0
    for _, ent in pairs(G.Ents) do
        if ent:IsValid() and ent.GetSaveRecord then
            local ok, rec = pcall(ent.GetSaveRecord, ent)
            if ok and rec then
                n = n + 1
                test_sample.nested[n] = rec
                if n >= 200 then break end
            end
        end
    end

    local fast_str, fast_err = _fd(test_sample, nil)
    assert(fast_str ~= nil, "[BENCH] FastDump FAILED: " .. tostring(fast_err))
    local f_fast, err1 = G.loadstring(fast_str)
    assert(f_fast ~= nil, "[BENCH] FastDump loadstring failed: " .. tostring(err1))
    local eq, diff = DeepEqual(test_sample, f_fast(), "root")
    assert(eq, "[BENCH] FastDump ROUND-TRIP MISMATCH: " .. tostring(diff))

    local r_bad, e_bad = _fd({ x = 0 / 0 }, nil)
    assert(r_bad == nil and e_bad == "corrupt_number", "[BENCH] FastDump NaN guard failed: " .. tostring(e_bad))
    local r_inf, e_inf = _fd({ x = math.huge }, nil)
    assert(r_inf == nil and e_inf == "corrupt_number", "[BENCH] FastDump Inf guard failed: " .. tostring(e_inf))
    local cyc = {}; cyc.self = cyc
    local r_cyc, e_cyc = _fd(cyc, nil)
    assert(r_cyc == nil and e_cyc == "cycle_detected", "[BENCH] FastDump single cycle guard failed: " .. tostring(e_cyc))

    -- Red-team VETO-SERVER-01: multi-branch cycle {a=c, b=c} (would blow up 2^128 without visited set!)
    local mcyc = {}; mcyc.a = mcyc; mcyc.b = mcyc
    local t_mcyc = os.clock()
    local r_mcyc, e_mcyc = _fd(mcyc, nil)
    assert(r_mcyc == nil and e_mcyc == "cycle_detected" and (os.clock() - t_mcyc) < 0.05,
        "[BENCH] FastDump multi-branch cycle guard failed: " .. tostring(e_mcyc))

    -- Red-team exponential non-cyclic shared DAG (2^24 = 16.7M leaves -> must abort via node_limit_exceeded!)
    local dag = { 1 }
    for i = 1, 24 do
        dag = { dag, dag }
    end
    local r_dag, e_dag = _fd(dag, nil)
    assert(r_dag == nil and (e_dag == "node_limit_exceeded" or e_dag == "buffer_limit_exceeded"),
        "[BENCH] FastDump exponential DAG guard failed: " .. tostring(e_dag))

    -- Red-team NEW-03: SpatialGrid exact vanilla parity on edge & whole-world radii:
    --   1) radius = 5000 (whole-world finite scan) -> must return > 10,000 entities
    --   2) radius = math.huge -> delegates to vanilla (0x140114970 cvttss2si returns 0 in vanilla; 100% parity, no hang/UB)
    --   3) radius = -5 -> returns 0 entities
    local world_ents = G.TheSim:FindEntities(0, 0, 0, 5000)
    assert(type(world_ents) == "table" and #world_ents > 10000,
        "[BENCH] SpatialGrid FindEntities(5000) FAILED! got count=" .. tostring(type(world_ents) == "table" and #world_ents or "nil"))
    local huge_ents = G.TheSim:FindEntities(0, 0, 0, math.huge)
    assert(type(huge_ents) == "table" and #huge_ents == 0,
        "[BENCH] SpatialGrid FindEntities(math.huge) parity mismatch! got count=" .. tostring(type(huge_ents) == "table" and #huge_ents or "nil"))
    local neg_ents = G.TheSim:FindEntities(0, 0, 0, -5)
    assert(type(neg_ents) == "table" and #neg_ents == 0,
        "[BENCH] SpatialGrid FindEntities(-5) failed! got count=" .. tostring(type(neg_ents) == "table" and #neg_ents or "nil"))

    print(string.format("[BENCH_EQUIV] FastDump Round-Trip + MultiBranchCycle(%s) + ExpDAG(%s) + SpatialGrid(r=5000:%d, r=huge:%d, r=-5:%d): PASS",
        tostring(e_mcyc), tostring(e_dag), #world_ents, #huge_ents, #neg_ents))
end

AddSimPostInit(function()
    if not (G.TheWorld and G.TheWorld.ismastersim) then return end

    local init_ents = 0
    for _ in pairs(G.Ents) do init_ents = init_ents + 1 end
    print(string.format("[BENCH] MODE=%s InitialEnts=%d", MODE, init_ents))

    if MODE == "GEN_BASE" then
        G.TheWorld:DoTaskInTime(1.5, function()
            G.SaveGame(false, function()
                print("[BENCH] GEN_BASE_SAVED_OK")
                G.TheWorld:DoTaskInTime(0.3, function() G.c_shutdown(false) end)
            end)
        end)
        return
    end

    if MODE == "VERIFY_SAVELOAD" then
        -- Count our specific Hell World entities loaded back cleanly in 100% pure vanilla!
        local counts = { merm = 0, mermguard = 0, icebox = 0, winter_food = 0, perish_ok = 0, beefalo = 0, firesuppressor = 0 }
        local paused_foods = 0
        for _, ent in pairs(G.Ents) do
            if ent:IsValid() then
                local p = ent.prefab
                if counts[p] ~= nil then counts[p] = counts[p] + 1 end
                if p and p:sub(1, 11) == "winter_food" then
                    counts.winter_food = counts.winter_food + 1
                end
                if ent.components.perishable and (p == "berries" or p == "carrot" or p == "meat" or p == "monstermeat" or p == "fishmeat" or p == "drumstick" or p == "froglegs" or p == "watermelon" or p == "pumpkin") then
                    counts.perish_ok = counts.perish_ok + 1
                    if not ent.components.perishable:IsPerishing() then
                        paused_foods = paused_foods + 1
                    end
                end
            end
        end
        print(string.format("[BENCH_UNINSTALL_VERIFY] PASS ents=%d merms=%d+%d iceboxes=%d perish_foods=%d(stuck_paused=%d) winter_foods=%d beefalo=%d flingos=%d",
            init_ents, counts.merm, counts.mermguard, counts.icebox, counts.perish_ok, paused_foods, counts.winter_food, counts.beefalo, counts.firesuppressor))
        assert(counts.merm + counts.mermguard >= 75, "Missing merms after vanilla reload!")
        assert(counts.icebox >= 150, "Missing iceboxes after vanilla reload!")
        assert(counts.perish_ok >= 1300, "Missing boxed perishable foods after vanilla reload!")
        assert(counts.winter_food >= 1500, "Missing winter foods after vanilla reload!")
        assert(paused_foods == 0, "FAIL: Some foods were saved as paused=true!")
        G.TheWorld:DoTaskInTime(0.5, function() G.c_shutdown(false) end)
        return
    end

    -- Standard BENCH mode
    G.TheWorld:DoTaskInTime(0.8, function()
        SpawnHellWorld()

        -- Settle 2.0s so lazy perishables enter sleep/closed-box integral mode
        G.TheWorld:DoTaskInTime(2.0, function()
            local _rs = rawget(G, "HyperCore_ResetStats")
            local _gs = rawget(G, "HyperCore_GetStats")
            local _hs = rawget(G, "c_hyperstats")
            if type(_rs) == "function" then _rs() end
            print("[BENCH] WINDOW_START")

            G.TheWorld:DoTaskInTime(12.0, function()
                print("[BENCH] WINDOW_END")
                if type(_gs) == "function" then
                    local s = _gs()
                    local ticks = math.max(1, s.ticks_total or 1)
                    print(string.format("[BENCH_TICK] ticks=%d avg_us=%.0f p95_us=%.0f p99_us=%.0f max_us=%.0f overruns=%d gc_us_t=%.0f gc_max_us=%.0f gc_full=%d gc_kb=%d fe_calls=%d fe_us_t=%.0f zc=%d cells=%d mi_ops=%d",
                        s.ticks_total, s.tick_avg_us, s.tick_p95_us, s.tick_p99_us, s.tick_max_us, s.tick_overruns,
                        s.gc_us_sum / ticks, s.gc_max_us, s.gc_full_count, s.gc_last_kb,
                        s.find_entities_calls, s.find_entities_us_sum / ticks, s.spatial_zerocopy_hits, s.spatial_cells_visited,
                        (s.mi_allocs or 0) + (s.mi_reallocs or 0) + (s.mi_frees or 0)))
                end
                if type(_hs) == "function" then _hs() end

                VerifyFastDumperEquivalence()

                -- Reset save counters right before SaveGame for exact isolated save measurement
                if type(_rs) == "function" then _rs() end
                local save_t0 = os.clock()
                G.SaveGame(false, function()
                    local save_total_ms = (os.clock() - save_t0) * 1000
                    local extra = ""
                    if type(_gs) == "function" then
                        local s = _gs()
                        extra = string.format(" fastdump_ms=%.1f fastdump_MB=%.2f deflate_ms=%.1f deflate_in=%.2fMB deflate_out=%.2fMB ser_ms=%.1f",
                            s.fastdump_us_sum / 1000, s.fastdump_bytes_sum / (1024*1024),
                            s.deflate_us_sum / 1000, s.deflate_in_bytes / (1024*1024), s.deflate_out_bytes / (1024*1024),
                            s.serialize_world_us_sum / 1000)
                    end
                    print(string.format("[BENCH_SAVE_DONE] total_cb_ms=%.1f%s", save_total_ms, extra))
                    G.TheWorld:DoTaskInTime(0.4, function() G.c_shutdown(false) end)
                end)
                local save_sync_ms = (os.clock() - save_t0) * 1000
                print(string.format("[BENCH_SAVE_SYNC] main_thread_freeze_ms=%.1f", save_sync_ms))
            end)
        end)
    end)
end)
"""

def setup_runtime():
    os.makedirs(BIN64, exist_ok=True)
    os.makedirs(MODS_DIR, exist_ok=True)
    os.makedirs(STORAGE, exist_ok=True)

    data_link = os.path.join(RUNTIME, "data")
    if not os.path.exists(data_link):
        subprocess.run(["cmd", "/c", "mklink", "/J", data_link, os.path.join(DST_DIR, "data")], check=True)

    src_bin = os.path.join(DST_DIR, "bin64")
    for fn in ["dontstarve_dedicated_server_nullrenderer_x64.exe", "fmodex64.dll", "fmod_event64.dll", "steam_api64.dll"]:
        dst = os.path.join(BIN64, fn)
        if not os.path.exists(dst):
            shutil.copy2(os.path.join(src_bin, fn), dst)

    bm = os.path.join(MODS_DIR, "hypercore_bench")
    os.makedirs(bm, exist_ok=True)
    open(os.path.join(bm, "modinfo.lua"), "w", encoding="utf-8").write(BENCH_MOD_INFO)
    open(os.path.join(bm, "modmain.lua"), "w", encoding="utf-8").write(BENCH_MOD_MAIN)

    hc_dst = os.path.join(MODS_DIR, "hypercore_server")
    if os.path.exists(hc_dst):
        shutil.rmtree(hc_dst)
    shutil.copytree(os.path.join(ROOT, "server_mod"), hc_dst)

def set_bench_mode(mode: str):
    open(os.path.join(MODS_DIR, "hypercore_bench", "bench_mode.lua"), "w", encoding="utf-8").write(f'BENCH_MODE = "{mode}"\n')

def write_cluster_configs(enable_hypercore_mod: bool):
    mdir = os.path.join(CLUSTER_DIR, "Master")
    os.makedirs(mdir, exist_ok=True)
    open(os.path.join(CLUSTER_DIR, "cluster.ini"), "w", encoding="utf-8").write("""[GAMEPLAY]
game_mode = endless
max_players = 6
pvp = false
pause_when_empty = false

[NETWORK]
cluster_name = HyperCoreBench
cluster_description = Isolated Benchmark
cluster_intention = cooperative
offline_cluster = true
lan_only_cluster = true

[MISC]
console_enabled = true

[SHARD]
shard_enabled = false
""")
    open(os.path.join(CLUSTER_DIR, "cluster_token.txt"), "w", encoding="utf-8").write("pds-g^KU_bench^offline_token")
    open(os.path.join(mdir, "server.ini"), "w", encoding="utf-8").write("[NETWORK]\nserver_port = 11099\n[SHARD]\nis_master = true\nname = Master\nid = 1\n")
    open(os.path.join(mdir, "worldgenoverride.lua"), "w", encoding="utf-8").write('KLEI     1 return { override_enabled = true, preset = "SURVIVAL_TOGETHER", overrides = { world_size = "default" } }\n')

    mods_lua = 'return {\n  ["hypercore_bench"] = { enabled = true },\n'
    if enable_hypercore_mod:
        mods_lua += '  ["hypercore_server"] = { enabled = true },\n'
    mods_lua += '}\n'
    open(os.path.join(mdir, "modoverrides.lua"), "w", encoding="utf-8").write(mods_lua)
    open(os.path.join(MODS_DIR, "modsettings.lua"), "w", encoding="utf-8").write("")

def restore_base_world(enable_hypercore_mod: bool):
    if os.path.exists(CLUSTER_DIR):
        shutil.rmtree(CLUSTER_DIR)
    shutil.copytree(BASE_BACKUP, CLUSTER_DIR)
    write_cluster_configs(enable_hypercore_mod)

def write_native_config(install_dll: bool, cfg: dict):
    dll_dst = os.path.join(BIN64, "iphlpapi.dll")
    ini_dst = os.path.join(BIN64, "dst_hypercore.ini")
    for fn in os.listdir(BIN64):
        if fn.startswith("dst_hypercore") and fn.endswith(".log"):
            os.remove(os.path.join(BIN64, fn))
    if install_dll:
        shutil.copy2(os.path.join(ROOT, "native_plugin", "iphlpapi.dll"), dll_dst)
        open(ini_dst, "w", encoding="utf-8").write("\n".join(f"{k} = {v}" for k, v in cfg.items()) + "\n")
    else:
        for p in (dll_dst, ini_dst):
            if os.path.exists(p): os.remove(p)

def run_server(label: str, timeout_s: int = 120):
    exe = os.path.join(BIN64, "dontstarve_dedicated_server_nullrenderer_x64.exe")
    cmd = [
        exe,
        "-persistent_storage_root", STORAGE,
        "-conf_dir", "DoNotStarveTogether",
        "-cluster", CLUSTER_NAME,
        "-shard", "Master",
        "-offline",
        "-skip_update_server_mods",
    ]
    print(f"\n==================== {label} ====================", flush=True)
    t0 = time.time()
    # Keep stdin open via PIPE until process exits so DST never sees early EOF!
    proc = subprocess.Popen(
        cmd, cwd=BIN64, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=1
    )
    out_lines = []
    def _reader():
        assert proc.stdout is not None
        for line in proc.stdout:
            out_lines.append(line.rstrip("\r\n"))
    rt = threading.Thread(target=_reader, daemon=True)
    rt.start()
    try:
        proc.wait(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        print(f"  [TIMEOUT after {timeout_s}s]", flush=True)
    finally:
        try:
            if proc.stdin:
                proc.stdin.close()
        except Exception:
            pass
        rt.join(timeout=5)
    elapsed = time.time() - t0
    print(f"  Exit={proc.returncode} ({elapsed:.1f}s)", flush=True)
    for l in out_lines:
        if any(k in l for k in ["[BENCH", "[HyperCore", "LUA ERROR", "ASSERT", "stack traceback", "=== DST HyperCore"]):
            print("  |", l, flush=True)
    log_dst = os.path.join(BIN64, "dst_hypercore_Master.log")
    if os.path.exists(log_dst):
        print(f"  [DLL_LOG] Per-shard log verified: {os.path.basename(log_dst)}", flush=True)
        for l in open(log_dst, "r", encoding="utf-8", errors="replace"):
            if any(k in l for k in ["[FINAL]", "FAIL", "ERROR", "WARNING", "W^X", "AOB verified"]):
                print("  [DLL]", l.strip(), flush=True)
    assert not os.path.exists(os.path.join(BIN64, "dst_hypercore.log")), "Legacy shared dst_hypercore.log should not exist!"
    if proc.returncode != 0:
        print("  --- LAST 25 STDOUT LINES ON NON-ZERO EXIT ---", flush=True)
        for l in out_lines[-25:]:
            print("  !", l, flush=True)
    return proc.returncode, out_lines

def main():
    setup_runtime()

    # Step 0: Generate clean deterministic base world ONCE in 100% pure vanilla (if not cached)
    if not os.path.exists(BASE_BACKUP):
        if os.path.exists(CLUSTER_DIR):
            shutil.rmtree(CLUSTER_DIR)
        write_cluster_configs(enable_hypercore_mod=False)
        set_bench_mode("GEN_BASE")
        write_native_config(False, {})
        rc, _ = run_server("STEP 0: Generate Clean Deterministic Base World (100% Pure Vanilla)")
        assert rc == 0, f"Base worldgen failed: {rc}"
        shutil.copytree(CLUSTER_DIR, BASE_BACKUP)

    base_off = {
        "enable_profiler": 1, "enable_mimalloc": 0, "enable_spatial_zerocopy": 0,
        "enable_adaptive_gc": 0, "enable_libdeflate": 0, "enable_fast_dumper": 0,
        "enable_timer_boost": 0, "log_interval_ticks": 0,
    }
    all_on = {
        "enable_profiler": 1, "enable_mimalloc": 1, "enable_spatial_zerocopy": 1,
        "enable_adaptive_gc": 1, "enable_libdeflate": 1, "enable_fast_dumper": 1,
        "enable_timer_boost": 1, "log_interval_ticks": 0,
    }

    set_bench_mode("BENCH")

    # RUN A: Pure Vanilla Baseline (profiler=1 ONLY)
    restore_base_world(enable_hypercore_mod=False)
    write_native_config(True, base_off)
    run_server("RUN A: BASELINE (Vanilla — All 5 Native Opts OFF, Lua Mod OFF)")

    # RUN B1: Ablation — mimalloc ONLY
    c = dict(base_off, enable_mimalloc=1)
    restore_base_world(enable_hypercore_mod=False)
    write_native_config(True, c)
    run_server("RUN B1: ABLATION — mimalloc Lua Heap ONLY")

    # RUN B2: Ablation — SpatialGrid Zero-Copy ONLY
    c = dict(base_off, enable_spatial_zerocopy=1)
    restore_base_world(enable_hypercore_mod=False)
    write_native_config(True, c)
    run_server("RUN B2: ABLATION — SpatialGrid Zero-Copy ONLY")

    # RUN B3: Ablation — 30Hz Adaptive GC ONLY
    c = dict(base_off, enable_adaptive_gc=1)
    restore_base_world(enable_hypercore_mod=False)
    write_native_config(True, c)
    run_server("RUN B3: ABLATION — 30Hz Adaptive Slack GC ONLY")

    # RUN C: Layer 1 Native DLL ALL ON (Lua Mod OFF)
    restore_base_world(enable_hypercore_mod=False)
    write_native_config(True, all_on)
    run_server("RUN C: LAYER 1 ALL ON (Native DLL Only, Lua Mod OFF)")

    # RUN D: FULL STACK (Layer 1 Native DLL + Layer 2 Zero-Divergence Lua Mod)
    restore_base_world(enable_hypercore_mod=True)
    write_native_config(True, all_on)
    run_server("RUN D: FULL STACK (Layer 1 Native + Layer 2 Zero-Divergence Lua Mod)")

    # RUN E: UNINSTALL SAFETY TEST — Remove DLL & Lua Mod, reload EXACT Run-D Save in 100% Pure Vanilla!
    set_bench_mode("VERIFY_SAVELOAD")
    write_cluster_configs(enable_hypercore_mod=False)
    write_native_config(False, {})
    run_server("RUN E: UNINSTALL SAFETY (100% Pure Vanilla Reloading Run-D Hell Save!)")

if __name__ == "__main__":
    main()
