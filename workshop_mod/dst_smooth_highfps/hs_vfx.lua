-- =============================================================================
-- HyperSmooth v2.9.5 Native Client-Side VFX Engine (hs_vfx.lua)
--   100% Pure Client-Side (Non-Networked Local Entities, Public-Server Safe)
--   Uses DST's built-in GPU VFXEffect + SetGroundPhysics + FollowSymbol +
--   EnvelopeManager with vanilla hand-drawn fx/*.tex assets:
--     1) Atmosphere & Lighting Micro-VFX:
--        - Rising warm/cold fire embers above lit campfires/firepits/furnaces
--        - Ambient golden sun motes (surface), bioluminescent cave spores, &
--          subtle seasonal drifting leaves/petals
--     2) Character Movement Micro-VFX (velocity-driven + ground physics):
--        - Turf-aware footstep dust, winter snow kick-up, rain splashes, &
--          occasional grass/leaf rustle (0 particles when standing still!)
--     3) Zero-Shake Combat & Work Impact Physics:
--        - Ground-bouncing hand-drawn wood chips (chop) & rock chunks (mine)
--        - Velocity-aligned bloom sparks & crisp rotating star glints
--        - Brief weapon-tip FollowSymbol("swap_object") swing glint on strike
-- =============================================================================

local G = GLOBAL
local pcall = G.pcall
local rawget = G.rawget
local type = G.type
local pairs = G.pairs
local math = G.math

local TEX_SNOW    = "fx/snow.tex"
local TEX_SMOKE   = "fx/smoke.tex"
local TEX_SPARK   = "fx/spark.tex"
local TEX_SPARKLE = "fx/sparkle.tex"
local TEX_PETAL   = "fx/petal.tex"
local TEX_ROCK    = "fx/debris_rock.tex"

local SH_PREMUL   = "shaders/vfx_particle.ksh"
local SH_ADD      = "shaders/vfx_particle_add.ksh"

local function ResPath(p)
    local fn = rawget(G, "resolvefilepath")
    if type(fn) == "function" then
        local ok, r = pcall(fn, p)
        if ok and type(r) == "string" and r ~= "" then
            return r
        end
    end
    return p
end

local function IntC(r, g, b, a)
    return { r / 255, g / 255, b / 255, a / 255 }
end

local function IsDedicated()
    local net = rawget(G, "TheNet")
    return net ~= nil and type(net.IsDedicated) == "function" and net:IsDedicated() == true
end

local function SafePos(ent)
    if ent ~= nil and ent.Transform ~= nil and type(ent.Transform.GetWorldPosition) == "function" then
        local ok, x, y, z = pcall(ent.Transform.GetWorldPosition, ent.Transform)
        if ok and type(x) == "number" and type(z) == "number" then
            return x, (type(y) == "number" and y or 0.0), z
        end
    end
    return nil, nil, nil
end

local function IsValidEnt(ent)
    if ent == nil then return false end
    if type(ent.IsValid) == "function" then
        return ent:IsValid() == true
    end
    return ent.entity ~= nil
end

local function Clamp(v, lo, hi)
    if type(v) ~= "number" or v ~= v then return lo end
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function RandRange(a, b)
    return a + math.random() * (b - a)
end

-- =============================================================================
-- State & Configuration (Player-First: all 0.0 in Preset 0 Vanilla Pure)
-- =============================================================================
local cfg = {
    fire_embers   = 0.0,
    ambient_motes = 0.0,
    footsteps     = 0.0,
    combat_vfx    = 0.0,
    combat_on     = false,
}

local stats = {
    atmo_emitted   = 0,
    foot_emitted   = 0,
    combat_emitted = 0,
    weapon_emitted = 0,
    tracked_fires  = 0,
}

local fire_sources = {} -- [inst] = 1 (warm) or 2 (cold)
local atmo_ent = nil
local foot_ent = nil
local combat_ent = nil
local weapon_ent = nil
local weapon_owner = nil

local last_px, last_pz = nil, nil
local acc_ember = 0.0
local acc_mote  = 0.0
local acc_leaf  = 0.0
local acc_foot  = 0.0
local acc_fleaf = 0.0
local swing_burst_t = 0.0
local swing_arcane  = false
local swing_scale   = 1.0

local envelopes_ready = false

local function EnsureEnvelopes()
    if envelopes_ready then return true end
    local em = rawget(G, "EnvelopeManager")
    if em == nil or type(em.AddColourEnvelope) ~= "function" or type(em.AddVector2Envelope) ~= "function" then
        return false
    end
    -- Guard across Cave <-> Surface SimReset (EnvelopeManager lives on TheGlobalInstance)
    if rawget(G, "_HS_VFX_ENV_DONE") == true then
        envelopes_ready = true
        return true
    end

    local ok = pcall(function()
        -- 1. Atmosphere: Warm Fire Embers
        em:AddColourEnvelope("hs_vfx_ember_warm_c", {
            { 0.00, IntC(255, 240, 175, 0)   },
            { 0.12, IntC(255, 210, 105, 235) },
            { 0.55, IntC(255, 132, 36,  210) },
            { 0.85, IntC(205, 58,  14,  110) },
            { 1.00, IntC(120, 22,  6,   0)   },
        })
        em:AddVector2Envelope("hs_vfx_ember_warm_s", {
            { 0.00, { 0.11, 0.11 } },
            { 0.35, { 0.14, 0.14 } },
            { 1.00, { 0.04, 0.04 } },
        })

        -- 2. Atmosphere: Cold Endo-Fire Sparks
        em:AddColourEnvelope("hs_vfx_ember_cold_c", {
            { 0.00, IntC(210, 255, 255, 0)   },
            { 0.14, IntC(145, 245, 255, 225) },
            { 0.60, IntC(64,  205, 235, 180) },
            { 1.00, IntC(20,  110, 160, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_ember_cold_s", {
            { 0.00, { 0.10, 0.10 } },
            { 0.40, { 0.13, 0.13 } },
            { 1.00, { 0.04, 0.04 } },
        })

        -- 3. Atmosphere: Surface Sun Motes (delicate, restrained)
        em:AddColourEnvelope("hs_vfx_mote_day_c", {
            { 0.00, IntC(255, 244, 206, 0)  },
            { 0.25, IntC(255, 238, 186, 78) },
            { 0.72, IntC(255, 228, 165, 68) },
            { 1.00, IntC(255, 218, 145, 0)  },
        })
        em:AddVector2Envelope("hs_vfx_mote_day_s", {
            { 0.00, { 0.07, 0.07 } },
            { 0.50, { 0.11, 0.11 } },
            { 1.00, { 0.06, 0.06 } },
        })

        -- 4. Atmosphere: Cave Bioluminescent Spores
        em:AddColourEnvelope("hs_vfx_mote_cave_c", {
            { 0.00, IntC(125, 245, 235, 0)   },
            { 0.25, IntC(110, 235, 225, 108) },
            { 0.70, IntC(142, 180, 255, 92)  },
            { 1.00, IntC(120, 130, 230, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_mote_cave_s", {
            { 0.00, { 0.08, 0.08 } },
            { 0.45, { 0.13, 0.13 } },
            { 1.00, { 0.06, 0.06 } },
        })

        -- 5. Atmosphere: Seasonal Drifting Leaves / Petals
        em:AddColourEnvelope("hs_vfx_leaf_c", {
            { 0.00, IntC(228, 176, 98, 0)   },
            { 0.15, IntC(228, 176, 98, 155) },
            { 0.80, IntC(212, 148, 76, 135) },
            { 1.00, IntC(185, 120, 58, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_leaf_s", {
            { 0.00, { 0.24, 0.24 } },
            { 0.60, { 0.22, 0.22 } },
            { 1.00, { 0.15, 0.15 } },
        })

        -- 6. Footsteps: Subtle Earth / Road / Stone Dust
        em:AddColourEnvelope("hs_vfx_foot_dust_c", {
            { 0.00, IntC(206, 194, 172, 0)  },
            { 0.18, IntC(206, 194, 172, 68) },
            { 0.65, IntC(188, 176, 156, 38) },
            { 1.00, IntC(168, 158, 140, 0)  },
        })
        em:AddVector2Envelope("hs_vfx_foot_dust_s", {
            { 0.00, { 0.16, 0.16 } },
            { 0.50, { 0.28, 0.28 } },
            { 1.00, { 0.38, 0.38 } },
        })

        -- 7. Footsteps: Winter Snow Kick-Up
        em:AddColourEnvelope("hs_vfx_foot_snow_c", {
            { 0.00, IntC(245, 250, 255, 0)   },
            { 0.12, IntC(245, 250, 255, 165) },
            { 0.78, IntC(230, 240, 250, 125) },
            { 1.00, IntC(220, 232, 245, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_foot_snow_s", {
            { 0.00, { 0.14, 0.14 } },
            { 0.65, { 0.13, 0.13 } },
            { 1.00, { 0.08, 0.08 } },
        })

        -- 8. Footsteps: Rain Splash Droplets
        em:AddColourEnvelope("hs_vfx_foot_rain_c", {
            { 0.00, IntC(195, 226, 248, 0)   },
            { 0.12, IntC(195, 226, 248, 150) },
            { 0.75, IntC(175, 212, 240, 95)  },
            { 1.00, IntC(160, 200, 232, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_foot_rain_s", {
            { 0.00, { 0.11, 0.11 } },
            { 1.00, { 0.06, 0.06 } },
        })

        -- 9. Footsteps: Grass/Turf Brush Leaf
        em:AddColourEnvelope("hs_vfx_foot_leaf_c", {
            { 0.00, IntC(170, 198, 118, 0)   },
            { 0.14, IntC(170, 198, 118, 175) },
            { 0.78, IntC(152, 178, 102, 140) },
            { 1.00, IntC(135, 158, 90,  0)   },
        })
        em:AddVector2Envelope("hs_vfx_foot_leaf_s", {
            { 0.00, { 0.25, 0.25 } },
            { 1.00, { 0.16, 0.16 } },
        })

        -- 10. Combat: Ground-Physics Wood Chips (Chop)
        em:AddColourEnvelope("hs_vfx_hit_wood_c", {
            { 0.00, IntC(198, 148, 92, 235) },
            { 0.72, IntC(176, 128, 76, 215) },
            { 1.00, IntC(148, 104, 62, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_hit_wood_s", {
            { 0.00, { 0.24, 0.24 } },
            { 0.75, { 0.21, 0.21 } },
            { 1.00, { 0.14, 0.14 } },
        })

        -- 11. Combat: Ground-Physics Rock Chunks (Mine)
        em:AddColourEnvelope("hs_vfx_hit_rock_c", {
            { 0.00, IntC(186, 184, 178, 240) },
            { 0.72, IntC(156, 154, 148, 220) },
            { 1.00, IntC(128, 126, 122, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_hit_rock_s", {
            { 0.00, { 0.25, 0.25 } },
            { 0.75, { 0.22, 0.22 } },
            { 1.00, { 0.15, 0.15 } },
        })

        -- 12. Combat: Directional Warm/Physical/Fire Sparks (Velocity-Aligned + Bloom)
        em:AddColourEnvelope("hs_vfx_hit_spark_w_c", {
            { 0.00, IntC(255, 255, 242, 255) },
            { 0.22, IntC(255, 224, 132, 245) },
            { 0.65, IntC(255, 142, 42,  195) },
            { 1.00, IntC(180, 52,  12,  0)   },
        })
        em:AddVector2Envelope("hs_vfx_hit_spark_w_s", {
            { 0.00, { 0.65, 0.95 } },
            { 0.55, { 0.42, 0.68 } },
            { 1.00, { 0.14, 0.26 } },
        })

        -- 13. Combat: Directional Shadow/Lunar Arcane Sparks (Velocity-Aligned + Bloom)
        em:AddColourEnvelope("hs_vfx_hit_spark_a_c", {
            { 0.00, IntC(235, 252, 255, 255) },
            { 0.24, IntC(155, 228, 255, 240) },
            { 0.68, IntC(172, 110, 255, 185) },
            { 1.00, IntC(95,  45,  190, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_hit_spark_a_s", {
            { 0.00, { 0.65, 0.95 } },
            { 0.55, { 0.42, 0.68 } },
            { 1.00, { 0.14, 0.26 } },
        })

        -- 14. Combat: Crisp Impact Star Glints
        em:AddColourEnvelope("hs_vfx_hit_glint_c", {
            { 0.00, IntC(255, 255, 245, 255) },
            { 0.35, IntC(255, 232, 150, 220) },
            { 1.00, IntC(255, 190, 90,  0)   },
        })
        em:AddVector2Envelope("hs_vfx_hit_glint_s", {
            { 0.00, { 0.48, 0.48 } },
            { 0.40, { 0.34, 0.34 } },
            { 1.00, { 0.08, 0.08 } },
        })

        -- 15. Weapon-Tip Follow Swing Glints (warm & arcane)
        em:AddColourEnvelope("hs_vfx_wpn_warm_c", {
            { 0.00, IntC(255, 250, 225, 220) },
            { 0.45, IntC(255, 212, 122, 165) },
            { 1.00, IntC(255, 155, 55,  0)   },
        })
        em:AddColourEnvelope("hs_vfx_wpn_arcane_c", {
            { 0.00, IntC(220, 255, 252, 220) },
            { 0.45, IntC(150, 195, 255, 165) },
            { 1.00, IntC(148, 92,  255, 0)   },
        })
        em:AddVector2Envelope("hs_vfx_wpn_s", {
            { 0.00, { 0.34, 0.34 } },
            { 0.60, { 0.20, 0.20 } },
            { 1.00, { 0.06, 0.06 } },
        })
    end)

    if ok then
        envelopes_ready = true
        G.rawset(G, "_HS_VFX_ENV_DONE", true)
    end
    return envelopes_ready
end

local function BlendMode(key, fallback)
    local bm = rawget(G, "BLENDMODE")
    if type(bm) == "table" and bm[key] ~= nil then return bm[key] end
    return fallback
end

local function NewLocalVfxHost(num_emitters)
    local create = rawget(G, "CreateEntity")
    if type(create) ~= "function" then return nil, nil end
    local inst = create()
    if inst == nil or inst.entity == nil then return nil, nil end
    if type(inst.AddTag) == "function" then
        inst:AddTag("FX")
        inst:AddTag("NOCLICK")
    end
    inst.persists = false
    if type(inst.entity.SetCanSleep) == "function" then
        inst.entity:SetCanSleep(false)
    end
    if type(inst.entity.AddTransform) == "function" then
        inst.entity:AddTransform()
    end
    if type(inst.entity.AddVFXEffect) ~= "function" then
        if type(inst.Remove) == "function" then inst:Remove() end
        return nil, nil
    end
    local eff = inst.entity:AddVFXEffect()
    if eff == nil or type(eff.InitEmitters) ~= "function" then
        if type(inst.Remove) == "function" then inst:Remove() end
        return nil, nil
    end
    eff:InitEmitters(num_emitters)
    return inst, eff
end

local function SafeCall(obj, method, ...)
    if obj ~= nil and type(obj[method]) == "function" then
        obj[method](obj, ...)
    end
end

-- CRITICAL DST C++ Engine Invariant:
--   Outside SpawnPrefab(), CreateEntity() + AddVFXEffect() + InitEmitters() leaves
--   VFXParticleEmitter::mRenderNode (+0x90) NULL until CallPrefabConstructionComplete()
--   invokes VFXEffect::OnPrefabConstructionComplete (vtable+0x38)!
local function FinalizeLocalEnt(inst)
    if inst ~= nil and inst.entity ~= nil and type(inst.entity.CallPrefabConstructionComplete) == "function" then
        inst.entity:CallPrefabConstructionComplete()
    end
end

local function EnsureHosts()
    if IsDedicated() then return false end
    if not EnsureEnvelopes() then return false end

    local shPremul = ResPath(SH_PREMUL)
    local shAdd    = ResPath(SH_ADD)
    local bmPremul = BlendMode("Premultiplied", 2)
    local bmAdd    = BlendMode("Additive", 1)

    -- 1. Atmosphere host (5 emitters: warm embers, cold embers, sun motes, cave spores, drifting leaves)
    if not IsValidEnt(atmo_ent) then
        local inst, eff = NewLocalVfxHost(5)
        if not inst then return false end
        atmo_ent = inst

        local texSnow  = ResPath(TEX_SNOW)
        local texPetal = ResPath(TEX_PETAL)

        -- 0: Warm fire embers
        SafeCall(eff, "SetRenderResources", 0, texSnow, shAdd)
        SafeCall(eff, "SetMaxNumParticles", 0, 180)
        SafeCall(eff, "SetMaxLifetime", 0, 2.6)
        SafeCall(eff, "SetColourEnvelope", 0, "hs_vfx_ember_warm_c")
        SafeCall(eff, "SetScaleEnvelope", 0, "hs_vfx_ember_warm_s")
        SafeCall(eff, "SetBlendMode", 0, bmAdd)
        SafeCall(eff, "EnableBloomPass", 0, true)
        SafeCall(eff, "SetUVFrameSize", 0, 1, 1)
        SafeCall(eff, "SetAcceleration", 0, 0, 0.008, 0)
        SafeCall(eff, "SetDragCoefficient", 0, 0.04)
        SafeCall(eff, "SetRadius", 0, 32)
        SafeCall(eff, "SetSortOrder", 0, 0)

        -- 1: Cold endo-fire sparks
        SafeCall(eff, "SetRenderResources", 1, texSnow, shAdd)
        SafeCall(eff, "SetMaxNumParticles", 1, 96)
        SafeCall(eff, "SetMaxLifetime", 1, 2.7)
        SafeCall(eff, "SetColourEnvelope", 1, "hs_vfx_ember_cold_c")
        SafeCall(eff, "SetScaleEnvelope", 1, "hs_vfx_ember_cold_s")
        SafeCall(eff, "SetBlendMode", 1, bmAdd)
        SafeCall(eff, "EnableBloomPass", 1, true)
        SafeCall(eff, "SetUVFrameSize", 1, 1, 1)
        SafeCall(eff, "SetAcceleration", 1, 0, 0.006, 0)
        SafeCall(eff, "SetDragCoefficient", 1, 0.05)
        SafeCall(eff, "SetRadius", 1, 32)
        SafeCall(eff, "SetSortOrder", 1, 0)

        -- 2: Surface sun motes
        SafeCall(eff, "SetRenderResources", 2, texSnow, shAdd)
        SafeCall(eff, "SetMaxNumParticles", 2, 110)
        SafeCall(eff, "SetMaxLifetime", 2, 5.0)
        SafeCall(eff, "SetColourEnvelope", 2, "hs_vfx_mote_day_c")
        SafeCall(eff, "SetScaleEnvelope", 2, "hs_vfx_mote_day_s")
        SafeCall(eff, "SetBlendMode", 2, bmAdd)
        SafeCall(eff, "EnableBloomPass", 2, true)
        SafeCall(eff, "SetUVFrameSize", 2, 1, 1)
        SafeCall(eff, "SetDragCoefficient", 2, 0.02)
        SafeCall(eff, "SetRadius", 2, 28)

        -- 3: Cave bioluminescent spores
        SafeCall(eff, "SetRenderResources", 3, texSnow, shAdd)
        SafeCall(eff, "SetMaxNumParticles", 3, 110)
        SafeCall(eff, "SetMaxLifetime", 3, 5.2)
        SafeCall(eff, "SetColourEnvelope", 3, "hs_vfx_mote_cave_c")
        SafeCall(eff, "SetScaleEnvelope", 3, "hs_vfx_mote_cave_s")
        SafeCall(eff, "SetBlendMode", 3, bmAdd)
        SafeCall(eff, "EnableBloomPass", 3, true)
        SafeCall(eff, "SetUVFrameSize", 3, 1, 1)
        SafeCall(eff, "SetDragCoefficient", 3, 0.02)
        SafeCall(eff, "SetRadius", 3, 28)

        -- 4: Seasonal drifting leaves/petals
        SafeCall(eff, "SetRenderResources", 4, texPetal, shPremul)
        SafeCall(eff, "SetMaxNumParticles", 4, 48)
        SafeCall(eff, "SetMaxLifetime", 4, 5.5)
        SafeCall(eff, "SetColourEnvelope", 4, "hs_vfx_leaf_c")
        SafeCall(eff, "SetScaleEnvelope", 4, "hs_vfx_leaf_s")
        SafeCall(eff, "SetBlendMode", 4, bmPremul)
        SafeCall(eff, "SetRotationStatus", 4, true)
        SafeCall(eff, "SetGroundPhysics", 4, true)
        SafeCall(eff, "SetUVFrameSize", 4, 0.125, 1)
        SafeCall(eff, "SetDragCoefficient", 4, 0.04)
        SafeCall(eff, "SetRadius", 4, 28)

        FinalizeLocalEnt(inst)

        -- Attach main tick updater onto atmo_ent via EmitterManager
        local emMgr = rawget(G, "EmitterManager")
        if emMgr ~= nil and type(emMgr.AddEmitter) == "function" then
            emMgr:AddEmitter(inst, nil, function()
                if type(HS_VFX) == "table" and type(HS_VFX.StepTick) == "function" then
                    HS_VFX.StepTick()
                end
            end)
        end
    end

    -- 2. Footstep host (4 emitters: dust, snow, rain splash, grass leaf; ALL with GroundPhysics!)
    if not IsValidEnt(foot_ent) then
        local inst, eff = NewLocalVfxHost(4)
        if inst then
            foot_ent = inst
            local texSmoke = ResPath(TEX_SMOKE)
            local texSnow  = ResPath(TEX_SNOW)
            local texPetal = ResPath(TEX_PETAL)

            -- 0: Dust
            SafeCall(eff, "SetRenderResources", 0, texSmoke, shPremul)
            SafeCall(eff, "SetMaxNumParticles", 0, 128)
            SafeCall(eff, "SetMaxLifetime", 0, 1.15)
            SafeCall(eff, "SetColourEnvelope", 0, "hs_vfx_foot_dust_c")
            SafeCall(eff, "SetScaleEnvelope", 0, "hs_vfx_foot_dust_s")
            SafeCall(eff, "SetBlendMode", 0, bmPremul)
            SafeCall(eff, "SetRotationStatus", 0, true)
            SafeCall(eff, "SetGroundPhysics", 0, true)
            SafeCall(eff, "SetUVFrameSize", 0, 0.25, 1)
            SafeCall(eff, "SetDragCoefficient", 0, 0.12)
            SafeCall(eff, "SetRadius", 0, 14)

            -- 1: Winter Snow
            SafeCall(eff, "SetRenderResources", 1, texSnow, shPremul)
            SafeCall(eff, "SetMaxNumParticles", 1, 140)
            SafeCall(eff, "SetMaxLifetime", 1, 1.50)
            SafeCall(eff, "SetColourEnvelope", 1, "hs_vfx_foot_snow_c")
            SafeCall(eff, "SetScaleEnvelope", 1, "hs_vfx_foot_snow_s")
            SafeCall(eff, "SetBlendMode", 1, bmPremul)
            SafeCall(eff, "SetGroundPhysics", 1, true)
            SafeCall(eff, "SetUVFrameSize", 1, 1, 1)
            SafeCall(eff, "SetAcceleration", 1, 0, -0.014, 0)
            SafeCall(eff, "SetDragCoefficient", 1, 0.05)
            SafeCall(eff, "SetRadius", 1, 14)

            -- 2: Rain Splash
            SafeCall(eff, "SetRenderResources", 2, texSnow, shPremul)
            SafeCall(eff, "SetMaxNumParticles", 2, 120)
            SafeCall(eff, "SetMaxLifetime", 2, 0.70)
            SafeCall(eff, "SetColourEnvelope", 2, "hs_vfx_foot_rain_c")
            SafeCall(eff, "SetScaleEnvelope", 2, "hs_vfx_foot_rain_s")
            SafeCall(eff, "SetBlendMode", 2, bmPremul)
            SafeCall(eff, "SetGroundPhysics", 2, true)
            SafeCall(eff, "SetUVFrameSize", 2, 1, 1)
            SafeCall(eff, "SetAcceleration", 2, 0, -0.028, 0)
            SafeCall(eff, "SetDragCoefficient", 2, 0.04)
            SafeCall(eff, "SetRadius", 2, 14)

            -- 3: Grass/Turf Brush Leaf
            SafeCall(eff, "SetRenderResources", 3, texPetal, shPremul)
            SafeCall(eff, "SetMaxNumParticles", 3, 48)
            SafeCall(eff, "SetMaxLifetime", 3, 1.80)
            SafeCall(eff, "SetColourEnvelope", 3, "hs_vfx_foot_leaf_c")
            SafeCall(eff, "SetScaleEnvelope", 3, "hs_vfx_foot_leaf_s")
            SafeCall(eff, "SetBlendMode", 3, bmPremul)
            SafeCall(eff, "SetRotationStatus", 3, true)
            SafeCall(eff, "SetGroundPhysics", 3, true)
            SafeCall(eff, "SetUVFrameSize", 3, 0.125, 1)
            SafeCall(eff, "SetAcceleration", 3, 0, -0.010, 0)
            SafeCall(eff, "SetDragCoefficient", 3, 0.06)
            SafeCall(eff, "SetRadius", 3, 14)

            FinalizeLocalEnt(inst)
        end
    end

    -- 3. Combat & Work Hand-Drawn Impact Physics host (5 emitters)
    if not IsValidEnt(combat_ent) then
        local inst, eff = NewLocalVfxHost(5)
        if inst then
            combat_ent = inst
            local texRock    = ResPath(TEX_ROCK)
            local texSpark   = ResPath(TEX_SPARK)
            local texSparkle = ResPath(TEX_SPARKLE)

            -- 0: Wood chips (ground-physics!)
            SafeCall(eff, "SetRenderResources", 0, texRock, shPremul)
            SafeCall(eff, "SetMaxNumParticles", 0, 128)
            SafeCall(eff, "SetMaxLifetime", 0, 1.35)
            SafeCall(eff, "SetColourEnvelope", 0, "hs_vfx_hit_wood_c")
            SafeCall(eff, "SetScaleEnvelope", 0, "hs_vfx_hit_wood_s")
            SafeCall(eff, "SetBlendMode", 0, bmPremul)
            SafeCall(eff, "SetRotationStatus", 0, true)
            SafeCall(eff, "SetGroundPhysics", 0, true)
            SafeCall(eff, "SetUVFrameSize", 0, 0.25, 0.5)
            SafeCall(eff, "SetAcceleration", 0, 0, -0.026, 0)
            SafeCall(eff, "SetDragCoefficient", 0, 0.04)
            SafeCall(eff, "SetAngularDragCoefficient", 0, 0.08)
            SafeCall(eff, "SetRadius", 0, 28)

            -- 1: Rock chunks (ground-physics!)
            SafeCall(eff, "SetRenderResources", 1, texRock, shPremul)
            SafeCall(eff, "SetMaxNumParticles", 1, 128)
            SafeCall(eff, "SetMaxLifetime", 1, 1.40)
            SafeCall(eff, "SetColourEnvelope", 1, "hs_vfx_hit_rock_c")
            SafeCall(eff, "SetScaleEnvelope", 1, "hs_vfx_hit_rock_s")
            SafeCall(eff, "SetBlendMode", 1, bmPremul)
            SafeCall(eff, "SetRotationStatus", 1, true)
            SafeCall(eff, "SetGroundPhysics", 1, true)
            SafeCall(eff, "SetUVFrameSize", 1, 0.25, 0.5)
            SafeCall(eff, "SetAcceleration", 1, 0, -0.032, 0)
            SafeCall(eff, "SetDragCoefficient", 1, 0.03)
            SafeCall(eff, "SetAngularDragCoefficient", 1, 0.08)
            SafeCall(eff, "SetRadius", 1, 28)

            -- 2: Warm / Physical / Fire / Mine sparks (velocity-aligned + ground bounce + bloom!)
            SafeCall(eff, "SetRenderResources", 2, texSpark, shAdd)
            SafeCall(eff, "SetMaxNumParticles", 2, 160)
            SafeCall(eff, "SetMaxLifetime", 2, 0.65)
            SafeCall(eff, "SetColourEnvelope", 2, "hs_vfx_hit_spark_w_c")
            SafeCall(eff, "SetScaleEnvelope", 2, "hs_vfx_hit_spark_w_s")
            SafeCall(eff, "SetBlendMode", 2, bmAdd)
            SafeCall(eff, "EnableBloomPass", 2, true)
            SafeCall(eff, "SetRotateOnVelocity", 2, true)
            SafeCall(eff, "SetGroundPhysics", 2, true)
            SafeCall(eff, "SetUVFrameSize", 2, 0.25, 1)
            SafeCall(eff, "SetAcceleration", 2, 0, -0.022, 0)
            SafeCall(eff, "SetDragCoefficient", 2, 0.08)
            SafeCall(eff, "SetRadius", 2, 28)

            -- 3: Shadow / Lunar arcane sparks
            SafeCall(eff, "SetRenderResources", 3, texSpark, shAdd)
            SafeCall(eff, "SetMaxNumParticles", 3, 128)
            SafeCall(eff, "SetMaxLifetime", 3, 0.65)
            SafeCall(eff, "SetColourEnvelope", 3, "hs_vfx_hit_spark_a_c")
            SafeCall(eff, "SetScaleEnvelope", 3, "hs_vfx_hit_spark_a_s")
            SafeCall(eff, "SetBlendMode", 3, bmAdd)
            SafeCall(eff, "EnableBloomPass", 3, true)
            SafeCall(eff, "SetRotateOnVelocity", 3, true)
            SafeCall(eff, "SetGroundPhysics", 3, true)
            SafeCall(eff, "SetUVFrameSize", 3, 0.25, 1)
            SafeCall(eff, "SetAcceleration", 3, 0, -0.016, 0)
            SafeCall(eff, "SetDragCoefficient", 3, 0.09)
            SafeCall(eff, "SetRadius", 3, 28)

            -- 4: Crisp rotating impact star glints
            SafeCall(eff, "SetRenderResources", 4, texSparkle, shAdd)
            SafeCall(eff, "SetMaxNumParticles", 4, 64)
            SafeCall(eff, "SetMaxLifetime", 4, 0.36)
            SafeCall(eff, "SetColourEnvelope", 4, "hs_vfx_hit_glint_c")
            SafeCall(eff, "SetScaleEnvelope", 4, "hs_vfx_hit_glint_s")
            SafeCall(eff, "SetBlendMode", 4, bmAdd)
            SafeCall(eff, "EnableBloomPass", 4, true)
            SafeCall(eff, "SetRotationStatus", 4, true)
            SafeCall(eff, "SetUVFrameSize", 4, 0.25, 1)
            SafeCall(eff, "SetDragCoefficient", 4, 0.12)
            SafeCall(eff, "SetRadius", 4, 28)

            FinalizeLocalEnt(inst)
        end
    end

    return IsValidEnt(atmo_ent)
end

local function EnsureWeaponFollower(player)
    if IsDedicated() or not IsValidEnt(player) then return nil end
    if IsValidEnt(weapon_ent) and weapon_owner == player then
        return weapon_ent
    end
    if IsValidEnt(weapon_ent) and type(weapon_ent.Remove) == "function" then
        pcall(weapon_ent.Remove, weapon_ent)
    end
    weapon_ent = nil
    weapon_owner = nil

    if not EnsureEnvelopes() then return nil end
    local inst, eff = NewLocalVfxHost(2)
    if not inst then return nil end

    local shAdd      = ResPath(SH_ADD)
    local texSparkle = ResPath(TEX_SPARKLE)
    local bmAdd      = BlendMode("Additive", 1)

    for idx = 0, 1 do
        SafeCall(eff, "SetRenderResources", idx, texSparkle, shAdd)
        SafeCall(eff, "SetMaxNumParticles", idx, 64)
        SafeCall(eff, "SetMaxLifetime", idx, 0.34)
        SafeCall(eff, "SetColourEnvelope", idx, idx == 0 and "hs_vfx_wpn_warm_c" or "hs_vfx_wpn_arcane_c")
        SafeCall(eff, "SetScaleEnvelope", idx, "hs_vfx_wpn_s")
        SafeCall(eff, "SetBlendMode", idx, bmAdd)
        SafeCall(eff, "EnableBloomPass", idx, true)
        SafeCall(eff, "SetRotationStatus", idx, true)
        SafeCall(eff, "SetUVFrameSize", idx, 0.25, 1)
        SafeCall(eff, "SetDragCoefficient", idx, 0.10)
    end

    if type(inst.entity.AddFollower) == "function" and type(inst.entity.SetParent) == "function" and player.entity ~= nil and player.GUID ~= nil then
        local ok = pcall(function()
            inst.entity:AddFollower()
            inst.entity:SetParent(player.entity)
            if inst.Follower ~= nil and type(inst.Follower.FollowSymbol) == "function" then
                inst.Follower:FollowSymbol(player.GUID, "swap_object", 0, -90, 0)
            end
        end)
        if not ok then
            if type(inst.Remove) == "function" then pcall(inst.Remove, inst) end
            return nil
        end
    end
    FinalizeLocalEnt(inst)
    weapon_ent = inst
    weapon_owner = player
    return weapon_ent
end

local function IsGrassOrForestTile(tile)
    if tile == nil then return false end
    local WT = rawget(G, "WORLD_TILES") or rawget(G, "GROUND")
    if type(WT) ~= "table" then return false end
    return tile == WT.GRASS or tile == WT.FOREST or tile == WT.DECIDUOUS
        or tile == WT.SAVANNA or tile == WT.MARSH or tile == WT.FUNGUS
        or tile == WT.FUNGUSRED or tile == WT.FUNGUSGREEN
end

local function ClearHostEmitters(ent, start_idx, end_idx)
    if IsValidEnt(ent) and ent.VFXEffect ~= nil and type(ent.VFXEffect.ClearAllParticles) == "function" then
        for i = start_idx, end_idx do
            pcall(ent.VFXEffect.ClearAllParticles, ent.VFXEffect, i)
        end
    end
end

-- =============================================================================
-- Public API (HS_VFX table in mod environment)
-- =============================================================================
HS_VFX = {}

function HS_VFX.ApplyConfig(fire_embers, ambient_motes, footsteps, combat_on, combat_vfx)
    cfg.fire_embers   = Clamp(fire_embers or 0.0, 0.0, 1.5)
    cfg.ambient_motes = Clamp(ambient_motes or 0.0, 0.0, 1.5)
    cfg.footsteps     = Clamp(footsteps or 0.0, 0.0, 1.5)
    cfg.combat_on     = (combat_on == true)
    cfg.combat_vfx    = Clamp(combat_vfx or 0.0, 0.0, 1.5)
    if cfg.fire_embers <= 0.005 then
        ClearHostEmitters(atmo_ent, 0, 1)
    end
    if cfg.ambient_motes <= 0.005 then
        ClearHostEmitters(atmo_ent, 2, 4)
    end
    if cfg.footsteps <= 0.005 then
        ClearHostEmitters(foot_ent, 0, 3)
    end
    if not (cfg.combat_on and cfg.combat_vfx > 0.005) then
        swing_burst_t = 0.0
        ClearHostEmitters(combat_ent, 0, 4)
        ClearHostEmitters(weapon_ent, 0, 1)
    end
    if (cfg.fire_embers > 0.005 or cfg.ambient_motes > 0.005 or cfg.footsteps > 0.005
        or (cfg.combat_on and cfg.combat_vfx > 0.005)) then
        if rawget(G, "ThePlayer") ~= nil then
            EnsureHosts()
        end
    end
end

function HS_VFX.GetStats()
    local nf = 0
    for k in pairs(fire_sources) do
        if IsValidEnt(k) then nf = nf + 1 else fire_sources[k] = nil end
    end
    stats.tracked_fires = nf
    return stats
end

function HS_VFX.RegisterFire(inst, kind)
    if IsDedicated() or not IsValidEnt(inst) then return end
    fire_sources[inst] = kind or 1
    if type(inst.ListenForEvent) == "function" then
        inst:ListenForEvent("onremove", function()
            fire_sources[inst] = nil
        end)
    end
end

function HS_VFX.OnImpact(tx, ty, tz, px, py, pz, intensity, style, is_work)
    if IsDedicated() or not cfg.combat_on or cfg.combat_vfx <= 0.005 then return false end
    if type(tx) ~= "number" or type(tz) ~= "number" then return false end
    if not EnsureHosts() or not IsValidEnt(combat_ent) or combat_ent.VFXEffect == nil then return false end

    local player = rawget(G, "ThePlayer")
    local ax, _, az = SafePos(player)
    if ax == nil then ax, az = px or tx, pz or tz end
    if combat_ent.Transform ~= nil and type(combat_ent.Transform.SetPosition) == "function" then
        combat_ent.Transform:SetPosition(ax, 0, az)
    end

    local eff = combat_ent.VFXEffect
    local ox = tx - ax
    local oy = (type(ty) == "number" and ty or 0.0) + (is_work and 0.85 or 0.98)
    local oz = tz - az

    local dx = tx - (px or tx)
    local dz = tz - (pz or tz)
    local dlen = math.sqrt(dx * dx + dz * dz)
    if dlen > 1e-4 then
        dx, dz = dx / dlen, dz / dlen
    else
        dx, dz = 1.0, 0.0
    end
    local base_ang = math.atan2(dz, dx)
    local sc = Clamp(cfg.combat_vfx, 0.15, 1.5)
    local inten = Clamp(intensity or 1.0, 0.4, 1.6)
    local emitted = 0

    if style == 5 then
        -- Chop: hand-drawn wood chips with TRUE SetGroundPhysics bounce & settle!
        local n = math.floor(Clamp(5 * inten * (0.65 + 0.35 * sc), 3, 11) + 0.5)
        for _ = 1, n do
            local ang = base_ang + RandRange(-1.45, 1.45)
            local spd = RandRange(0.055, 0.145) * inten
            local u = math.random(0, 3) * 0.25
            local v = math.random(0, 1) * 0.5
            SafeCall(eff, "AddRotatingParticleUV", 0,
                RandRange(0.75, 1.30),
                ox + RandRange(-0.18, 0.18), oy + RandRange(-0.22, 0.25), oz + RandRange(-0.18, 0.18),
                math.cos(ang) * spd, RandRange(0.06, 0.14), math.sin(ang) * spd,
                math.random() * 360, RandRange(-9.0, 9.0),
                u, v)
            emitted = emitted + 1
        end
    elseif style == 6 then
        -- Mine: hand-drawn rock fragments + bright velocity-aligned flint sparks!
        local nr = math.floor(Clamp(4 * inten * (0.65 + 0.35 * sc), 3, 9) + 0.5)
        for _ = 1, nr do
            local ang = base_ang + RandRange(-1.55, 1.55)
            local spd = RandRange(0.050, 0.135) * inten
            local u = math.random(0, 3) * 0.25
            local v = math.random(0, 1) * 0.5
            SafeCall(eff, "AddRotatingParticleUV", 1,
                RandRange(0.80, 1.35),
                ox + RandRange(-0.16, 0.16), oy + RandRange(-0.18, 0.22), oz + RandRange(-0.16, 0.16),
                math.cos(ang) * spd, RandRange(0.07, 0.16), math.sin(ang) * spd,
                math.random() * 360, RandRange(-10.0, 10.0),
                u, v)
            emitted = emitted + 1
        end
        local ns = math.floor(Clamp(6 * inten * (0.65 + 0.35 * sc), 4, 12) + 0.5)
        for _ = 1, ns do
            local ang = base_ang + RandRange(-1.15, 1.15)
            local spd = RandRange(0.095, 0.210) * inten
            SafeCall(eff, "AddParticleUV", 2,
                RandRange(0.28, 0.58),
                ox + RandRange(-0.12, 0.12), oy + RandRange(-0.12, 0.16), oz + RandRange(-0.12, 0.12),
                math.cos(ang) * spd, RandRange(0.05, 0.13), math.sin(ang) * spd,
                math.random(0, 3) * 0.25, 0)
            emitted = emitted + 1
        end
    else
        -- Combat Hit: directional bloom sparks + crisp star glints + brief weapon-tip swing glint
        local is_arcane = (style == 2 or style == 3) -- Shadow / Lunar
        local sp_idx = is_arcane and 3 or 2
        local ns = math.floor(Clamp(7 * inten * (0.65 + 0.35 * sc), 4, 14) + 0.5)
        for _ = 1, ns do
            local ang = base_ang + RandRange(-0.95, 0.95)
            local spd = RandRange(0.10, 0.22) * inten
            SafeCall(eff, "AddParticleUV", sp_idx,
                RandRange(0.26, 0.55),
                ox + RandRange(-0.15, 0.15), oy + RandRange(-0.18, 0.20), oz + RandRange(-0.15, 0.15),
                math.cos(ang) * spd, RandRange(0.04, 0.13), math.sin(ang) * spd,
                math.random(0, 3) * 0.25, 0)
            emitted = emitted + 1
        end
        local ng = math.floor(Clamp(3 * inten * (0.6 + 0.4 * sc), 2, 6) + 0.5)
        for _ = 1, ng do
            SafeCall(eff, "AddRotatingParticleUV", 4,
                RandRange(0.18, 0.34),
                ox + RandRange(-0.24, 0.24), oy + RandRange(-0.20, 0.24), oz + RandRange(-0.24, 0.24),
                RandRange(-0.025, 0.025), RandRange(0.015, 0.045), RandRange(-0.025, 0.025),
                math.random() * 360, RandRange(-6.0, 6.0),
                math.random(0, 3) * 0.25, 0)
            emitted = emitted + 1
        end

        -- Arm brief weapon-tip FollowSymbol("swap_object") swing glint burst (0.13s, silent when idle!)
        if player ~= nil then
            EnsureWeaponFollower(player)
            swing_burst_t = 0.13
            swing_arcane  = is_arcane
            swing_scale   = sc
        end
    end

    stats.combat_emitted = stats.combat_emitted + emitted
    return emitted > 0
end

function HS_VFX.StepTick(dt_override)
    if IsDedicated() then return end
    local want_fire   = cfg.fire_embers > 0.005
    local want_mote   = cfg.ambient_motes > 0.005
    local want_foot   = cfg.footsteps > 0.005
    local want_swing  = swing_burst_t > 0.001 and cfg.combat_on and cfg.combat_vfx > 0.005
    if not (want_fire or want_mote or want_foot or want_swing) then
        last_px, last_pz = nil, nil
        return
    end

    local player = rawget(G, "ThePlayer")
    if not IsValidEnt(player) then
        last_px, last_pz = nil, nil
        return
    end
    local px, py, pz = SafePos(player)
    if px == nil then return end

    local sim = rawget(G, "TheSim")
    local dt = dt_override
    if type(dt) ~= "number" or dt <= 0 then
        dt = (sim ~= nil and type(sim.GetTickTime) == "function") and sim:GetTickTime() or (1.0 / 30.0)
    end
    if dt > 0.1 then dt = 0.1 end

    if not EnsureHosts() then return end

    -- Sync world anchors to player XZ at exact y = 0 for accurate ground physics + frustum radius
    if IsValidEnt(atmo_ent) and atmo_ent.Transform then
        atmo_ent.Transform:SetPosition(px, 0, pz)
    end
    if IsValidEnt(foot_ent) and foot_ent.Transform then
        foot_ent.Transform:SetPosition(px, 0, pz)
    end

    local world = rawget(G, "TheWorld")
    local wstate = (world ~= nil and type(world.state) == "table") and world.state or nil
    local is_cave = (world ~= nil and type(world.HasTag) == "function" and world:HasTag("cave")) == true

    -- -------------------------------------------------------------------------
    -- 1. Rising Fire Embers (Zero FindEntities: O(tracked lit fires), max 6!)
    -- -------------------------------------------------------------------------
    if want_fire and IsValidEnt(atmo_ent) and atmo_ent.VFXEffect ~= nil then
        local eff = atmo_ent.VFXEffect
        acc_ember = acc_ember + dt * (4.6 * cfg.fire_embers)
        if acc_ember >= 1.0 then
            local burst = math.floor(acc_ember)
            if burst > 3 then burst = 3 end
            acc_ember = acc_ember - burst
            local served = 0
            for f_inst, f_kind in pairs(fire_sources) do
                if not IsValidEnt(f_inst) then
                    fire_sources[f_inst] = nil
                else
                    local fx, fy, fz = SafePos(f_inst)
                    if fx ~= nil then
                        local dx, dz = fx - px, fz - pz
                        if dx * dx + dz * dz <= 576.0 then -- 24 tiles
                            local em_idx = (f_kind == 2) and 1 or 0
                            for _ = 1, burst do
                                SafeCall(eff, "AddParticle", em_idx,
                                    RandRange(1.4, 2.5),
                                    dx + RandRange(-0.42, 0.42),
                                    (fy or 0.0) + RandRange(0.45, 1.05),
                                    dz + RandRange(-0.42, 0.42),
                                    RandRange(-0.016, 0.016),
                                    RandRange(0.022, 0.048),
                                    RandRange(-0.016, 0.016))
                                stats.atmo_emitted = stats.atmo_emitted + 1
                            end
                            served = served + 1
                            if served >= 6 then break end
                        end
                    end
                end
            end
        end
    end

    -- -------------------------------------------------------------------------
    -- 2. Ambient Sun Motes / Cave Spores / Seasonal Drifting Leaves
    -- -------------------------------------------------------------------------
    if want_mote and IsValidEnt(atmo_ent) and atmo_ent.VFXEffect ~= nil then
        local eff = atmo_ent.VFXEffect
        local is_raining = (wstate ~= nil and wstate.israining == true)
        local is_night   = (wstate ~= nil and wstate.isnight == true)
        local is_autumn  = (wstate ~= nil and wstate.isautumn == true)
        local is_spring  = (wstate ~= nil and wstate.isspring == true)

        local rate = is_cave and 7.5 or (is_night and 2.8 or (is_raining and 2.0 or 6.5))
        acc_mote = acc_mote + dt * (rate * cfg.ambient_motes)
        while acc_mote >= 1.0 do
            acc_mote = acc_mote - 1.0
            local em_idx = is_cave and 3 or 2
            SafeCall(eff, "AddParticle", em_idx,
                RandRange(3.2, 4.8),
                RandRange(-14.0, 14.0), RandRange(0.45, 4.2), RandRange(-14.0, 14.0),
                RandRange(-0.012, 0.018), RandRange(-0.004, 0.008), RandRange(-0.012, 0.012))
            stats.atmo_emitted = stats.atmo_emitted + 1
        end

        if (not is_cave) and (not is_raining) and (is_autumn or is_spring) then
            acc_leaf = acc_leaf + dt * (1.25 * cfg.ambient_motes)
            if acc_leaf >= 1.0 then
                acc_leaf = acc_leaf - 1.0
                SafeCall(eff, "AddRotatingParticleUV", 4,
                    RandRange(3.8, 5.4),
                    RandRange(-13.0, 13.0), RandRange(3.4, 5.2), RandRange(-13.0, 13.0),
                    RandRange(0.018, 0.042), RandRange(-0.022, -0.011), RandRange(-0.015, 0.015),
                    math.random() * 360, RandRange(-2.5, 2.5),
                    math.random(0, 7) * 0.125, 0)
                stats.atmo_emitted = stats.atmo_emitted + 1
            end
        end
    end

    -- -------------------------------------------------------------------------
    -- 3. Velocity-Driven Turf-Aware Footstep Micro-VFX (Ground Physics!)
    -- -------------------------------------------------------------------------
    if want_foot and IsValidEnt(foot_ent) and foot_ent.VFXEffect ~= nil then
        local visible = (player.entity == nil or type(player.entity.IsVisible) ~= "function" or player.entity:IsVisible() == true)
        if last_px ~= nil and visible then
            local vx = px - last_px
            local vz = pz - last_pz
            local dist = math.sqrt(vx * vx + vz * vz)
            -- Ignore teleports (dist > 2.5 in 1 tick) and idle micro-jitter (dist < 0.035)
            if dist >= 0.035 and dist < 2.5 then
                local move = Clamp(dist * 5.2, 0.0, 1.0)
                local on_land = true
                local tile = nil
                if world ~= nil and world.Map ~= nil then
                    if type(world.Map.IsVisualGroundAtPoint) == "function" then
                        on_land = world.Map:IsVisualGroundAtPoint(px, 0, pz) == true
                    elseif type(world.Map.IsPassableAtPoint) == "function" then
                        on_land = world.Map:IsPassableAtPoint(px, 0, pz) == true
                    end
                    if type(world.Map.GetTileAtPoint) == "function" then
                        tile = world.Map:GetTileAtPoint(px, 0, pz)
                    end
                end
                if on_land then
                    local eff = foot_ent.VFXEffect
                    local nx, nz = -vx / dist, -vz / dist -- kick slightly backward behind heels
                    local snow_lvl = (wstate ~= nil and type(wstate.snowlevel) == "number") and wstate.snowlevel or 0.0
                    local is_rain  = (wstate ~= nil and wstate.israining == true)
                    local is_snow  = (not is_cave) and snow_lvl > 0.20

                    local pps = (is_snow and 15.0 or (is_rain and 14.0 or 10.5)) * move * cfg.footsteps
                    acc_foot = acc_foot + dt * pps
                    local emitted_n = 0
                    while acc_foot >= 1.0 and emitted_n < 3 do
                        acc_foot = acc_foot - 1.0
                        emitted_n = emitted_n + 1
                        local sx = nx * 0.22 + RandRange(-0.24, 0.24)
                        local sz = nz * 0.22 + RandRange(-0.24, 0.24)
                        if is_snow then
                            SafeCall(eff, "AddParticle", 1,
                                RandRange(0.75, 1.35),
                                sx, 0.12, sz,
                                nx * RandRange(0.015, 0.045) + RandRange(-0.02, 0.02),
                                RandRange(0.028, 0.058),
                                nz * RandRange(0.015, 0.045) + RandRange(-0.02, 0.02))
                        elseif is_rain then
                            SafeCall(eff, "AddParticle", 2,
                                RandRange(0.35, 0.65),
                                sx, 0.10, sz,
                                nx * RandRange(0.025, 0.060) + RandRange(-0.028, 0.028),
                                RandRange(0.042, 0.078),
                                nz * RandRange(0.025, 0.060) + RandRange(-0.028, 0.028))
                        else
                            SafeCall(eff, "AddRotatingParticleUV", 0,
                                RandRange(0.55, 1.05),
                                sx, 0.10, sz,
                                nx * RandRange(0.010, 0.028) + RandRange(-0.015, 0.015),
                                RandRange(0.008, 0.022),
                                nz * RandRange(0.010, 0.028) + RandRange(-0.015, 0.015),
                                math.random() * 360, RandRange(-2.0, 2.0),
                                math.random(0, 3) * 0.25, 0)
                        end
                        stats.foot_emitted = stats.foot_emitted + 1
                    end

                    -- Occasional grass/leaf rustle when running on verdant turf
                    if (not is_snow) and (not is_cave) and IsGrassOrForestTile(tile) then
                        acc_fleaf = acc_fleaf + dt * (1.8 * move * cfg.footsteps)
                        if acc_fleaf >= 1.0 then
                            acc_fleaf = acc_fleaf - 1.0
                            SafeCall(eff, "AddRotatingParticleUV", 3,
                                RandRange(1.0, 1.7),
                                nx * 0.25 + RandRange(-0.28, 0.28), 0.22, nz * 0.25 + RandRange(-0.28, 0.28),
                                nx * RandRange(0.02, 0.05) + RandRange(-0.025, 0.025),
                                RandRange(0.028, 0.052),
                                nz * RandRange(0.02, 0.05) + RandRange(-0.025, 0.025),
                                math.random() * 360, RandRange(-5.0, 5.0),
                                math.random(0, 7) * 0.125, 0)
                            stats.foot_emitted = stats.foot_emitted + 1
                        end
                    end
                end
            end
        end
    end
    last_px, last_pz = px, pz

    -- -------------------------------------------------------------------------
    -- 4. Brief Weapon-Tip FollowSymbol("swap_object") Glint Burst on Strike
    -- -------------------------------------------------------------------------
    if want_swing then
        swing_burst_t = swing_burst_t - dt
        if IsValidEnt(weapon_ent) and weapon_ent.VFXEffect ~= nil then
            local eff = weapon_ent.VFXEffect
            local em_idx = swing_arcane and 1 or 0
            local cnt = (swing_scale >= 1.0) and 2 or 1
            for _ = 1, cnt do
                SafeCall(eff, "AddRotatingParticleUV", em_idx,
                    RandRange(0.16, 0.30),
                    RandRange(-0.12, 0.12), RandRange(-0.12, 0.12), RandRange(-0.12, 0.12),
                    RandRange(-0.018, 0.018), RandRange(-0.010, 0.020), RandRange(-0.018, 0.018),
                    math.random() * 360, RandRange(-5.0, 5.0),
                    math.random(0, 3) * 0.25, 0)
                stats.weapon_emitted = stats.weapon_emitted + 1
            end
        end
    end
end

local bound_world_vfx = nil

local function BindVfxWorldTeardown()
    local world = rawget(G, "TheWorld")
    if world ~= nil and world ~= bound_world_vfx and type(world.ListenForEvent) == "function" then
        bound_world_vfx = world
        pcall(world.ListenForEvent, world, "onremove", function()
            if type(HS_VFX) == "table" and type(HS_VFX.Shutdown) == "function" then
                pcall(HS_VFX.Shutdown)
            end
        end)
    end
end

function HS_VFX.Shutdown()
    local function Kill(e)
        if IsValidEnt(e) and type(e.Remove) == "function" then pcall(e.Remove, e) end
    end
    Kill(atmo_ent);   atmo_ent = nil
    Kill(foot_ent);   foot_ent = nil
    Kill(combat_ent); combat_ent = nil
    Kill(weapon_ent); weapon_ent = nil
    weapon_owner = nil
    bound_world_vfx = nil
    last_px, last_pz = nil, nil
    swing_burst_t = 0.0
end

-- =============================================================================
-- Lightweight Prefab Hooks for Zero-Scan Lit Fire Tracking
-- =============================================================================
if not IsDedicated() and type(AddPrefabPostInit) == "function" then
    local WARM_FIRES = {
        "campfirefire", "portable_campfirefire", "fire", "pigtorch_flame",
        "dragonflyfurnace", "chimineafire",
        "cotl_tabernacle_level2", "cotl_tabernacle_level3",
    }
    for i = 1, #WARM_FIRES do
        AddPrefabPostInit(WARM_FIRES[i], function(inst)
            HS_VFX.RegisterFire(inst, 1)
        end)
    end
    AddPrefabPostInit("coldfirefire", function(inst)
        HS_VFX.RegisterFire(inst, 2)
    end)
end

if not IsDedicated() and type(AddPlayerPostInit) == "function" then
    AddPlayerPostInit(function(inst)
        if inst ~= nil and type(inst.DoTaskInTime) == "function" then
            inst:DoTaskInTime(0.2, function()
                local tp = rawget(G, "ThePlayer")
                if tp ~= nil and tp == inst then
                    BindVfxWorldTeardown()
                    if cfg.fire_embers > 0.005 or cfg.ambient_motes > 0.005
                        or cfg.footsteps > 0.005 or (cfg.combat_on and cfg.combat_vfx > 0.005) then
                        EnsureHosts()
                    end
                end
            end)
        end
    end)
end
