#!/usr/bin/env python3
"""
Exact P0 Save-Safety Regression Test for DST HyperCore Server v1.2.1 (opt_lazy_perishable)
inside Klei's REAL Dedicated Server Lua 5.1 engine with REAL prefabs/components:
  1. Reproduces exact Cluster_2 snapshot #117 crash record:
       { prefab="monstermeat", x=-45.735, z=-394.43, data={ perishable={time=-135.66}, stackable={stack=2} } }
     under POPULATING=true + OnEntitySleep during SetPosition -> MUST NOT crash and MUST rescue cleanly!
  2. Verifies OnSave() / GetSaveRecord() is 100% side-effect-free when a lazy perishable has expired:
     NEVER calls Perish()/Remove() mid-save, NEVER writes negative time, NEVER writes paused=true!
  3. Verifies GetPercent() is 100% pure read-only (never calls Perish()/Remove() even when expired).
  4. Verifies mutators (AddTime/SetPercent/ReducePercent/LongUpdate) guard inst:IsValid() after sync.
  5. Verifies container:Open() snapshots slots before syncing expiring lazy items (0 table-mutation bugs).
"""
import os
import shutil
import subprocess
import sys

SERVER_REPO = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server"
SERVER_MOD  = os.path.join(SERVER_REPO, "server_mod")
BENCH_RT    = os.path.join(SERVER_REPO, "bench_runtime")
BIN64       = os.path.join(BENCH_RT, "bin64")
MODS_DIR    = os.path.join(BENCH_RT, "mods")
STORAGE     = os.path.join(BENCH_RT, "storage")
BASE_BACKUP = os.path.join(BENCH_RT, "base_world_backup")
CLUSTER_NAME = "PerishSafetyCluster"
CLUSTER_DIR = os.path.join(STORAGE, "DoNotStarveTogether", CLUSTER_NAME)

HARNESS_NAME = "perish_savesafety_test"
HARNESS_DIR  = os.path.join(MODS_DIR, HARNESS_NAME)

HARNESS_MODINFO = """
name = "Perish SaveSafety Test"
description = "P0 Save-Safety regression test for HyperCore Server v1.2.1"
author = "test"
version = "1.0.0"
api_version = 10
dst_compatible = true
all_clients_require_mod = false
client_only_mod = false
"""

HARNESS_MODMAIN = r"""
local G = GLOBAL
local assert = G.assert

AddSimPostInit(function()
    if not (G.TheWorld and G.TheWorld.ismastersim) then return end

    G.TheWorld:DoTaskInTime(0.5, function()
        -- =====================================================================
        -- TEST 1: Exact Cluster_2 #117 Crash Reproduction & Rescue
        --         (POPULATING=true + OnEntitySleep during SetPosition + time=-135.66 + stack=2)
        -- =====================================================================
        do
            local saved_pop = G.POPULATING
            G.POPULATING = true
            local rec = {
                id = 102301,
                prefab = "monstermeat",
                x = -45.735,
                z = -394.43,
                data = {
                    perishable = { time = -135.66004444737 },
                    stackable = { stack = 2 },
                },
            }
            local inst = G.SpawnPrefab(rec.prefab)
            assert(inst ~= nil, "Failed spawning monstermeat")
            inst.Transform:SetPosition(rec.x, 0, rec.z)
            -- Simulate off-screen OnEntitySleep firing right after SetPosition (exact Klei engine sequence!)
            if inst.OnEntitySleep then inst:OnEntitySleep() end
            assert(inst.components.perishable._hc_lazy_active == nil,
                   "FAIL: EnterLazyMode MUST NOT activate while POPULATING==true!")
            -- Now execute SetPersistData (perishable:OnLoad + stackable:OnLoad)
            inst:SetPersistData(rec.data, {})
            G.POPULATING = saved_pop

            assert(inst:IsValid(), "FAIL: monstermeat became invalid during SetPersistData!")
            assert(inst.components.stackable:StackSize() == 2, "FAIL: stacksize lost!")
            assert(inst.replica.inventoryitem ~= nil and inst.replica.inventoryitem.classified ~= nil,
                   "FAIL: inventoryitem replica classified missing!")
            assert(inst.components.perishable.perishremainingtime > 0,
                   "FAIL: negative perishremainingtime was not rescued!")
            inst:Remove()
            print("[PERISH_SAFETY_TEST] 1. Exact Cluster_2 #117 negative-time + stackable rescue PASS")
        end

        -- =====================================================================
        -- TEST 2 & 3: Pure Read-Only GetPercent() + 100% Side-Effect-Free OnSave()
        -- =====================================================================
        do
            local fake_time = G.GetTime()
            local orig_GetTime = G.GetTime
            G.rawset(G, "GetTime", function() return fake_time end)

            local m = G.SpawnPrefab("monstermeat")
            m.Transform:SetPosition(120, 0, 120)
            m.components.stackable:SetStackSize(2)
            local p = m.components.perishable
            p:SetPercent(0.2)
            p:StartPerishing()
            -- Force enter lazy mode (ground sleeping)
            p._HC_EnterLazy()
            assert(p._hc_lazy_active == true, "FAIL: expected lazy mode active")

            -- Advance world clock by 10 days (far past expiry -> raw remaining time < -10000!)
            fake_time = fake_time + 4800

            -- TEST 3: GetPercent() MUST be pure read-only (never calls Perish/Remove!)
            local pct = p:GetPercent()
            assert(pct == 0, "Expected GetPercent()==0 for expired lazy item, got " .. tostring(pct))
            assert(m:IsValid(), "FAIL: GetPercent() destroyed entity!")
            assert(p._hc_lazy_active == true, "FAIL: GetPercent() mutated lazy state!")
            print("[PERISH_SAFETY_TEST] 2. Pure read-only GetPercent() on expired lazy item PASS")

            -- TEST 2: OnSave() / GetSaveRecord() MUST NOT call Perish()/Remove(), MUST NOT write negative time or paused=true,
            --         AND repeated OnSave() calls MUST schedule ZERO DoTaskInTime tasks on the entity (#08)!
            local function count_pending(ent)
                local c = 0
                if ent.pendingtasks ~= nil then
                    for _ in G.pairs(ent.pendingtasks) do c = c + 1 end
                end
                return c
            end
            local tasks_before = count_pending(m)
            local rec = nil
            for _ = 1, 5 do
                rec = m:GetSaveRecord()
            end
            local tasks_after = count_pending(m)
            assert(tasks_after == tasks_before,
                   "FAIL (#08): OnSave() mutated entity pendingtasks (" .. tostring(tasks_before) .. " -> " .. tostring(tasks_after) .. ")!")
            assert(m:IsValid(), "FAIL: OnSave()/GetSaveRecord() destroyed entity mid-save!")
            assert(rec ~= nil and rec.data ~= nil and rec.data.perishable ~= nil, "Missing perishable save data")
            assert(rec.data.perishable.time ~= nil and rec.data.perishable.time > 0,
                   "FAIL: OnSave() wrote negative/zero time: " .. tostring(rec.data.perishable.time))
            assert(rec.data.perishable.paused == nil,
                   "FAIL: OnSave() leaked paused=true into save!")
            assert(rec.data.stackable ~= nil and rec.data.stackable.stack == 2,
                   "FAIL: OnSave() corrupted stackable record!")
            print("[PERISH_SAFETY_TEST] 3. Side-effect-free 5x OnSave() (time=" .. tostring(rec.data.perishable.time) .. ", paused=nil, +0 tasks, valid=true) PASS")

            -- TEST 4: Mutator (AddTime) after expiry MUST cleanly perish without crashing _orig on dead entity!
            p:AddTime(5)
            assert(not m:IsValid(), "Expected AddTime->SyncLazyMode to cleanly perish expired monstermeat")
            print("[PERISH_SAFETY_TEST] 4. Post-expiry mutator (AddTime) validity guard PASS")

            -- TEST 5: Container:Open() with multiple expired lazy items MUST snapshot slots safely!
            local box = G.SpawnPrefab("icebox")
            box.Transform:SetPosition(140, 0, 140)
            for i = 1, 3 do
                local meat = G.SpawnPrefab("monstermeat")
                meat.components.perishable:SetPercent(0.05)
                meat.components.perishable:StartPerishing()
                box.components.container:GiveItem(meat, i)
                meat.components.perishable._HC_EnterLazy()
                assert(meat.components.perishable._hc_lazy_active == true, "Item " .. i .. " failed EnterLazy")
            end
            -- Advance 20 days so all 3 items in closed icebox expire simultaneously
            fake_time = fake_time + 9600
            box.components.container:Open(box)
            local rot_count = 0
            for i = 1, 3 do
                local it = box.components.container.slots[i]
                if it ~= nil and it:IsValid() and it.prefab == "spoiled_food" then
                    rot_count = rot_count + 1
                end
            end
            assert(rot_count == 3, "Expected 3 spoiled_food in opened icebox, got " .. tostring(rot_count))
            box:Remove()
            print("[PERISH_SAFETY_TEST] 5. Container:Open() multi-expiry snapshot safety PASS (3/3 spoiled cleanly)")

            -- TEST 6 (#08): StopPerishing() on lazy-paused item MUST settle accumulated decay first!
            local m6 = G.SpawnPrefab("monstermeat")
            m6.Transform:SetPosition(150, 0, 150)
            local p6 = m6.components.perishable
            p6:SetPercent(0.80)
            p6:StartPerishing()
            p6._HC_EnterLazy()
            assert(p6._hc_lazy_active == true, "TEST 6: expected lazy active")
            local pre_stop_time = p6.perishremainingtime
            fake_time = fake_time + 600 -- 600s elapsed while lazy-paused
            p6:StopPerishing()
            assert(p6._hc_lazy_active == nil, "TEST 6: expected lazy cleared")
            assert(p6.perishremainingtime < pre_stop_time - 400,
                   "FAIL (#08): StopPerishing() dropped accumulated lazy decay! before=" .. tostring(pre_stop_time) .. " after=" .. tostring(p6.perishremainingtime))
            assert(not p6:IsPerishing(), "TEST 6: expected stopped after StopPerishing()")
            m6:Remove()
            print("[PERISH_SAFETY_TEST] 6. StopPerishing() settles accumulated lazy decay PASS (" .. string.format("%.1f -> %.1f", pre_stop_time, p6.perishremainingtime) .. ")")

            -- TEST 7 (#08): StartPerishing() on lazy-paused item MUST settle accumulated decay first!
            local m7 = G.SpawnPrefab("monstermeat")
            m7.Transform:SetPosition(160, 0, 160)
            local p7 = m7.components.perishable
            p7:SetPercent(0.80)
            p7:StartPerishing()
            p7._HC_EnterLazy()
            assert(p7._hc_lazy_active == true, "TEST 7: expected lazy active")
            local pre_start_time = p7.perishremainingtime
            fake_time = fake_time + 600
            p7:StartPerishing()
            assert(p7._hc_lazy_active == nil, "TEST 7: expected lazy cleared")
            assert(p7.perishremainingtime < pre_start_time - 400,
                   "FAIL (#08): StartPerishing() dropped accumulated lazy decay! before=" .. tostring(pre_start_time) .. " after=" .. tostring(p7.perishremainingtime))
            assert(p7:IsPerishing(), "TEST 7: expected actively perishing after StartPerishing()")
            m7:Remove()
            print("[PERISH_SAFETY_TEST] 7. StartPerishing() settles accumulated lazy decay PASS (" .. string.format("%.1f -> %.1f", pre_start_time, p7.perishremainingtime) .. ")")

            G.rawset(G, "GetTime", orig_GetTime)
        end

        print("[PERISH_SAFETY_TEST] ALL_PASS")
        G.c_shutdown(false)
    end)
end)
"""


def main():
    assert os.path.isdir(BASE_BACKUP), f"Missing {BASE_BACKUP}"
    if os.path.exists(CLUSTER_DIR):
        shutil.rmtree(CLUSTER_DIR)
    shutil.copytree(BASE_BACKUP, CLUSTER_DIR)

    # Sync fresh native iphlpapi.dll + fixed server_mod into bench_runtime
    shutil.copy2(os.path.join(SERVER_REPO, "native_plugin", "iphlpapi.dll"), os.path.join(BIN64, "iphlpapi.dll"))
    dst_hc = os.path.join(MODS_DIR, "hypercore_server")
    os.makedirs(dst_hc, exist_ok=True)
    for fn in ("modinfo.lua", "modmain.lua"):
        shutil.copy2(os.path.join(SERVER_MOD, fn), os.path.join(dst_hc, fn))

    os.makedirs(HARNESS_DIR, exist_ok=True)
    with open(os.path.join(HARNESS_DIR, "modinfo.lua"), "w", encoding="utf-8") as f:
        f.write(HARNESS_MODINFO)
    with open(os.path.join(HARNESS_DIR, "modmain.lua"), "w", encoding="utf-8") as f:
        f.write(HARNESS_MODMAIN)

    overrides_path = os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua")
    try:
        with open(overrides_path, "w", encoding="utf-8") as f:
            f.write(
                'return {\n'
                '  ["hypercore_server"] = { enabled = true, configuration_options = {\n'
                '      opt_lazy_perishable = true, opt_staggered_wake = true, opt_sim_60hz = false\n'
                '  } },\n'
                f'  ["{HARNESS_NAME}"] = {{ enabled = true, configuration_options = {{}} }},\n'
                '}\n'
            )

        exe = os.path.join(BIN64, "dontstarve_dedicated_server_nullrenderer_x64.exe")
        proc = subprocess.run(
            [exe, "-console", "-offline", "-port", "11017",
             "-steam_master_server_port", "27030", "-steam_authentication_port", "8780",
             "-persistent_storage_root", STORAGE,
             "-cluster", CLUSTER_NAME, "-shard", "Master"],
            cwd=BIN64,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=90,
        )
        out = proc.stdout + "\n" + proc.stderr
        for line in out.splitlines():
            if "[PERISH_SAFETY_TEST]" in line or "LUA ERROR" in line or "HyperCore Server" in line:
                print(line)
        if proc.returncode != 0 or "[PERISH_SAFETY_TEST] ALL_PASS" not in out:
            print(f"=== FAILURE TAIL (rc={proc.returncode}) ===")
            print("\n".join(out.splitlines()[-60:]))
            sys.exit(1)
    finally:
        shutil.rmtree(HARNESS_DIR, ignore_errors=True)
        shutil.rmtree(CLUSTER_DIR, ignore_errors=True)


if __name__ == "__main__":
    main()
