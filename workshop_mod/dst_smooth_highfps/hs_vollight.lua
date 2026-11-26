-- =============================================================================
-- HyperSmooth v2.9.5 World-Space Volumetric Lighting Engine (hs_vollight.lua)
--   100% Pure Client-Side (Non-Networked Local Entities + Native ShadeRenderer)
--   Zero server dependency, 100% public-server safe, Player-First (0 in Preset 0):
--     Gap 1: True 3D World-Space Forest & Cave Volumetric Light Rays
--            (official anim/lightrays.zip, deterministic grid streaming, smooth
--            Day/Dusk/FullMoon color & camera/player proximity fade)
--     Gap 2A: Native C++ ShadeRenderer Dynamic Drifting Cloud Shadows
--             (bespoke images/hs_cloud_shade.tex matching images/tree.tex polarity,
--             slow multi-axis cloud drift & organic scale breathing across world)
--     Gap 2B: Native C++ ShadeRenderer Deep-Forest Dappled Canopy Shade
--             (official images/tree.tex on dedicated ShadeType — zero conflict
--             with vanilla Waterlogged ShadeTypes.LeafCanopy!)
-- =============================================================================

local G = GLOBAL
local pcall = G.pcall
local rawget = G.rawget
local type = G.type
local pairs = G.pairs
local math = G.math

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

local function IsDedicated()
    local net = rawget(G, "TheNet")
    return net ~= nil and type(net.IsDedicated) == "function" and net:IsDedicated() == true
end

local function IsWorldReady()
    return rawget(G, "TheWorld") ~= nil and rawget(G, "ThePlayer") ~= nil
end

local function IsValidEnt(ent)
    if ent == nil then return false end
    if type(ent.IsValid) == "function" then return ent:IsValid() == true end
    return ent.entity ~= nil
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

local function Clamp(v, lo, hi)
    if type(v) ~= "number" or v ~= v then return lo end
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

-- Deterministic Lua 5.1 cell hash returning [0, 1)
local function CellHash(gx, gz, salt)
    local h = math.sin(gx * 127.1 + gz * 311.7 + (salt or 0) * 74.7) * 43758.5453123
    return h - math.floor(h)
end

local function CellKey(gx, gz)
    return gx * 65536 + gz
end

-- =============================================================================
-- State & Configuration (Player-First: all 0.0 in Preset 0 Vanilla Pure)
-- =============================================================================
local cfg = {
    lightrays     = 0.0,
    lightrays_all = false,
    cloud_shade   = 0.0,
    canopy_shade  = 0.0,
    edge_fog      = 0.0,
    ground_fog    = 0.0,
}

local stats = {
    active_rays        = 0,
    non_forest_rays    = 0,
    active_clouds      = 0,
    active_canopy      = 0,
    ray_alpha          = 0.0,
    cloud_str          = 0.0,
    canopy_str         = 0.0,
    edge_fog_emitted   = 0,
    ground_fog_emitted = 0,
    fog_host_ready     = false,
}

local ctrl_ent = nil
local fog_ent  = nil
local ray_nodes = {}      -- [cell_key] = { ent, wx, wz, fade, base_gain }
local cloud_handles = {}  -- [cell_key] = { handle, wx, wz }
local canopy_handles = {} -- [cell_key] = { handle, wx, wz }

local cloud_shade_type = nil
local canopy_shade_type = nil
local cloud_tex_bound = false
local canopy_tex_bound = false
local bound_world = nil
local shade_globally_nudged = false
local fog_envelopes_ready = false

local cur_ray_alpha   = 0.0
local cur_ray_r       = 1.0
local cur_ray_g       = 0.95
local cur_ray_b       = 0.78
local cur_cloud_str   = 0.0
local cur_canopy_str  = 0.0

local stream_timer    = 99.0
local edge_fog_acc    = 0.0
local ground_fog_acc  = 0.0
local fog_scan_phase  = 0
local last_stream_x   = nil
local last_stream_z   = nil

-- =============================================================================
-- Tile Classification Helpers
-- =============================================================================
local function GetTileAt(world, x, z)
    if world == nil or world.Map == nil or type(world.Map.GetTileAtPoint) ~= "function" then
        return nil
    end
    local ok, t = pcall(world.Map.GetTileAtPoint, world.Map, x, 0, z)
    return ok and t or nil
end

local function IsForestTile(tile)
    if tile == nil then return false end
    local WT = rawget(G, "WORLD_TILES") or rawget(G, "GROUND")
    if type(WT) ~= "table" then return false end
    return tile == WT.FOREST or tile == WT.DECIDUOUS or tile == WT.MONKEY_GROUND
end

local function IsCaveRayTile(tile)
    if tile == nil then return false end
    local WT = rawget(G, "WORLD_TILES") or rawget(G, "GROUND")
    if type(WT) ~= "table" then return false end
    return tile == WT.SINKHOLE or tile == WT.FUNGUS
        or tile == WT.FUNGUSRED or tile == WT.FUNGUSGREEN
end

local function IsVoidEdgeTile(tile)
    if tile == nil then return true end
    local WT = rawget(G, "WORLD_TILES") or rawget(G, "GROUND")
    if type(WT) ~= "table" then return tile == 1 or tile == 255 end
    return tile == WT.IMPASSABLE or tile == WT.INVALID
end

local TREE_MUST_TAGS = { "tree" }
local TREE_CANT_TAGS = { "stump", "burnt", "INLIMBO" }

local function IsWalkableSurfaceLand(world, wx, wz, tile)
    if tile == nil or IsVoidEdgeTile(tile) then return false end
    if world ~= nil and world.Map ~= nil and type(world.Map.IsVisualGroundAtPoint) == "function" then
        local ok, res = pcall(world.Map.IsVisualGroundAtPoint, world.Map, wx, 0, wz)
        if ok then return res == true end
    end
    local fnOcean = rawget(G, "IsOceanTile")
    if type(fnOcean) == "function" then
        local ok, oc = pcall(fnOcean, tile)
        if ok and oc == true then return false end
    end
    local WT = rawget(G, "WORLD_TILES") or rawget(G, "GROUND")
    if type(WT) == "table" then
        if tile == WT.OCEAN_COASTAL or tile == WT.OCEAN_COASTAL_SHORE or tile == WT.OCEAN_SWELL
            or tile == WT.OCEAN_ROUGH or tile == WT.OCEAN_HAZARDOUS or tile == WT.OCEAN_BRINEPOOL
            or tile == WT.OCEAN_BRINEPOOL_SHORE or tile == WT.OCEAN_WATERLOG then
            return false
        end
    end
    return true
end

local function HasNearbyLivingTree(wx, wz)
    local sim = rawget(G, "TheSim")
    if sim == nil or type(sim.FindEntities) ~= "function" then return false end
    local ok, ents = pcall(sim.FindEntities, sim, wx, 0, wz, 7.5, TREE_MUST_TAGS, TREE_CANT_TAGS)
    return ok and type(ents) == "table" and #ents > 0
end

local function GetGroundFogTileWeight(tile, is_cave)
    if tile == nil then return 0.0 end
    local WT = rawget(G, "WORLD_TILES") or rawget(G, "GROUND")
    if type(WT) ~= "table" then return 0.0 end
    if is_cave then
        if tile == WT.SINKHOLE or tile == WT.FUNGUS or tile == WT.FUNGUSRED
            or tile == WT.FUNGUSGREEN or tile == WT.MUD or tile == WT.UNDERROCK then
            return 1.0
        end
        return 0.0
    end
    if tile == WT.MARSH then return 1.25 end
    if tile == WT.FOREST or tile == WT.DECIDUOUS or tile == WT.MONKEY_GROUND then return 0.85 end
    return 0.0
end

local function IntC(r, g, b, a)
    return { r / 255, g / 255, b / 255, a / 255 }
end

local function SafeCall(obj, method, ...)
    if obj ~= nil and type(obj[method]) == "function" then
        obj[method](obj, ...)
    end
end

local function EnsureFogEnvelopes()
    if fog_envelopes_ready then return true end
    local em = rawget(G, "EnvelopeManager")
    if em == nil or type(em.AddColourEnvelope) ~= "function" or type(em.AddVector2Envelope) ~= "function" then
        return false
    end
    if rawget(G, "_HS_VOLLIGHT_FOG_ENV_V2_DONE") == true then
        fog_envelopes_ready = true
        return true
    end
    local ok = pcall(function()
        -- Emitter 0: World-Edge / Void Ocean Mist (sheer white ribbon, tinted by lightmap via vfx_particle.ksh)
        em:AddColourEnvelope("hs_vl_edge_fog_v2_c", {
            { 0.00, IntC(255, 255, 255, 0)  },
            { 0.16, IntC(255, 255, 255, 88) },
            { 0.74, IntC(255, 255, 255, 80) },
            { 1.00, IntC(255, 255, 255, 0)  },
        })
        em:AddVector2Envelope("hs_vl_edge_fog_v2_s", {
            { 0.00, { 7.5,  7.5  } },
            { 0.55, { 10.5, 10.5 } },
            { 1.00, { 12.8, 12.8 } },
        })
        -- Emitter 1: Lightmap-Lit Forest/Marsh/Cave Ground Mist (matches official mistparticle.lua ~11% sheer alpha)
        em:AddColourEnvelope("hs_vl_ground_fog_v2_c", {
            { 0.00, IntC(255, 255, 255, 0)  },
            { 0.15, IntC(255, 255, 255, 28) },
            { 0.74, IntC(255, 255, 255, 26) },
            { 1.00, IntC(255, 255, 255, 0)  },
        })
        em:AddVector2Envelope("hs_vl_ground_fog_v2_s", {
            { 0.00, { 5.8, 5.8 } },
            { 0.55, { 7.8, 7.8 } },
            { 1.00, { 9.8, 9.8 } },
        })
    end)
    if ok then
        fog_envelopes_ready = true
        G.rawset(G, "_HS_VOLLIGHT_FOG_ENV_V2_DONE", true)
    end
    return fog_envelopes_ready
end

local function DestroyFogHost()
    if IsValidEnt(fog_ent) then
        if fog_ent.VFXEffect ~= nil then
            SafeCall(fog_ent.VFXEffect, "ClearAllParticles", 0)
            SafeCall(fog_ent.VFXEffect, "ClearAllParticles", 1)
        end
        if type(fog_ent.Remove) == "function" then
            pcall(fog_ent.Remove, fog_ent)
        end
    end
    fog_ent = nil
    stats.fog_host_ready = false
end

local function EnsureFogHost()
    if IsDedicated() then return false end
    if IsValidEnt(fog_ent) and fog_ent.VFXEffect ~= nil then
        stats.fog_host_ready = true
        return true
    end
    if not EnsureFogEnvelopes() then return false end
    local create = rawget(G, "CreateEntity")
    if type(create) ~= "function" then return false end
    local inst = create()
    if inst == nil or inst.entity == nil then return false end
    if type(inst.AddTag) == "function" then
        inst:AddTag("FX")
        inst:AddTag("NOCLICK")
        inst:AddTag("NOBLOCK")
        inst:AddTag("ignorewalkableplatforms")
    end
    inst.persists = false
    if type(inst.entity.SetCanSleep) == "function" then inst.entity:SetCanSleep(false) end
    if type(inst.entity.AddTransform) == "function" then inst.entity:AddTransform() end
    -- Keep fog host PERMANENTLY anchored at world origin (0,0,0) so MatrixW stays Identity
    -- and moving the player NEVER drags, slides, or depth-pops existing mist ribbons!
    if inst.Transform ~= nil and type(inst.Transform.SetPosition) == "function" then
        inst.Transform:SetPosition(0, 0, 0)
    end
    if type(inst.entity.SetAABB) == "function" then
        inst.entity:SetAABB(4096, 32)
    end
    if type(inst.entity.AddVFXEffect) ~= "function" then
        if type(inst.Remove) == "function" then pcall(inst.Remove, inst) end
        return false
    end
    local eff = inst.entity:AddVFXEffect()
    if eff == nil or type(eff.InitEmitters) ~= "function" then
        if type(inst.Remove) == "function" then pcall(inst.Remove, inst) end
        return false
    end
    eff:InitEmitters(2)

    local texFog = ResPath("levels/textures/ds_fog1.tex")
    local shFog  = ResPath("shaders/vfx_particle.ksh")
    local bmTbl  = rawget(G, "BLENDMODE")
    local bmPremul = (type(bmTbl) == "table" and bmTbl.Premultiplied) or 3

    -- Emitter 0: Horizontal World-Edge / Ocean Void Mist (NO rotation — ds_fog1.tex is a 512x128 wind ribbon!)
    SafeCall(eff, "SetRenderResources", 0, texFog, shFog)
    SafeCall(eff, "SetMaxNumParticles", 0, 64)
    SafeCall(eff, "SetMaxLifetime", 0, 20.0)
    SafeCall(eff, "SetColourEnvelope", 0, "hs_vl_edge_fog_v2_c")
    SafeCall(eff, "SetScaleEnvelope", 0, "hs_vl_edge_fog_v2_s")
    SafeCall(eff, "SetBlendMode", 0, bmPremul)
    SafeCall(eff, "SetUVFrameSize", 0, 1, 1)
    SafeCall(eff, "SetFollowEmitter", 0, false)
    SafeCall(eff, "SetSpawnVectors", 0, -1, 0, 1, 1, 0, 1)
    SafeCall(eff, "SetSortOrder", 0, 3)
    SafeCall(eff, "SetRadius", 0, 4096)

    -- Emitter 1: Horizontal Lightmap-Lit Forest / Marsh / Cave Ground Mist (matches official mistparticle.lua)
    SafeCall(eff, "SetRenderResources", 1, texFog, shFog)
    SafeCall(eff, "SetMaxNumParticles", 1, 32)
    SafeCall(eff, "SetMaxLifetime", 1, 20.0)
    SafeCall(eff, "SetColourEnvelope", 1, "hs_vl_ground_fog_v2_c")
    SafeCall(eff, "SetScaleEnvelope", 1, "hs_vl_ground_fog_v2_s")
    SafeCall(eff, "SetBlendMode", 1, bmPremul)
    SafeCall(eff, "SetUVFrameSize", 1, 1, 1)
    SafeCall(eff, "SetFollowEmitter", 1, false)
    SafeCall(eff, "SetSpawnVectors", 1, -1, 0, 1, 1, 0, 1)
    SafeCall(eff, "SetSortOrder", 1, 3)
    SafeCall(eff, "SetRadius", 1, 4096)

    -- CRITICAL DST C++ Engine Invariant: must finalize mRenderNode after InitEmitters!
    if type(inst.entity.CallPrefabConstructionComplete) == "function" then
        inst.entity:CallPrefabConstructionComplete()
    end
    fog_ent = inst
    stats.fog_host_ready = true
    return true
end

local function EmitFogParticle(eff, idx, life, wx, wy, wz, vx, vy, vz)
    if type(eff.AddParticle) == "function" then
        eff:AddParticle(idx, life, wx, wy, wz, vx, vy, vz)
    end
end

local function StepVolumetricFog(px, pz, world, wstate, is_cave, dt)
    if cfg.edge_fog <= 0.005 and cfg.ground_fog <= 0.005 then
        if fog_ent ~= nil then DestroyFogHost() end
        return
    end
    if not EnsureFogHost() or not IsValidEnt(fog_ent) or fog_ent.VFXEffect == nil then return end
    local eff = fog_ent.VFXEffect

    -- Emitter 0: World-Edge / Void Ocean Mist (world-anchored, slow dreamy drift)
    if cfg.edge_fog <= 0.005 then
        edge_fog_acc = 0.0
        SafeCall(eff, "ClearAllParticles", 0)
    else
        edge_fog_acc = edge_fog_acc + dt * Clamp(cfg.edge_fog, 0.25, 1.50)
        if edge_fog_acc >= 0.78 then
            edge_fog_acc = edge_fog_acc - 0.78
            if edge_fog_acc > 1.56 then edge_fog_acc = 0.0 end
            fog_scan_phase = (fog_scan_phase + 1) % 4
            local emitted = 0
            local STEP = 8.0
            local bx = math.floor(px / STEP) * STEP
            local bz = math.floor(pz / STEP) * STEP
            for gz = -5, 5 do
                if emitted >= 2 then break end
                for gx = -5, 5 do
                    if emitted >= 2 then break end
                    if ((gx + gz + fog_scan_phase) % 4) == 0 then
                        local wx = bx + gx * STEP + (math.random() - 0.5) * 4.5
                        local wz = bz + gz * STEP + (math.random() - 0.5) * 4.5
                        local dx, dz = wx - px, wz - pz
                        local d2 = dx * dx + dz * dz
                        if d2 >= 196.0 and d2 <= 2500.0 then -- 14..50 units
                            local tile = GetTileAt(world, wx, wz)
                            if IsVoidEdgeTile(tile) then
                                local life = 13.0 + math.random() * 6.0
                                local wy = 0.35 + math.random() * 0.20
                                local vx = 0.006 + math.random() * 0.006
                                local vz = (math.random() - 0.5) * 0.006
                                EmitFogParticle(eff, 0, life, wx, wy, wz, vx, 0.0, vz)
                                emitted = emitted + 1
                                stats.edge_fog_emitted = stats.edge_fog_emitted + 1
                            end
                        end
                    end
                end
            end
        end
    end

    -- Emitter 1: Lightmap-Lit Forest / Marsh / Cave Ground Mist (world-anchored, sheer vanilla feel)
    if cfg.ground_fog <= 0.005 then
        ground_fog_acc = 0.0
        SafeCall(eff, "ClearAllParticles", 1)
    else
        local is_dusk  = (wstate ~= nil and wstate.isdusk == true)
        local is_night = (wstate ~= nil and wstate.isnight == true)
        local is_rain  = (wstate ~= nil and wstate.israining == true)
        local atmo_m   = (is_cave or is_dusk or is_night or is_rain) and 1.00 or 0.60
        ground_fog_acc = ground_fog_acc + dt * Clamp(cfg.ground_fog, 0.25, 1.50) * atmo_m
        if ground_fog_acc >= 1.15 then
            ground_fog_acc = ground_fog_acc - 1.15
            if ground_fog_acc > 2.30 then ground_fog_acc = 0.0 end
            local emitted = 0
            for _ = 1, 6 do
                if emitted >= 1 then break end
                local ang = math.random() * 6.2831853
                local dist = 7.5 + math.random() * 24.5
                local wx = px + math.cos(ang) * dist
                local wz = pz + math.sin(ang) * dist
                local w = GetGroundFogTileWeight(GetTileAt(world, wx, wz), is_cave)
                if w > 0.0 and math.random() < w then
                    local life = 14.0 + math.random() * 5.5
                    local wy = 0.35 + math.random() * 0.15
                    local vx = 0.005 + math.random() * 0.006
                    local vz = (math.random() - 0.5) * 0.006
                    EmitFogParticle(eff, 1, life, wx, wy, wz, vx, 0.0, vz)
                    emitted = emitted + 1
                    stats.ground_fog_emitted = stats.ground_fog_emitted + 1
                end
            end
        end
    end
end

-- =============================================================================
-- Gap 1: World-Space 3D Volumetric Light Ray Entity Factory (anim/lightrays.zip)
-- =============================================================================
local function SpawnLocalLightRay(wx, wz, gx, gz)
    local create = rawget(G, "CreateEntity")
    if type(create) ~= "function" then return nil end
    local inst = create()
    if inst == nil or inst.entity == nil then return nil end

    if type(inst.AddTag) == "function" then
        inst:AddTag("FX")
        inst:AddTag("NOCLICK")
        inst:AddTag("NOBLOCK")
        inst:AddTag("ignorewalkableplatforms")
    end
    inst.persists = false
    if type(inst.entity.SetCanSleep) == "function" then
        inst.entity:SetCanSleep(false)
    end
    if type(inst.entity.AddTransform) == "function" then inst.entity:AddTransform() end
    if type(inst.entity.AddAnimState) ~= "function" then
        if type(inst.Remove) == "function" then pcall(inst.Remove, inst) end
        return nil
    end
    inst.entity:AddAnimState()

    if inst.Transform ~= nil then
        if type(inst.Transform.SetEightFaced) == "function" then inst.Transform:SetEightFaced() end
        if type(inst.Transform.SetPosition) == "function" then inst.Transform:SetPosition(wx, 0, wz) end
        if type(inst.Transform.SetRotation) == "function" then inst.Transform:SetRotation(45) end
    end

    local anim = inst.AnimState
    if anim ~= nil then
        if type(anim.SetBank) == "function" then anim:SetBank("lightrays") end
        if type(anim.SetBuild) == "function" then anim:SetBuild("lightrays") end
        if type(anim.PlayAnimation) == "function" then anim:PlayAnimation("idle_loop", true) end
        if type(anim.SetLightOverride) == "function" then anim:SetLightOverride(1) end
        if type(anim.SetFinalOffset) == "function" then anim:SetFinalOffset(1) end
        local sc = 0.92 + CellHash(gx, gz, 11) * 0.28
        if type(anim.SetScale) == "function" then anim:SetScale(sc, sc, sc) end
        if type(anim.Hide) == "function" and type(anim.Show) == "function" then
            for i = 1, 11 do
                anim:Hide("lightray" .. i)
            end
            local pick1 = math.floor(CellHash(gx, gz, 17) * 11) + 1
            local pick2 = ((pick1 + math.floor(CellHash(gx, gz, 23) * 9) + 1) % 11) + 1
            anim:Show("lightray" .. pick1)
            anim:Show("lightray" .. pick2)
        end
        if type(anim.OverrideMultColour) == "function" then
            anim:OverrideMultColour(1.0, 0.95, 0.78, 0.0)
        end
    end
    if type(inst.entity.CallPrefabConstructionComplete) == "function" then
        inst.entity:CallPrefabConstructionComplete()
    end
    return inst
end

local function ClearAllRays()
    for k, node in pairs(ray_nodes) do
        if node and IsValidEnt(node.ent) and type(node.ent.Remove) == "function" then
            pcall(node.ent.Remove, node.ent)
        end
        ray_nodes[k] = nil
    end
    stats.active_rays = 0
    stats.non_forest_rays = 0
end

local function ClearNonForestRays()
    local rem = 0
    local nf = 0
    for k, node in pairs(ray_nodes) do
        if node and node.non_forest then
            if IsValidEnt(node.ent) and type(node.ent.Remove) == "function" then
                pcall(node.ent.Remove, node.ent)
            end
            ray_nodes[k] = nil
        elseif node and IsValidEnt(node.ent) then
            rem = rem + 1
        else
            ray_nodes[k] = nil
        end
    end
    stats.active_rays = rem
    stats.non_forest_rays = nf
end

-- =============================================================================
-- Gap 2: Native C++ ShadeRenderer Setup (Cloud Shadows + Forest Canopy Shade)
--   CRITICAL SAFETY INVARIANT:
--     1. Klei's Lua_SetShadeEnabled @ 0x14001b1e0 reads arg 1 (shadeTypeId, always
--        truthy!) instead of arg 2 (enabled bool), and ShadeRenderer::Render calls
--        RenderDevice::SetTexture(0, texHandle) BEFORE checking 0-quads.
--     2. Therefore disabling/unloading MUST call SR:SetShadeTexture(id, "") so
--        C++ texHandle becomes -1 (SetTexture returns safely without NULL deref!).
--     3. Never allocate/bind ShadeTypes in Main Menu (TheWorld == nil) and reuse
--        persistent IDs across SimReset via Settings._hakimi_shade_types.
-- =============================================================================
local function DisarmShadeType(SR, shade_type, is_cloud)
    if SR ~= nil and shade_type ~= nil then
        if type(SR.SetShadeStrength) == "function" then pcall(SR.SetShadeStrength, SR, shade_type, 0.0) end
        if type(SR.SetShadeEnabled) == "function" then pcall(SR.SetShadeEnabled, SR, shade_type, false) end
        if type(SR.SetShadeTexture) == "function" then pcall(SR.SetShadeTexture, SR, shade_type, "") end
    end
    if is_cloud then
        cloud_tex_bound = false
    else
        canopy_tex_bound = false
    end
end

local function ArmCloudTexture(SR)
    if not IsWorldReady() or SR == nil or cloud_shade_type == nil or cloud_tex_bound then
        return cloud_tex_bound
    end
    if type(SR.SetShadeTexture) == "function" then
        local ok = pcall(SR.SetShadeTexture, SR, cloud_shade_type, ResPath("images/hs_cloud_shade.tex"))
        if ok then
            cloud_tex_bound = true
        end
    end
    return cloud_tex_bound
end

local function ArmCanopyTexture(SR)
    if not IsWorldReady() or SR == nil or canopy_shade_type == nil or canopy_tex_bound then
        return canopy_tex_bound
    end
    if type(SR.SetShadeTexture) == "function" then
        local ok = pcall(SR.SetShadeTexture, SR, canopy_shade_type, "images/tree.tex")
        if ok then
            canopy_tex_bound = true
        end
    end
    return canopy_tex_bound
end

local function BindWorldTeardown()
    local world = rawget(G, "TheWorld")
    if world ~= nil and world ~= bound_world and type(world.ListenForEvent) == "function" then
        bound_world = world
        pcall(world.ListenForEvent, world, "onremove", function()
            if type(HS_VOLLIGHT) == "table" and type(HS_VOLLIGHT.Shutdown) == "function" then
                pcall(HS_VOLLIGHT.Shutdown)
            end
        end)
    end
end

local function EnsureShadeTypes()
    if not IsWorldReady() then return nil end
    BindWorldTeardown()
    local SR = rawget(G, "ShadeRenderer")
    if SR == nil or type(SR.CreateShadeType) ~= "function" then return nil end

    local settings = rawget(G, "Settings")
    local saved = (type(settings) == "table" and type(settings._hakimi_shade_types) == "table")
        and settings._hakimi_shade_types or nil

    if cloud_shade_type == nil then
        local id = nil
        if saved ~= nil and type(saved.cloud) == "number" then
            id = saved.cloud
        else
            local ok, created = pcall(SR.CreateShadeType, SR)
            if ok and created ~= nil then id = created end
        end
        if id ~= nil then
            cloud_shade_type = id
            pcall(function()
                -- Note: Klei ShadeRenderer stores (1.0 / speed) and advances phase by (dt / speed),
                -- so larger values = SLOWER cycle period in seconds (vanilla canopy uses 5.0s).
                if type(SR.SetShadeMaxTranslation) == "function" then SR:SetShadeMaxTranslation(id, 18.0) end
                if type(SR.SetShadeTranslationSpeed) == "function" then SR:SetShadeTranslationSpeed(id, 55.0) end
                if type(SR.SetShadeMaxRotation) == "function" then SR:SetShadeMaxRotation(id, 4.0) end
                if type(SR.SetShadeRotationSpeed) == "function" then SR:SetShadeRotationSpeed(id, 90.0) end
                if type(SR.SetShadeMaxScale) == "function" then SR:SetShadeMaxScale(id, 1.8) end
                if type(SR.SetShadeScaleSpeed) == "function" then SR:SetShadeScaleSpeed(id, 70.0) end
                if cfg.cloud_shade > 0.005 then
                    ArmCloudTexture(SR)
                else
                    DisarmShadeType(SR, id, true)
                end
            end)
        end
    end

    if canopy_shade_type == nil then
        local id = nil
        if saved ~= nil and type(saved.canopy) == "number" then
            id = saved.canopy
        else
            local ok, created = pcall(SR.CreateShadeType, SR)
            if ok and created ~= nil then id = created end
        end
        if id ~= nil then
            canopy_shade_type = id
            pcall(function()
                if type(SR.SetShadeMaxRotation) == "function" then SR:SetShadeMaxRotation(id, 14.0) end
                if type(SR.SetShadeRotationSpeed) == "function" then SR:SetShadeRotationSpeed(id, 6.5) end
                if type(SR.SetShadeMaxTranslation) == "function" then SR:SetShadeMaxTranslation(id, 0.75) end
                if type(SR.SetShadeTranslationSpeed) == "function" then SR:SetShadeTranslationSpeed(id, 6.0) end
                if cfg.canopy_shade > 0.005 then
                    ArmCanopyTexture(SR)
                else
                    DisarmShadeType(SR, id, false)
                end
            end)
        end
    end

    if type(settings) == "table" and (cloud_shade_type ~= nil or canopy_shade_type ~= nil) then
        settings._hakimi_shade_types = { cloud = cloud_shade_type, canopy = canopy_shade_type }
    end

    return SR
end

local function ClearShadeHandles(tbl, shade_type)
    local SR = rawget(G, "ShadeRenderer")
    for k, rec in pairs(tbl) do
        if SR ~= nil and shade_type ~= nil and rec and rec.handle ~= nil and type(SR.RemoveShade) == "function" then
            pcall(SR.RemoveShade, SR, shade_type, rec.handle)
        end
        tbl[k] = nil
    end
end

-- =============================================================================
-- Controller Entity (Lightweight 15Hz local tick, auto-recreated per world)
-- =============================================================================
local function EnsureController()
    if IsDedicated() then return false end
    if IsValidEnt(ctrl_ent) then return true end
    local create = rawget(G, "CreateEntity")
    if type(create) ~= "function" then return false end
    local inst = create()
    if inst == nil or inst.entity == nil then return false end
    if type(inst.AddTag) == "function" then
        inst:AddTag("FX")
        inst:AddTag("NOCLICK")
    end
    inst.persists = false
    if type(inst.entity.SetCanSleep) == "function" then inst.entity:SetCanSleep(false) end
    if type(inst.DoPeriodicTask) == "function" then
        local frames = rawget(G, "FRAMES") or (1.0 / 30.0)
        inst:DoPeriodicTask(2 * frames, function()
            if type(HS_VOLLIGHT) == "table" and type(HS_VOLLIGHT.StepUpdate) == "function" then
                HS_VOLLIGHT.StepUpdate(2 * frames)
            end
        end)
    end
    if type(inst.entity.CallPrefabConstructionComplete) == "function" then
        inst.entity:CallPrefabConstructionComplete()
    end
    ctrl_ent = inst
    return true
end

-- =============================================================================
-- Spatial Grid Streaming (Runs at ~3Hz or on >=3-tile player displacement)
-- =============================================================================
local function StreamSpatialGrids(px, pz, world, is_cave)
    -- 1. Forest & Cave 3D Volumetric Light Rays (12x12 tile cells, max 14 active)
    if cfg.lightrays <= 0.005 then
        if stats.active_rays > 0 then ClearAllRays() end
    else
        local CELL = 12
        local cpx = math.floor(px / CELL)
        local cpz = math.floor(pz / CELL)
        local keep = {}
        local count = 0
        local nf_count = 0
        local max_rays = cfg.lightrays_all and 18 or 14

        -- Evict distant, invalid, or disabled non-forest ray entities
        for k, node in pairs(ray_nodes) do
            if not (node and IsValidEnt(node.ent)) then
                ray_nodes[k] = nil
            elseif node.non_forest and (not cfg.lightrays_all or is_cave) then
                if type(node.ent.Remove) == "function" then pcall(node.ent.Remove, node.ent) end
                ray_nodes[k] = nil
            else
                local dx, dz = node.wx - px, node.wz - pz
                if dx * dx + dz * dz > 1764.0 then -- > 42 tiles
                    if type(node.ent.Remove) == "function" then pcall(node.ent.Remove, node.ent) end
                    ray_nodes[k] = nil
                else
                    keep[k] = true
                    count = count + 1
                    if node.non_forest then nf_count = nf_count + 1 end
                end
            end
        end

        -- Populate deterministic forest / cave / optional all-turf base cells within [-3..3]
        if count < max_rays then
            for dz_c = -3, 3 do
                for dx_c = -3, 3 do
                    if count >= max_rays then break end
                    local gx, gz = cpx + dx_c, cpz + dz_c
                    local key = CellKey(gx, gz)
                    if not keep[key] then
                        local wx = gx * CELL + 2.0 + CellHash(gx, gz, 1) * (CELL - 4.0)
                        local wz = gz * CELL + 2.0 + CellHash(gx, gz, 2) * (CELL - 4.0)
                        local ddx, ddz = wx - px, wz - pz
                        if ddx * ddx + ddz * ddz <= 1369.0 then -- <= 37 tiles
                            local tile = GetTileAt(world, wx, wz)
                            local prob = 0.0
                            local is_nf = false
                            if is_cave then
                                if IsCaveRayTile(tile) then prob = 0.24 end
                            else
                                if IsForestTile(tile) then
                                    prob = 0.48
                                elseif cfg.lightrays_all and IsWalkableSurfaceLand(world, wx, wz, tile) then
                                    is_nf = true
                                    prob = HasNearbyLivingTree(wx, wz) and 0.54 or 0.22
                                end
                            end
                            if prob > 0.0 and CellHash(gx, gz, 3) < prob then
                                local ent = SpawnLocalLightRay(wx, wz, gx, gz)
                                if ent ~= nil then
                                    ray_nodes[key] = {
                                        ent = ent,
                                        wx = wx,
                                        wz = wz,
                                        fade = 0.0,
                                        non_forest = is_nf,
                                        base_gain = 0.80 + CellHash(gx, gz, 7) * 0.35,
                                    }
                                    count = count + 1
                                    if is_nf then nf_count = nf_count + 1 end
                                end
                            end
                        end
                    end
                end
            end
        end
        stats.active_rays = count
        stats.non_forest_rays = nf_count
    end

    -- 2A. Dynamic Drifting Cloud Shadows (Surface only, 36x36 large cells)
    local SR = nil
    if (cfg.cloud_shade > 0.005 or cfg.canopy_shade > 0.005) then
        SR = EnsureShadeTypes()
    end

    if cfg.cloud_shade <= 0.005 or is_cave or SR == nil or cloud_shade_type == nil then
        if stats.active_clouds > 0 then
            ClearShadeHandles(cloud_handles, cloud_shade_type)
            stats.active_clouds = 0
        end
        DisarmShadeType(SR, cloud_shade_type, true)
    else
        ArmCloudTexture(SR)
        local CCELL = 36
        local cpx = math.floor(px / CCELL)
        local cpz = math.floor(pz / CCELL)
        local ccount = 0
        for k, rec in pairs(cloud_handles) do
            local dx, dz = rec.wx - px, rec.wz - pz
            if dx * dx + dz * dz > 10000.0 then -- > 100 tiles
                if type(SR.RemoveShade) == "function" then
                    pcall(SR.RemoveShade, SR, cloud_shade_type, rec.handle)
                end
                cloud_handles[k] = nil
            else
                ccount = ccount + 1
            end
        end
        for dz_c = -2, 2 do
            for dx_c = -2, 2 do
                local gx, gz = cpx + dx_c, cpz + dz_c
                local key = CellKey(gx, gz)
                if cloud_handles[key] == nil and CellHash(gx, gz, 31) < 0.62 then
                    local wx = gx * CCELL + CellHash(gx, gz, 33) * CCELL
                    local wz = gz * CCELL + CellHash(gx, gz, 37) * CCELL
                    local rot = CellHash(gx, gz, 41) * 360.0
                    local sc = 18.0 + CellHash(gx, gz, 43) * 11.0
                    if type(SR.SpawnShade) == "function" then
                        local ok, h = pcall(SR.SpawnShade, SR, cloud_shade_type, wx, wz, rot, sc)
                        if ok and h ~= nil then
                            cloud_handles[key] = { handle = h, wx = wx, wz = wz }
                            ccount = ccount + 1
                        end
                    end
                end
            end
        end
        stats.active_clouds = ccount
    end

    -- 2B. Deep-Forest Dappled Canopy Shade (Surface Forest/Deciduous, 8x8 cells)
    if cfg.canopy_shade <= 0.005 or is_cave or SR == nil or canopy_shade_type == nil then
        if stats.active_canopy > 0 then
            ClearShadeHandles(canopy_handles, canopy_shade_type)
            stats.active_canopy = 0
        end
        DisarmShadeType(SR, canopy_shade_type, false)
    else
        ArmCanopyTexture(SR)
        local FCELL = 8
        local cpx = math.floor(px / FCELL)
        local cpz = math.floor(pz / FCELL)
        local fcount = 0
        for k, rec in pairs(canopy_handles) do
            local dx, dz = rec.wx - px, rec.wz - pz
            if dx * dx + dz * dz > 1600.0 then -- > 40 tiles
                if type(SR.RemoveShade) == "function" then
                    pcall(SR.RemoveShade, SR, canopy_shade_type, rec.handle)
                end
                canopy_handles[k] = nil
            else
                fcount = fcount + 1
            end
        end
        for dz_c = -4, 4 do
            for dx_c = -4, 4 do
                if fcount >= 48 then break end
                local gx, gz = cpx + dx_c, cpz + dz_c
                local key = CellKey(gx, gz)
                if canopy_handles[key] == nil and CellHash(gx, gz, 53) < 0.54 then
                    local wx = gx * FCELL + 1.5 + CellHash(gx, gz, 59) * (FCELL - 3.0)
                    local wz = gz * FCELL + 1.5 + CellHash(gx, gz, 61) * (FCELL - 3.0)
                    local ddx, ddz = wx - px, wz - pz
                    if ddx * ddx + ddz * ddz <= 1156.0 and IsForestTile(GetTileAt(world, wx, wz)) then
                        local rot = CellHash(gx, gz, 67) * 360.0
                        local sc = 3.2 + CellHash(gx, gz, 71) * 1.5
                        if type(SR.SpawnShade) == "function" then
                            local ok, h = pcall(SR.SpawnShade, SR, canopy_shade_type, wx, wz, rot, sc)
                            if ok and h ~= nil then
                                canopy_handles[key] = { handle = h, wx = wx, wz = wz }
                                fcount = fcount + 1
                            end
                        end
                    end
                end
            end
        end
        stats.active_canopy = fcount
    end
end

-- =============================================================================
-- Public API (HS_VOLLIGHT table in mod environment)
-- =============================================================================
HS_VOLLIGHT = {}

function HS_VOLLIGHT.ApplyConfig(lightrays, cloud_shade, canopy_shade, edge_fog, ground_fog, lightrays_all)
    cfg.lightrays    = Clamp(lightrays or 0.0, 0.0, 1.5)
    cfg.cloud_shade  = Clamp(cloud_shade or 0.0, 0.0, 1.5)
    cfg.canopy_shade = Clamp(canopy_shade or 0.0, 0.0, 1.5)
    if edge_fog ~= nil then
        cfg.edge_fog = Clamp(edge_fog, 0.0, 1.5)
    end
    if ground_fog ~= nil then
        cfg.ground_fog = Clamp(ground_fog, 0.0, 1.5)
    end
    if lightrays_all ~= nil then
        local want_all = (lightrays_all == true) or (type(lightrays_all) == "number" and lightrays_all > 0.5)
        if cfg.lightrays_all and not want_all then
            ClearNonForestRays()
        end
        cfg.lightrays_all = want_all
    end

    local any_on = (cfg.lightrays > 0.005 or cfg.cloud_shade > 0.005 or cfg.canopy_shade > 0.005
        or cfg.edge_fog > 0.005 or cfg.ground_fog > 0.005)
    if not any_on then
        ClearAllRays()
        ClearShadeHandles(cloud_handles, cloud_shade_type)
        ClearShadeHandles(canopy_handles, canopy_shade_type)
        DestroyFogHost()
        stats.active_clouds = 0
        stats.active_canopy = 0
        cur_ray_alpha = 0.0
        cur_cloud_str = 0.0
        cur_canopy_str = 0.0
        stats.ray_alpha = 0.0
        stats.cloud_str = 0.0
        stats.canopy_str = 0.0
        local SR = rawget(G, "ShadeRenderer")
        DisarmShadeType(SR, cloud_shade_type, true)
        DisarmShadeType(SR, canopy_shade_type, false)
        return
    end

    if cfg.cloud_shade <= 0.005 then
        ClearShadeHandles(cloud_handles, cloud_shade_type)
        stats.active_clouds = 0
        cur_cloud_str = 0.0
        stats.cloud_str = 0.0
        DisarmShadeType(rawget(G, "ShadeRenderer"), cloud_shade_type, true)
    end
    if cfg.canopy_shade <= 0.005 then
        ClearShadeHandles(canopy_handles, canopy_shade_type)
        stats.active_canopy = 0
        cur_canopy_str = 0.0
        stats.canopy_str = 0.0
        DisarmShadeType(rawget(G, "ShadeRenderer"), canopy_shade_type, false)
    end

    if cfg.edge_fog <= 0.005 and cfg.ground_fog <= 0.005 then
        DestroyFogHost()
    end

    if IsWorldReady() then
        BindWorldTeardown()
        if (cfg.cloud_shade > 0.005 or cfg.canopy_shade > 0.005) and not shade_globally_nudged then
            local SR = EnsureShadeTypes()
            if SR ~= nil and type(SR.Enable) == "function" then
                pcall(SR.Enable, SR, true)
                shade_globally_nudged = true
            end
        end
        EnsureController()
        stream_timer = 99.0 -- immediate refresh on slider change
    end
end

function HS_VOLLIGHT.GetStats()
    return stats
end

function HS_VOLLIGHT.StepUpdate(dt)
    if IsDedicated() then return end
    local any_on = (cfg.lightrays > 0.005 or cfg.cloud_shade > 0.005 or cfg.canopy_shade > 0.005
        or cfg.edge_fog > 0.005 or cfg.ground_fog > 0.005)
    if not any_on then return end

    local player = rawget(G, "ThePlayer")
    if not IsValidEnt(player) then return end
    local px, _, pz = SafePos(player)
    if px == nil then return end

    if type(dt) ~= "number" or dt <= 0 then dt = 0.066 end
    if dt > 0.25 then dt = 0.25 end

    local world = rawget(G, "TheWorld")
    local wstate = (world ~= nil and type(world.state) == "table") and world.state or nil
    local is_cave = (world ~= nil and type(world.HasTag) == "function" and world:HasTag("cave")) == true

    -- Periodic or displacement-triggered spatial grid streaming (~3Hz)
    stream_timer = stream_timer + dt
    local moved_far = (last_stream_x == nil)
        or ((px - last_stream_x) * (px - last_stream_x) + (pz - last_stream_z) * (pz - last_stream_z) >= 9.0)
    if stream_timer >= 0.33 or moved_far then
        stream_timer = 0.0
        last_stream_x, last_stream_z = px, pz
        StreamSpatialGrids(px, pz, world, is_cave)
    end

    -- Time-of-day & weather lighting targets
    local is_dusk     = (wstate ~= nil and wstate.isdusk == true)
    local is_night    = (wstate ~= nil and wstate.isnight == true)
    local is_fullmoon = (wstate ~= nil and wstate.isfullmoon == true)
    local is_rain     = (wstate ~= nil and wstate.israining == true)
    local is_day      = (not is_dusk) and (not is_night)

    local blend = math.min(1.0, dt * 3.5)

    -- 1. Update 3D Volumetric Light Ray color & proximity-faded alpha
    local tgt_ra, tr, tg, tb = 0.0, 1.0, 0.95, 0.78
    if cfg.lightrays > 0.005 then
        if is_cave then
            if is_day then
                tgt_ra, tr, tg, tb = 0.36 * cfg.lightrays, 0.70, 0.92, 0.78
            elseif is_dusk or is_fullmoon then
                tgt_ra, tr, tg, tb = 0.28 * cfg.lightrays, 0.56, 0.80, 1.00
            else
                tgt_ra = 0.0
            end
        else
            if is_day then
                tgt_ra, tr, tg, tb = (is_rain and 0.16 or 0.48) * cfg.lightrays, 1.00, 0.95, 0.78
            elseif is_dusk then
                tgt_ra, tr, tg, tb = (is_rain and 0.10 or 0.36) * cfg.lightrays, 1.00, 0.74, 0.44
            elseif is_fullmoon then
                tgt_ra, tr, tg, tb = (is_rain and 0.12 or 0.44) * cfg.lightrays, 0.66, 0.86, 1.00
            else
                tgt_ra = 0.0
            end
        end
    end
    tgt_ra = Clamp(tgt_ra, 0.0, 0.78)
    cur_ray_alpha = cur_ray_alpha + (tgt_ra - cur_ray_alpha) * blend
    cur_ray_r     = cur_ray_r + (tr - cur_ray_r) * blend
    cur_ray_g     = cur_ray_g + (tg - cur_ray_g) * blend
    cur_ray_b     = cur_ray_b + (tb - cur_ray_b) * blend
    stats.ray_alpha = cur_ray_alpha

    local cam = rawget(G, "TheCamera")
    local cam_down = (cam ~= nil and type(cam.GetDownVec) == "function") and cam:GetDownVec() or nil
    local cam_pos = (cam ~= nil and type(cam.currentpos) == "table") and cam.currentpos or nil

    for k, node in pairs(ray_nodes) do
        if not (node and IsValidEnt(node.ent)) then
            ray_nodes[k] = nil
        else
            local dx, dz = node.wx - px, node.wz - pz
            local dist = math.sqrt(dx * dx + dz * dz)
            -- Gentle near-player clear zone (never blinds player) + smooth far horizon fade
            local near_f = (dist < 5.0) and (0.35 + 0.65 * (dist / 5.0)) or 1.0
            local far_f  = (dist > 26.0) and Clamp(1.0 - (dist - 26.0) / 11.0, 0.0, 1.0) or 1.0
            -- Match official distancefade camera-down soft occlusion when very close to lens
            local cam_f = 1.0
            if cam_down ~= nil and cam_pos ~= nil and type(cam_pos.x) == "number" and type(cam_down.x) == "number" then
                local mody = (node.wx - cam_pos.x) * cam_down.x + (node.wz - cam_pos.z) * cam_down.z
                if mody > 16.0 then
                    cam_f = Clamp(1.0 - (mody - 16.0) / 14.0, 0.0, 1.0)
                end
            end
            local tgt_node_f = near_f * far_f * cam_f
            node.fade = node.fade + (tgt_node_f - node.fade) * math.min(1.0, dt * 5.0)
            local a = Clamp(cur_ray_alpha * node.fade * node.base_gain, 0.0, 0.85)
            local anim = node.ent.AnimState
            if anim ~= nil and type(anim.OverrideMultColour) == "function" then
                anim:OverrideMultColour(cur_ray_r, cur_ray_g, cur_ray_b, a)
            end
            if a <= 0.008 then
                if type(node.ent.Hide) == "function" then node.ent:Hide() end
            else
                if type(node.ent.Show) == "function" then node.ent:Show() end
            end
        end
    end

    -- 2. Update Native C++ ShadeRenderer strengths (Cloud Shadows + Forest Canopy)
    local SR = rawget(G, "ShadeRenderer")
    if SR ~= nil then
        local tgt_cloud = 0.0
        if cfg.cloud_shade > 0.005 and (not is_cave) then
            if is_day then
                tgt_cloud = (is_rain and 0.10 or 0.24) * cfg.cloud_shade
            elseif is_dusk then
                tgt_cloud = (is_rain and 0.06 or 0.15) * cfg.cloud_shade
            elseif is_fullmoon then
                tgt_cloud = (is_rain and 0.05 or 0.14) * cfg.cloud_shade
            end
        end
        tgt_cloud = Clamp(tgt_cloud, 0.0, 0.36)
        cur_cloud_str = cur_cloud_str + (tgt_cloud - cur_cloud_str) * blend
        stats.cloud_str = cur_cloud_str
        if cloud_shade_type ~= nil then
            local en_c = cur_cloud_str > 0.004 and stats.active_clouds > 0
            if type(SR.SetShadeEnabled) == "function" then pcall(SR.SetShadeEnabled, SR, cloud_shade_type, en_c) end
            if type(SR.SetShadeStrength) == "function" then pcall(SR.SetShadeStrength, SR, cloud_shade_type, cur_cloud_str) end
        end

        local tgt_canopy = 0.0
        if cfg.canopy_shade > 0.005 and (not is_cave) then
            if is_day then
                tgt_canopy = (is_rain and 0.10 or 0.22) * cfg.canopy_shade
            elseif is_dusk then
                tgt_canopy = 0.12 * cfg.canopy_shade
            elseif is_fullmoon then
                tgt_canopy = 0.12 * cfg.canopy_shade
            end
        end
        tgt_canopy = Clamp(tgt_canopy, 0.0, 0.34)
        cur_canopy_str = cur_canopy_str + (tgt_canopy - cur_canopy_str) * blend
        stats.canopy_str = cur_canopy_str
        if canopy_shade_type ~= nil then
            local en_f = cur_canopy_str > 0.004 and stats.active_canopy > 0
            if type(SR.SetShadeEnabled) == "function" then pcall(SR.SetShadeEnabled, SR, canopy_shade_type, en_f) end
            if type(SR.SetShadeStrength) == "function" then pcall(SR.SetShadeStrength, SR, canopy_shade_type, cur_canopy_str) end
        end
    end

    -- 3. Horizontal Volumetric Fog (World-Edge Void Mist + Lightmap-Lit Forest/Marsh/Cave Ground Fog)
    StepVolumetricFog(px, pz, world, wstate, is_cave, dt)
end

function HS_VOLLIGHT.GetPersistentShadeIds()
    local c = cloud_shade_type
    local f = canopy_shade_type
    if c == nil or f == nil then
        local settings = rawget(G, "Settings")
        local saved = (type(settings) == "table" and type(settings._hakimi_shade_types) == "table")
            and settings._hakimi_shade_types or nil
        if saved ~= nil then
            if c == nil and type(saved.cloud) == "number" then c = saved.cloud end
            if f == nil and type(saved.canopy) == "number" then f = saved.canopy end
        end
    end
    if c ~= nil or f ~= nil then
        return { cloud = c, canopy = f }
    end
    return nil
end

function HS_VOLLIGHT.Shutdown()
    ClearAllRays()
    ClearShadeHandles(cloud_handles, cloud_shade_type)
    ClearShadeHandles(canopy_handles, canopy_shade_type)
    DestroyFogHost()
    stats.active_rays = 0
    stats.non_forest_rays = 0
    stats.active_clouds = 0
    stats.active_canopy = 0
    stats.ray_alpha = 0.0
    stats.cloud_str = 0.0
    stats.canopy_str = 0.0
    cur_ray_alpha = 0.0
    cur_cloud_str = 0.0
    cur_canopy_str = 0.0
    edge_fog_acc = 0.0
    ground_fog_acc = 0.0
    if IsValidEnt(ctrl_ent) and type(ctrl_ent.Remove) == "function" then
        pcall(ctrl_ent.Remove, ctrl_ent)
    end
    ctrl_ent = nil
    bound_world = nil
    shade_globally_nudged = false
    last_stream_x, last_stream_z = nil, nil
    local SR = rawget(G, "ShadeRenderer")
    DisarmShadeType(SR, cloud_shade_type, true)
    DisarmShadeType(SR, canopy_shade_type, false)
end

-- Auto-start controller when local player enters world (surface or cave)
if not IsDedicated() and type(AddPlayerPostInit) == "function" then
    AddPlayerPostInit(function(inst)
        if inst ~= nil and type(inst.DoTaskInTime) == "function" then
            inst:DoTaskInTime(0.25, function()
                local tp = rawget(G, "ThePlayer")
                if tp ~= nil and tp == inst then
                    BindWorldTeardown()
                    if (cfg.cloud_shade > 0.005 or cfg.canopy_shade > 0.005) and not shade_globally_nudged then
                        local SR = EnsureShadeTypes()
                        if SR ~= nil and type(SR.Enable) == "function" then
                            pcall(SR.Enable, SR, true)
                            shade_globally_nudged = true
                        end
                    end
                    if cfg.lightrays > 0.005 or cfg.cloud_shade > 0.005 or cfg.canopy_shade > 0.005
                        or cfg.edge_fog > 0.005 or cfg.ground_fog > 0.005 then
                        EnsureController()
                        stream_timer = 99.0
                    end
                end
            end)
        end
    end)
end
