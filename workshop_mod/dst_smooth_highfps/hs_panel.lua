-- =============================================================================
-- HyperSmooth v2.6.0 Sleek Klei-Native In-Game Live Customizer Side-Panel
--   - Right-docked translucent overlay inside PlayerHUD (never pauses world or
--     blocks center viewport: walk, zoom & preview 0ms live changes on the fly!)
--   - Pure core DST widgets (images/global.xml + images/global_redux.xml)
--   - Strict Player-First UX: 9 independent switches + 1-click Vanilla Restore!
-- =============================================================================

local G = GLOBAL
local pcall = G.pcall
local rawget = G.rawget
local type = G.type
local tostring = G.tostring
local string = G.string
local math = G.math

local ok_req, Widget, Image, ImageButton, Text = pcall(function()
    return G.require("widgets/widget"),
           G.require("widgets/image"),
           G.require("widgets/imagebutton"),
           G.require("widgets/text")
end)
if not (ok_req and Widget and Image and ImageButton and Text) then
    return
end

local panel_root = nil
local panel_visible = false
local panel_refresh_fn = nil
local panel_telemetry_fn = nil

local function IsChineseLocale()
    local opt_str = G.STRINGS and G.STRINGS.UI and G.STRINGS.UI.OPTIONS
    return not (opt_str and opt_str.ENABLED == "Enabled")
end

local function CreateLiveCustomizerPanel(hud)
    local api = HS_PanelApi
    if type(api) ~= "table" or not hud or not hud.AddChild then
        return nil
    end
    local is_zh = IsChineseLocale()
    local FONT_BODY = G.BODYTEXTFONT or G.UIFONT or G.NUMBERFONT
    local FONT_NUM  = G.NUMBERFONT or FONT_BODY

    local root = hud:AddChild(Widget("HyperSmoothLivePanel"))
    root:SetHAnchor(G.ANCHOR_RIGHT)
    root:SetVAnchor(G.ANCHOR_MIDDLE)
    if G.SCALEMODE_PROPORTIONAL and root.SetScaleMode then
        root:SetScaleMode(G.SCALEMODE_PROPORTIONAL)
    end
    root:Hide()

    local W, H = 368, 580
    -- Position inner child container inside SCALEMODE_PROPORTIONAL anchor so offsets use
    -- resolution-independent virtual 720p units instead of unscaled raw screen pixels.
    local panel = root:AddChild(Widget("HyperSmoothLivePanelBody"))
    panel:SetPosition(-232, -20, 0)

    -- Subtle warm charcoal translucent backdrop + crisp border frame
    local border = panel:AddChild(Image("images/global.xml", "square.tex"))
    border:ScaleToSize(W + 4, H + 4)
    border:SetTint(0.76, 0.62, 0.34, 0.62)

    local bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    bg:ScaleToSize(W, H)
    bg:SetTint(0.07, 0.07, 0.09, 0.88)

    -- Header bar
    local title = panel:AddChild(Text(FONT_BODY, 23, is_zh and "HyperSmooth v2.6.0 实时微调" or "HyperSmooth v2.6.0 Live Panel"))
    title:SetPosition(-18, H * 0.5 - 24, 0)
    title:SetColour(0.96, 0.85, 0.56, 1.0)

    local close_btn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    close_btn.scale_on_focus = false
    close_btn.move_on_click = false
    close_btn:ForceImageSize(34, 28)
    close_btn:SetPosition(W * 0.5 - 24, H * 0.5 - 24, 0)
    close_btn:SetText("X")
    close_btn:SetTextSize(20)
    close_btn:SetFont(FONT_NUM)
    close_btn:SetTextColour(0.95, 0.55, 0.55, 1.0)
    close_btn:SetTextFocusColour(1.0, 0.78, 0.78, 1.0)
    close_btn:SetOnClick(function()
        if type(HS_TogglePanel) == "function" then
            HS_TogglePanel(false)
        end
    end)

    -- Native bin64/dinput8.dll Version & Update Alert Strip ("更新条")
    local dll_bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    dll_bg:ScaleToSize(W - 22, 26)
    dll_bg:SetPosition(0, H * 0.5 - 55, 0)
    dll_bg:SetTint(0.08, 0.18, 0.13, 0.86)

    local dll_txt = panel:AddChild(Text(FONT_BODY, 15, ""))
    dll_txt:SetRegionSize(W - 30, 24)
    dll_txt:SetPosition(0, H * 0.5 - 55, 0)
    dll_txt:SetColour(0.50, 1.0, 0.68, 0.96)
    dll_txt:SetClickable(false)

    -- Live Telemetry Strip (2 compact lines)
    local tele_bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    tele_bg:ScaleToSize(W - 22, 46)
    tele_bg:SetPosition(0, H * 0.5 - 96, 0)
    tele_bg:SetTint(0.03, 0.04, 0.06, 0.72)

    local tele1 = panel:AddChild(Text(FONT_NUM, 16, ""))
    tele1:SetPosition(0, H * 0.5 - 86, 0)
    tele1:SetColour(0.50, 1.0, 0.68, 0.96)

    local tele2 = panel:AddChild(Text(FONT_NUM, 15, ""))
    tele2:SetPosition(0, H * 0.5 - 106, 0)
    tele2:SetColour(0.78, 0.86, 0.96, 0.92)

    -- 4 Quick Preset Pills
    local preset_defs = is_zh and {
        { id = 0, label = "原版纯净" },
        { id = 1, label = "护眼高清" },
        { id = 2, label = "柔和绘本" },
        { id = 3, label = "自定义" },
    } or {
        { id = 0, label = "Vanilla" },
        { id = 1, label = "EyeHD" },
        { id = 2, label = "Story" },
        { id = 3, label = "Custom" },
    }
    local preset_btns = {}
    local pill_w = 82
    local start_x = -((#preset_defs - 1) * (pill_w + 5)) * 0.5
    local preset_y = H * 0.5 - 140
    for i = 1, #preset_defs do
        local pd = preset_defs[i]
        local px = start_x + (i - 1) * (pill_w + 5)
        local pbg = panel:AddChild(Image("images/global.xml", "square.tex"))
        pbg:ScaleToSize(pill_w, 28)
        pbg:SetPosition(px, preset_y, 0)
        pbg:SetTint(0.14, 0.14, 0.18, 0.90)

        local pb = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        pb.scale_on_focus = false
        pb.move_on_click = false
        pb:ForceImageSize(pill_w, 28)
        pb:SetPosition(px, preset_y, 0)
        pb:SetText(pd.label)
        pb:SetTextSize(17)
        pb:SetFont(FONT_BODY)
        pb:SetTextFocusColour(1.0, 0.96, 0.74, 1.0)
        pb:SetOnClick(function()
            api.SetPreset(pd.id)
            if panel_refresh_fn then panel_refresh_fn() end
        end)
        preset_btns[i] = { id = pd.id, bg = pbg, btn = pb }
    end

    -- 9 Independent Player-First Visual Toggles
    local vis_rows = is_zh and {
        { key = "af16",         label = "16× 各向异性过滤 (斜视角清晰)" },
        { key = "full_lm",      label = "全分辨率 1.0× 光照图 (0ms热切+防色带)" },
        { key = "mip_sharp",    label = "原生纹理防虚化 (Mip LOD -0.45)" },
        { key = "cas",          label = "轻度保真锐化与暗夜防色带 (CAS)" },
        { key = "ao",           label = "实体温和接地阴影与防爆白 (AO)" },
        { key = "env",          label = "柔和近岸与雨天湿土微调 (Env)" },
        { key = "soft_bloom",   label = "护眼防过曝柔光 Bloom (开泛光生效)" },
        { key = "tone",         label = "柔和绘本微通透与轻暗角 (Tone)" },
        { key = "dark_clarity", label = "暗夜/洞穴护眼层次 (31.5 LUT)" },
    } or {
        { key = "af16",         label = "16x Anisotropic Filtering" },
        { key = "full_lm",      label = "1.0x Full-Res LightMap (0ms Swap)" },
        { key = "mip_sharp",    label = "Native Mipmap Sharpness (-0.45)" },
        { key = "cas",          label = "Mild Fidelity CAS + Dither" },
        { key = "ao",           label = "Calm Entity Contact AO" },
        { key = "env",          label = "Soft Shoreline & Rain Wetness" },
        { key = "soft_bloom",   label = "Eye-Care Soft Bloom (Bloom ON)" },
        { key = "tone",         label = "Soft Storybook Midtone" },
        { key = "dark_clarity", label = "Dark/Cave Clarity + 31.5 LUT" },
    }

    local row_widgets = {}
    local top_row_y = H * 0.5 - 176
    local row_step = 29

    for i = 1, #vis_rows do
        local rv = vis_rows[i]
        local ry = top_row_y - (i - 1) * row_step

        local rbg = panel:AddChild(Image("images/global.xml", "square.tex"))
        rbg:ScaleToSize(W - 22, 25)
        rbg:SetPosition(0, ry, 0)
        rbg:SetTint(0.11, 0.12, 0.15, 0.72)

        local rbtn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        rbtn.scale_on_focus = false
        rbtn.move_on_click = false
        rbtn:ForceImageSize(W - 22, 25)
        rbtn:SetPosition(0, ry, 0)
        rbtn:SetOnClick(function()
            api.ToggleVisualFlag(rv.key)
            if panel_refresh_fn then panel_refresh_fn() end
        end)

        local rlbl = panel:AddChild(Text(FONT_BODY, 17, rv.label))
        rlbl:SetRegionSize(276, 24)
        rlbl:SetHAlign(G.ANCHOR_LEFT)
        rlbl:SetPosition(-28, ry, 0)
        rlbl:SetClickable(false)

        local rval = panel:AddChild(Text(FONT_NUM, 16, ""))
        rval:SetRegionSize(60, 24)
        rval:SetHAlign(G.ANCHOR_RIGHT)
        rval:SetPosition(136, ry, 0)
        rval:SetClickable(false)

        row_widgets[i] = { key = rv.key, bg = rbg, lbl = rlbl, val = rval }
    end

    -- Divider + 2 Core Engine Switches (240Hz Smoothness + Low Latency)
    local div = panel:AddChild(Image("images/global.xml", "square.tex"))
    div:ScaleToSize(W - 26, 2)
    div:SetPosition(0, top_row_y - #vis_rows * row_step + 4, 0)
    div:SetTint(0.76, 0.62, 0.34, 0.38)

    local eng_rows = is_zh and {
        { key = "skeletal", label = "240Hz 全场景补帧 (骨骼+粒子+光源+镜头)" },
        { key = "lowlat",   label = "D3D9Ex 1帧低延迟 + 240Hz 急甩点击防漏" },
    } or {
        { key = "skeletal", label = "240Hz Smooth (Anim+VFX+Light+Cam)" },
        { key = "lowlat",   label = "D3D9Ex 1-Frame Queue + Flick-Click" },
    }
    local eng_widgets = {}
    local eng_top_y = top_row_y - #vis_rows * row_step - 16
    for i = 1, #eng_rows do
        local ev = eng_rows[i]
        local ry = eng_top_y - (i - 1) * 30

        local rbg = panel:AddChild(Image("images/global.xml", "square.tex"))
        rbg:ScaleToSize(W - 22, 26)
        rbg:SetPosition(0, ry, 0)
        rbg:SetTint(0.10, 0.14, 0.17, 0.78)

        local rbtn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        rbtn.scale_on_focus = false
        rbtn.move_on_click = false
        rbtn:ForceImageSize(W - 22, 26)
        rbtn:SetPosition(0, ry, 0)
        rbtn:SetOnClick(function()
            if ev.key == "skeletal" then
                api.ToggleSkeletal()
            else
                api.ToggleLowLatency()
            end
            if panel_refresh_fn then panel_refresh_fn() end
        end)

        local rlbl = panel:AddChild(Text(FONT_BODY, 17, ev.label))
        rlbl:SetRegionSize(276, 24)
        rlbl:SetHAlign(G.ANCHOR_LEFT)
        rlbl:SetPosition(-28, ry, 0)
        rlbl:SetClickable(false)

        local rval = panel:AddChild(Text(FONT_NUM, 16, ""))
        rval:SetRegionSize(60, 24)
        rval:SetHAlign(G.ANCHOR_RIGHT)
        rval:SetPosition(136, ry, 0)
        rval:SetClickable(false)

        eng_widgets[i] = { key = ev.key, lbl = rlbl, val = rval }
    end

    -- Bottom Action Bar: 1-Click Vanilla Pure Restore + Hide
    local bot_y = -H * 0.5 + 26
    local reset_bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    reset_bg:ScaleToSize(188, 30)
    reset_bg:SetPosition(-76, bot_y, 0)
    reset_bg:SetTint(0.32, 0.24, 0.12, 0.92)

    local reset_btn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    reset_btn.scale_on_focus = false
    reset_btn.move_on_click = false
    reset_btn:ForceImageSize(188, 30)
    reset_btn:SetPosition(-76, bot_y, 0)
    reset_btn:SetText(is_zh and "一键恢复原版纯净" or "Restore Vanilla Pure")
    reset_btn:SetTextSize(18)
    reset_btn:SetFont(FONT_BODY)
    reset_btn:SetTextColour(1.0, 0.88, 0.58, 1.0)
    reset_btn:SetTextFocusColour(1.0, 0.96, 0.78, 1.0)
    reset_btn:SetOnClick(function()
        api.SetPreset(0)
        if panel_refresh_fn then panel_refresh_fn() end
    end)

    local hide_bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    hide_bg:ScaleToSize(134, 30)
    hide_bg:SetPosition(96, bot_y, 0)
    hide_bg:SetTint(0.16, 0.16, 0.20, 0.92)

    local hide_btn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    hide_btn.scale_on_focus = false
    hide_btn.move_on_click = false
    hide_btn:ForceImageSize(134, 30)
    hide_btn:SetPosition(96, bot_y, 0)
    hide_btn:SetText(is_zh and "隐藏面板" or "Close Panel")
    hide_btn:SetTextSize(18)
    hide_btn:SetFont(FONT_BODY)
    hide_btn:SetTextColour(0.85, 0.88, 0.92, 1.0)
    hide_btn:SetTextFocusColour(1.0, 1.0, 1.0, 1.0)
    hide_btn:SetOnClick(function()
        if type(HS_TogglePanel) == "function" then
            HS_TogglePanel(false)
        end
    end)

    local function RefreshTelemetry()
        local snap = api.GetSnapshot()
        local exp_ver = tostring(snap.expected_dll_ver or "2.6.0")
        if snap.dll_status == "ok" then
            dll_bg:SetTint(0.08, 0.20, 0.14, 0.86)
            dll_txt:SetColour(0.50, 1.0, 0.68, 0.96)
            dll_txt:SetString(is_zh
                and string.format("已直连 bin64/dinput8.dll v%s (配套版本一致)", tostring(snap.dll_ver or exp_ver))
                or string.format("bin64/dinput8.dll v%s Connected (Up to Date)", tostring(snap.dll_ver or exp_ver)))
        elseif snap.dll_status == "outdated" then
            dll_bg:SetTint(0.46, 0.24, 0.06, 0.94)
            dll_txt:SetColour(1.0, 0.90, 0.42, 1.0)
            dll_txt:SetString(is_zh
                and string.format("旧版 dinput8.dll (%s)，请手动替换 v%s 防崩溃", tostring(snap.dll_ver or "old"), exp_ver)
                or string.format("Outdated dinput8.dll (%s) -> Please Update to v%s", tostring(snap.dll_ver or "old"), exp_ver))
        else
            dll_bg:SetTint(0.36, 0.19, 0.07, 0.90)
            dll_txt:SetColour(1.0, 0.82, 0.48, 1.0)
            dll_txt:SetString(is_zh
                and string.format("未检测到新版 dinput8.dll (未装或<=v2.2.0) 请更新 v%s", exp_ver)
                or string.format("No v%s dinput8.dll (Missing or <=v2.2.0) — Update DLL", exp_ver))
        end

        local t1 = string.format("%.0f FPS | 1%%L %.0f | Busy %.1fms",
            snap.avg_fps or 0, snap.low1_fps or 0, snap.busy_ms or 0)
        if (snap.vram_mb or 0) > 0.5 then
            t1 = t1 .. string.format(" | VRAM %.0fMB", snap.vram_mb)
        end
        tele1:SetString(t1)

        local lat_s = snap.d3d9ex_active
            and string.format("Lat=%d/%d", snap.cur_lat or 1, snap.orig_lat or 3)
            or (snap.has_dll and string.format("Lat=%d", snap.cur_lat or 3) or "NoDLL")
        local lm_s = string.format("LM=%.1fx", snap.lm_scale or 0.5)
        local t2 = string.format("D3D9Ex %s | %s | VFX:%s | Light:%s | Flick+%d",
            lat_s, lm_s,
            snap.vfx_ok and "240" or "30",
            snap.light_ok and "240" or "30",
            snap.flick_rescues or 0)
        tele2:SetString(t2)
    end

    local function RefreshStates()
        local snap = api.GetSnapshot()
        local fl = snap.flags or {}
        for i = 1, #preset_btns do
            local pb = preset_btns[i]
            local active = (snap.preset == pb.id)
            if active then
                pb.bg:SetTint(0.45, 0.34, 0.14, 0.95)
                pb.btn:SetTextColour(1.0, 0.92, 0.62, 1.0)
            else
                pb.bg:SetTint(0.14, 0.14, 0.18, 0.88)
                pb.btn:SetTextColour(0.74, 0.77, 0.82, 0.95)
            end
        end
        local on_txt  = is_zh and "[开]" or "[ON]"
        local off_txt = is_zh and "[关]" or "[OFF]"
        for i = 1, #row_widgets do
            local rw = row_widgets[i]
            local on = fl[rw.key] == true
            rw.val:SetString(on and on_txt or off_txt)
            if on then
                rw.bg:SetTint(0.12, 0.22, 0.16, 0.82)
                rw.lbl:SetColour(0.92, 0.98, 0.94, 1.0)
                rw.val:SetColour(0.42, 1.0, 0.62, 1.0)
            else
                rw.bg:SetTint(0.11, 0.12, 0.15, 0.72)
                rw.lbl:SetColour(0.75, 0.78, 0.82, 0.92)
                rw.val:SetColour(0.55, 0.58, 0.64, 0.88)
            end
        end
        for i = 1, #eng_widgets do
            local ew = eng_widgets[i]
            local on = (ew.key == "skeletal") and snap.skeletal or snap.low_latency
            ew.val:SetString(on and on_txt or off_txt)
            if on then
                ew.lbl:SetColour(0.88, 0.96, 1.0, 1.0)
                ew.val:SetColour(0.42, 0.92, 1.0, 1.0)
            else
                ew.lbl:SetColour(0.68, 0.72, 0.76, 0.90)
                ew.val:SetColour(0.55, 0.58, 0.64, 0.88)
            end
        end
        RefreshTelemetry()
    end

    panel_refresh_fn = RefreshStates
    panel_telemetry_fn = RefreshTelemetry
    RefreshStates()
    return root
end

function HS_TogglePanel(force_state)
    local player = G.ThePlayer
    local hud = player and player.HUD
    if not hud then
        if G.print then
            G.print("[HyperSmooth] Live panel can only be opened in-game (enter a world first).")
        end
        return false
    end
    if not (panel_root and panel_root.inst and panel_root.inst:IsValid()) then
        local ok, built = pcall(CreateLiveCustomizerPanel, hud)
        if not (ok and built) then
            return false
        end
        panel_root = built
        panel_visible = false
    end
    if force_state == nil then
        panel_visible = not panel_visible
    else
        panel_visible = force_state and true or false
    end
    if panel_visible then
        if panel_refresh_fn then pcall(panel_refresh_fn) end
        panel_root:Show()
        panel_root:MoveToFront()
    else
        panel_root:Hide()
    end
    return panel_visible
end

function HS_PanelTickTelemetry()
    if panel_visible and panel_root and panel_root.inst and panel_root.inst:IsValid() and panel_telemetry_fn then
        pcall(panel_telemetry_fn)
    end
end

function HS_PanelNotifySync()
    if panel_visible and panel_root and panel_root.inst and panel_root.inst:IsValid() and panel_refresh_fn then
        pcall(panel_refresh_fn)
    end
end

G.rawset(G, "c_hsmenu", function(on)
    local vis = HS_TogglePanel(on)
    if G.print then
        G.print("[HyperSmooth] Live Customizer Panel -> " .. (vis and "OPEN" or "CLOSED"))
    end
end)
