-- =============================================================================
-- HyperSmooth v2.8.0 Complete 7-Tab In-Game Live Visual & Lighting Studio Panel
--   - Right-docked translucent overlay inside PlayerHUD (never pauses world or
--     blocks center viewport: walk, zoom & preview 0ms live changes on the fly!)
--   - 7-Tab 100% Parameter-Driven Studio: 10 Master Toggles + 3 Engine Switches
--     + 3 Player Self-Save Custom Slots (A/B/C) + All 73 Live Studio Controls
--   - Pure core DST widgets (images/global.xml + images/global_redux.xml)
-- =============================================================================

local G = GLOBAL
local pcall = G.pcall
local rawget = G.rawget
local type = G.type
local tonumber = G.tonumber
local tostring = G.tostring
local pairs = G.pairs
local ipairs = G.ipairs
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
local ok_te, TextEdit = pcall(function()
    return G.require("widgets/textedit")
end)
if not ok_te then TextEdit = nil end

local panel_root = nil
local panel_visible = false
local panel_refresh_fn = nil
local panel_telemetry_fn = nil

local function IsChineseLocale()
    if type(HS_IsUiZh) == "function" then
        return HS_IsUiZh() == true
    end
    local lt = G.LanguageTranslator
    if type(lt) == "table" and type(lt.defaultlang) == "string" and #lt.defaultlang > 0 then
        local dl = lt.defaultlang
        if dl == "zh" or dl == "zhr" or dl == "zht" then
            return true
        end
        if dl ~= "en" then
            return false
        end
    end
    local opt_str = G.STRINGS and G.STRINGS.UI and G.STRINGS.UI.OPTIONS
    if type(opt_str) == "table" and type(opt_str.ENABLED) == "string" then
        return opt_str.ENABLED == "启用" or opt_str.ENABLED == "啟用"
    end
    return false
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

    local W, H = 400, 666
    -- Position inner child container inside SCALEMODE_PROPORTIONAL anchor so offsets use
    -- resolution-independent virtual 720p units instead of unscaled raw screen pixels.
    local panel = root:AddChild(Widget("HyperSmoothLivePanelBody"))
    panel:SetPosition(-244, -10, 0)

    -- Subtle warm charcoal translucent backdrop + crisp border frame
    local border = panel:AddChild(Image("images/global.xml", "square.tex"))
    border:ScaleToSize(W + 4, H + 4)
    border:SetTint(0.76, 0.62, 0.34, 0.64)

    local bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    bg:ScaleToSize(W, H)
    bg:SetTint(0.07, 0.07, 0.09, 0.90)

    -- Header bar
    local title = panel:AddChild(Text(FONT_BODY, 21, is_zh and "HyperSmooth v2.8.0 画质光影工坊" or "HyperSmooth v2.8.0 Visual Studio"))
    title:SetPosition(-18, H * 0.5 - 22, 0)
    title:SetColour(0.96, 0.85, 0.56, 1.0)

    local close_btn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    close_btn.scale_on_focus = false
    close_btn.move_on_click = false
    close_btn:ForceImageSize(32, 24)
    close_btn:SetPosition(W * 0.5 - 24, H * 0.5 - 22, 0)
    close_btn:SetText("X")
    close_btn:SetTextSize(18)
    close_btn:SetFont(FONT_NUM)
    close_btn:SetTextColour(0.95, 0.55, 0.55, 1.0)
    close_btn:SetTextFocusColour(1.0, 0.78, 0.78, 1.0)
    close_btn:SetOnClick(function()
        if type(HS_TogglePanel) == "function" then
            HS_TogglePanel(false)
        end
    end)

    -- Native bin64/dinput8.dll Version & Update Alert Strip
    local dll_bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    dll_bg:ScaleToSize(W - 22, 22)
    dll_bg:SetPosition(0, H * 0.5 - 49, 0)
    dll_bg:SetTint(0.08, 0.18, 0.13, 0.86)

    local dll_txt = panel:AddChild(Text(FONT_BODY, 14, ""))
    dll_txt:SetRegionSize(W - 28, 20)
    dll_txt:SetPosition(0, H * 0.5 - 49, 0)
    dll_txt:SetColour(0.50, 1.0, 0.68, 0.96)
    dll_txt:SetClickable(false)

    -- Live Telemetry Strip
    local tele_bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    tele_bg:ScaleToSize(W - 22, 42)
    tele_bg:SetPosition(0, H * 0.5 - 86, 0)
    tele_bg:SetTint(0.03, 0.04, 0.06, 0.72)

    local tele1 = panel:AddChild(Text(FONT_BODY, 15, ""))
    tele1:SetRegionSize(W - 26, 20)
    tele1:SetPosition(0, H * 0.5 - 77, 0)
    tele1:SetColour(0.50, 1.0, 0.68, 0.96)

    local tele2 = panel:AddChild(Text(FONT_BODY, 14, ""))
    tele2:SetRegionSize(W - 26, 20)
    tele2:SetPosition(0, H * 0.5 - 95, 0)
    tele2:SetColour(0.78, 0.86, 0.96, 0.92)

    -- 4 Quick Preset Pills (0=Vanilla, 1=EyeHD, 2=Cinema Studio, 3=Custom)
    local preset_labels_zh = { [0] = "原版纯净", [1] = "护眼高清", [2] = "电影光影", [3] = "自定义" }
    local preset_labels_en = { [0] = "Vanilla",  [1] = "EyeHD",    [2] = "Cinema",   [3] = "Custom" }
    local preset_btns = {}
    local pp_w, pp_gap = 90, 6
    local pp_start = -1.5 * (pp_w + pp_gap)
    local preset_y = H * 0.5 - 124
    for pid = 0, 3 do
        local px = pp_start + pid * (pp_w + pp_gap)
        local pbg = panel:AddChild(Image("images/global.xml", "square.tex"))
        pbg:ScaleToSize(pp_w, 24)
        pbg:SetPosition(px, preset_y, 0)
        pbg:SetTint(0.14, 0.14, 0.18, 0.90)

        local pb = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        pb.scale_on_focus = false
        pb.move_on_click = false
        pb:ForceImageSize(pp_w, 24)
        pb:SetPosition(px, preset_y, 0)
        pb:SetText(is_zh and preset_labels_zh[pid] or preset_labels_en[pid])
        pb:SetTextSize(15)
        pb:SetFont(FONT_BODY)
        pb:SetTextFocusColour(1.0, 0.96, 0.74, 1.0)
        pb:SetOnClick(function()
            api.SetPreset(pid)
            if panel_refresh_fn then panel_refresh_fn() end
        end)
        preset_btns[#preset_btns + 1] = { id = pid, bg = pbg, btn = pb }
    end

    -- 8-Tab Switcher: [1:总览] [2:锐化] [3:调色] [4:CDL] [5:辉光] [6:光影] [7:海滤] [8:扩展]
    local active_tab = 1
    local tab_y = H * 0.5 - 153
    local tab_roots = {}
    for tid = 1, 8 do
        tab_roots[tid] = panel:AddChild(Widget("HS_Tab_" .. tid))
        if tid > 1 then tab_roots[tid]:Hide() end
    end

    local tab_defs = {
        { id = 1, zh = "总览", en = "Main" },
        { id = 2, zh = "锐化", en = "Shrp" },
        { id = 3, zh = "调色", en = "Tone" },
        { id = 4, zh = "CDL",  en = "CDL"  },
        { id = 5, zh = "辉光", en = "Glow" },
        { id = 6, zh = "光影", en = "Lit"  },
        { id = 7, zh = "海滤", en = "Sea"  },
        { id = 8, zh = "扩展", en = "Ext"  },
    }
    local tab_btns = {}
    local tw_w, tw_gap = 45, 2.5
    local tw_start = -3.5 * (tw_w + tw_gap)
    for i = 1, #tab_defs do
        local td = tab_defs[i]
        local tx = tw_start + (i - 1) * (tw_w + tw_gap)
        local tbg = panel:AddChild(Image("images/global.xml", "square.tex"))
        tbg:ScaleToSize(tw_w, 24)
        tbg:SetPosition(tx, tab_y, 0)
        tbg:SetTint(0.14, 0.16, 0.22, 0.90)

        local tbtn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        tbtn.scale_on_focus = false
        tbtn.move_on_click = false
        tbtn:ForceImageSize(tw_w, 24)
        tbtn:SetPosition(tx, tab_y, 0)
        tbtn:SetText(is_zh and td.zh or td.en)
        tbtn:SetTextSize(13)
        tbtn:SetFont(FONT_BODY)
        tbtn:SetTextFocusColour(1.0, 0.96, 0.74, 1.0)
        tbtn:SetOnClick(function()
            active_tab = td.id
            for tid = 1, 8 do
                if tid == active_tab then
                    tab_roots[tid]:Show()
                else
                    tab_roots[tid]:Hide()
                end
            end
            if panel_refresh_fn then panel_refresh_fn() end
        end)
        tab_btns[i] = { id = td.id, zh = td.zh, en = td.en, bg = tbg, btn = tbtn }
    end

    -- Helper: section banner inside a tab
    local section_banners = {}
    local function AddSectionBanner(parent, ry, zh_s, en_s, tint)
        local sbg = parent:AddChild(Image("images/global.xml", "square.tex"))
        sbg:ScaleToSize(W - 22, 20)
        sbg:SetPosition(0, ry, 0)
        sbg:SetTint(tint[1], tint[2], tint[3], tint[4])

        local stxt = parent:AddChild(Text(FONT_BODY, 13, is_zh and zh_s or en_s))
        stxt:SetRegionSize(W - 28, 18)
        stxt:SetPosition(0, ry, 0)
        stxt:SetColour(0.86, 0.94, 1.0, 0.96)
        stxt:SetClickable(false)
        section_banners[#section_banners + 1] = { txt = stxt, zh = zh_s, en = en_s }
        return sbg, stxt
    end

    -- Helper: uniform numeric stepper row with clickable center single-reset readout!
    local all_steppers = {}
    local function AddStepperRow(parent, ry, row_h, def)
        local rbg = parent:AddChild(Image("images/global.xml", "square.tex"))
        rbg:ScaleToSize(W - 22, row_h)
        rbg:SetPosition(0, ry, 0)
        rbg:SetTint(0.11, 0.12, 0.16, 0.78)

        local rlbl = parent:AddChild(Text(FONT_BODY, 14, is_zh and def.zh or def.en))
        rlbl:SetRegionSize(216, row_h - 2)
        rlbl:SetHAlign(G.ANCHOR_LEFT)
        rlbl:SetPosition(-73, ry, 0)
        rlbl:SetColour(0.90, 0.93, 0.96, 1.0)
        rlbl:SetClickable(false)

        local bh = row_h - 4
        local minus_bg = parent:AddChild(Image("images/global.xml", "square.tex"))
        minus_bg:ScaleToSize(28, bh)
        minus_bg:SetPosition(58, ry, 0)
        minus_bg:SetTint(0.22, 0.22, 0.28, 0.92)

        local minus_btn = parent:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        minus_btn.scale_on_focus = false
        minus_btn.move_on_click = false
        minus_btn:ForceImageSize(28, bh)
        minus_btn:SetPosition(58, ry, 0)
        minus_btn:SetText("-")
        minus_btn:SetTextSize(16)
        minus_btn:SetFont(FONT_NUM)
        minus_btn:SetTextColour(1.0, 0.88, 0.65, 1.0)
        minus_btn:SetOnClick(function()
            if type(api.StepTune) == "function" then
                api.StepTune(def.key, -def.step)
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end)

        local val_btn = parent:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        val_btn.scale_on_focus = false
        val_btn.move_on_click = false
        val_btn:ForceImageSize(68, bh)
        val_btn:SetPosition(108, ry, 0)
        val_btn:SetText("")
        val_btn:SetTextSize(13)
        val_btn:SetFont(FONT_NUM)
        val_btn:SetTextColour(0.45, 1.0, 0.72, 1.0)
        val_btn:SetTextFocusColour(1.0, 0.96, 0.68, 1.0)
        val_btn:SetOnClick(function()
            if type(api.ResetTuneField) == "function" then
                api.ResetTuneField(def.key)
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end)

        local plus_bg = parent:AddChild(Image("images/global.xml", "square.tex"))
        plus_bg:ScaleToSize(28, bh)
        plus_bg:SetPosition(158, ry, 0)
        plus_bg:SetTint(0.22, 0.22, 0.28, 0.92)

        local plus_btn = parent:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        plus_btn.scale_on_focus = false
        plus_btn.move_on_click = false
        plus_btn:ForceImageSize(28, bh)
        plus_btn:SetPosition(158, ry, 0)
        plus_btn:SetText("+")
        plus_btn:SetTextSize(16)
        plus_btn:SetFont(FONT_NUM)
        plus_btn:SetTextColour(1.0, 0.88, 0.65, 1.0)
        plus_btn:SetOnClick(function()
            if type(api.StepTune) == "function" then
                api.StepTune(def.key, def.step)
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end)

        local entry = {
            key = def.key, flag = def.flag, native_ws = def.native_ws, fmt = def.fmt, scale = def.scale,
            zh = def.zh, en = def.en, bg = rbg, lbl = rlbl, val_btn = val_btn,
        }
        all_steppers[#all_steppers + 1] = entry
        return entry
    end

    -- Helper: group reset button at bottom of each tuning tab
    local tab_reset_btns = {}
    local function AddGroupResetButton(parent, ry, zh_lbl, en_lbl, keys)
        local rbg = parent:AddChild(Image("images/global.xml", "square.tex"))
        rbg:ScaleToSize(W - 22, 24)
        rbg:SetPosition(0, ry, 0)
        rbg:SetTint(0.20, 0.16, 0.12, 0.90)

        local rbtn = parent:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        rbtn.scale_on_focus = false
        rbtn.move_on_click = false
        rbtn:ForceImageSize(W - 22, 24)
        rbtn:SetPosition(0, ry, 0)
        rbtn:SetText(is_zh and zh_lbl or en_lbl)
        rbtn:SetTextSize(14)
        rbtn:SetFont(FONT_BODY)
        rbtn:SetTextColour(0.96, 0.86, 0.62, 1.0)
        rbtn:SetTextFocusColour(1.0, 0.96, 0.78, 1.0)
        rbtn:SetOnClick(function()
            if type(api.ResetTuneGroup) == "function" then
                api.ResetTuneGroup(keys)
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end)
        tab_reset_btns[#tab_reset_btns + 1] = { btn = rbtn, zh = zh_lbl, en = en_lbl }
    end

    -- =========================================================================
    -- TAB 1: Overview — 10 Visual Master Toggles + 3 Engine Switches + 3 Save Slots
    -- =========================================================================
    local tab1 = tab_roots[1]
    local vis_rows = {
        { key = "af16",         zh = "16× 各向异性过滤 (斜视角清晰)",       en = "16x Anisotropic Filtering" },
        { key = "full_lm",      zh = "全分辨率 1.0× 光照图 (0ms热切)",     en = "1.0x Full-Res LightMap" },
        { key = "mip_sharp",    zh = "原生纹理防虚化 (MipLodBias)",         en = "Native Mipmap Sharpness" },
        { key = "cas",          zh = "BCAS 双边锐化与反卷积墨线修复",       en = "BCAS Bilateral Sharp & Ink" },
        { key = "ao",           zh = "实体温和接地阴影与防爆白",           en = "Soft Entity Contact Shadow" },
        { key = "env",          zh = "柔和近岸与雨天湿土微调",             en = "Soft Shoreline & Rain Wetness" },
        { key = "soft_bloom",   zh = "护眼防过曝柔光泛光 (开泛光生效)",     en = "Eye-Care Soft Bloom (Bloom ON)" },
        { key = "tone",         zh = "电影调色、CDL 与 ACES 高光保护",     en = "Cinematic Grading & CDL/ACES" },
        { key = "dark_clarity", zh = "暗夜与洞穴护眼层次提亮",             en = "Dark/Cave Clarity + LUT Fix" },
        { key = "clean_filter", zh = "滤镜净化快捷开关 (理智/雪/沙)",       en = "Clean Filters Master Toggle" },
    }
    local row_widgets = {}
    for i = 1, #vis_rows do
        local rv = vis_rows[i]
        local ry = 148 - (i - 1) * 26

        local rbg = tab1:AddChild(Image("images/global.xml", "square.tex"))
        rbg:ScaleToSize(W - 22, 22)
        rbg:SetPosition(0, ry, 0)
        rbg:SetTint(0.11, 0.12, 0.15, 0.72)

        local rbtn = tab1:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        rbtn.scale_on_focus = false
        rbtn.move_on_click = false
        rbtn:ForceImageSize(W - 22, 22)
        rbtn:SetPosition(0, ry, 0)
        rbtn:SetOnClick(function()
            api.ToggleVisualFlag(rv.key)
            if panel_refresh_fn then panel_refresh_fn() end
        end)

        local rlbl = tab1:AddChild(Text(FONT_BODY, 14, is_zh and rv.zh or rv.en))
        rlbl:SetRegionSize(296, 20)
        rlbl:SetHAlign(G.ANCHOR_LEFT)
        rlbl:SetPosition(-33, ry, 0)
        rlbl:SetClickable(false)

        local rval = tab1:AddChild(Text(FONT_BODY, 13, ""))
        rval:SetRegionSize(64, 20)
        rval:SetHAlign(G.ANCHOR_RIGHT)
        rval:SetPosition(148, ry, 0)
        rval:SetClickable(false)

        row_widgets[i] = { key = rv.key, zh = rv.zh, en = rv.en, bg = rbg, lbl = rlbl, val = rval }
    end

    local div = tab1:AddChild(Image("images/global.xml", "square.tex"))
    div:ScaleToSize(W - 26, 2)
    div:SetPosition(0, -104, 0)
    div:SetTint(0.76, 0.62, 0.34, 0.38)

    local eng_rows = {
        { key = "skeletal", zh = "240Hz 全场景补帧 (骨骼+粒子+镜头)", en = "240Hz Smooth (Anim+VFX+Cam)" },
        { key = "lowlat",   zh = "1帧硬件低延迟与急甩防漏",           en = "1-Frame Reflex + Flick Rescue" },
        { key = "hudmode",  zh = "右上角 HUD 模式 (简洁/详细/隐藏)",   en = "Top-Right HUD (Compact/Detail/Off)" },
    }
    local eng_widgets = {}
    for i = 1, #eng_rows do
        local ev = eng_rows[i]
        local ry = -121 - (i - 1) * 27

        local rbg = tab1:AddChild(Image("images/global.xml", "square.tex"))
        rbg:ScaleToSize(W - 22, 23)
        rbg:SetPosition(0, ry, 0)
        rbg:SetTint(0.10, 0.14, 0.17, 0.78)

        local rbtn = tab1:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        rbtn.scale_on_focus = false
        rbtn.move_on_click = false
        rbtn:ForceImageSize(W - 22, 23)
        rbtn:SetPosition(0, ry, 0)
        rbtn:SetOnClick(function()
            if ev.key == "skeletal" then
                api.ToggleSkeletal()
            elseif ev.key == "lowlat" then
                api.ToggleLowLatency()
            elseif type(api.CycleHudMode) == "function" then
                api.CycleHudMode()
            end
            if panel_refresh_fn then panel_refresh_fn() end
        end)

        local rlbl = tab1:AddChild(Text(FONT_BODY, 14, is_zh and ev.zh or ev.en))
        rlbl:SetRegionSize(284, 20)
        rlbl:SetHAlign(G.ANCHOR_LEFT)
        rlbl:SetPosition(-39, ry, 0)
        rlbl:SetClickable(false)

        local rval = tab1:AddChild(Text(FONT_BODY, 13, ""))
        rval:SetRegionSize(74, 20)
        rval:SetHAlign(G.ANCHOR_RIGHT)
        rval:SetPosition(143, ry, 0)
        rval:SetClickable(false)

        eng_widgets[i] = { key = ev.key, zh = ev.zh, en = ev.en, lbl = rlbl, val = rval }
    end

    -- 3 Player Self-Save Custom Preset Slots (Save / Load 1..3)
    AddSectionBanner(tab1, -205, "玩家自存方案槽 (可存 3 套私人配方 · 重启不丢)", "Player Custom Slots (Save / Load 3 Personal Looks)", { 0.18, 0.15, 0.10, 0.86 })
    local slot_widgets = {}
    local sb_w, sb_gap = 58, 5
    local sb_start = -2.5 * (sb_w + sb_gap)
    for sidx = 1, 3 do
        local idx_c = sidx
        local sx_save = sb_start + (sidx - 1) * 2 * (sb_w + sb_gap)
        local sx_load = sx_save + (sb_w + sb_gap)

        local sbg = tab1:AddChild(Image("images/global.xml", "square.tex"))
        sbg:ScaleToSize(sb_w, 25)
        sbg:SetPosition(sx_save, -234, 0)
        sbg:SetTint(0.24, 0.19, 0.12, 0.92)

        local sbtn = tab1:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        sbtn.scale_on_focus = false
        sbtn.move_on_click = false
        sbtn:ForceImageSize(sb_w, 25)
        sbtn:SetPosition(sx_save, -234, 0)
        sbtn:SetText(is_zh and ("存槽" .. idx_c) or ("Sav" .. idx_c))
        sbtn:SetTextSize(14)
        sbtn:SetFont(FONT_BODY)
        sbtn:SetTextColour(1.0, 0.88, 0.58, 1.0)
        sbtn:SetOnClick(function()
            if type(api.SaveCustomSlot) == "function" then
                api.SaveCustomSlot(idx_c)
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end)

        local lbg = tab1:AddChild(Image("images/global.xml", "square.tex"))
        lbg:ScaleToSize(sb_w, 25)
        lbg:SetPosition(sx_load, -234, 0)
        lbg:SetTint(0.12, 0.22, 0.28, 0.92)

        local lbtn = tab1:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        lbtn.scale_on_focus = false
        lbtn.move_on_click = false
        lbtn:ForceImageSize(sb_w, 25)
        lbtn:SetPosition(sx_load, -234, 0)
        lbtn:SetText(is_zh and ("读槽" .. idx_c) or ("Lod" .. idx_c))
        lbtn:SetTextSize(14)
        lbtn:SetFont(FONT_BODY)
        lbtn:SetTextColour(0.68, 0.92, 1.0, 1.0)
        lbtn:SetOnClick(function()
            if type(api.LoadCustomSlot) == "function" then
                api.LoadCustomSlot(idx_c)
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end)
        slot_widgets[idx_c] = { idx = idx_c, sbtn = sbtn, lbg = lbg, lbtn = lbtn }
    end

    -- Share Code & JSON Library Card Toggle Button (in Tab 1 bottom area)
    local share_card = nil
    local share_open_bg = tab1:AddChild(Image("images/global.xml", "square.tex"))
    share_open_bg:ScaleToSize(W - 22, 25)
    share_open_bg:SetPosition(0, -267, 0)
    share_open_bg:SetTint(0.24, 0.20, 0.11, 0.94)

    local share_open_btn = tab1:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    share_open_btn.scale_on_focus = false
    share_open_btn.move_on_click = false
    share_open_btn:ForceImageSize(W - 22, 25)
    share_open_btn:SetPosition(0, -267, 0)
    share_open_btn:SetText(is_zh and "分享 / 导入画质方案码 (HS1 / JSON)" or "Share / Import Preset Code (HS1 / JSON)")
    share_open_btn:SetTextSize(14)
    share_open_btn:SetFont(FONT_BODY)
    share_open_btn:SetTextColour(1.0, 0.90, 0.62, 1.0)
    share_open_btn:SetTextFocusColour(1.0, 0.98, 0.80, 1.0)

    -- =========================================================================
    -- Floating Share & JSON Library Card (docked left of Studio Panel, zero pause!)
    -- Strict 3-Phase Transactional Import: Validate -> Atomic Apply -> Persist
    -- =========================================================================
    local SW, SH = 356, 392
    share_card = panel:AddChild(Widget("HS_ShareCodeCard"))
    share_card:SetPosition(-(W * 0.5 + SW * 0.5 + 10), -10, 0)
    share_card:Hide()
    local share_card_visible = false

    local sc_border = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_border:ScaleToSize(SW + 4, SH + 4)
    sc_border:SetTint(0.76, 0.62, 0.34, 0.72)

    local sc_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_bg:ScaleToSize(SW, SH)
    sc_bg:SetTint(0.08, 0.08, 0.11, 0.96)

    local sc_title = share_card:AddChild(Text(FONT_BODY, 18, is_zh and "HyperSmooth · 画质方案分享" or "HyperSmooth · Share Preset"))
    sc_title:SetPosition(-16, SH * 0.5 - 22, 0)
    sc_title:SetColour(0.96, 0.86, 0.58, 1.0)

    local sc_close = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_close.scale_on_focus = false
    sc_close.move_on_click = false
    sc_close:ForceImageSize(28, 22)
    sc_close:SetPosition(SW * 0.5 - 20, SH * 0.5 - 22, 0)
    sc_close:SetText("X")
    sc_close:SetTextSize(16)
    sc_close:SetFont(FONT_NUM)
    sc_close:SetTextColour(0.95, 0.55, 0.55, 1.0)
    sc_close:SetOnClick(function()
        share_card_visible = false
        share_card:Hide()
    end)

    local sc_top_div = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_top_div:ScaleToSize(SW - 24, 1)
    sc_top_div:SetPosition(0, SH * 0.5 - 39, 0)
    sc_top_div:SetTint(0.76, 0.62, 0.34, 0.35)

    -- Current scheme summary row ("当前方案：xxx" + green "已保存" badge + subtitle)
    local sc_cur_lbl = share_card:AddChild(Text(FONT_BODY, 15, is_zh and "当前方案：原版纯净" or "Active: Vanilla Pure"))
    sc_cur_lbl:SetRegionSize(SW - 96, 20)
    sc_cur_lbl:SetHAlign(G.ANCHOR_LEFT)
    sc_cur_lbl:SetPosition(-36, 144, 0)
    sc_cur_lbl:SetColour(0.96, 0.96, 0.98, 1.0)

    local sc_saved_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_saved_bg:ScaleToSize(58, 20)
    sc_saved_bg:SetPosition(SW * 0.5 - 41, 144, 0)
    sc_saved_bg:SetTint(0.11, 0.32, 0.19, 0.96)

    local sc_saved_txt = share_card:AddChild(Text(FONT_BODY, 12, is_zh and "已保存" or "Saved"))
    sc_saved_txt:SetPosition(SW * 0.5 - 41, 144, 0)
    sc_saved_txt:SetColour(0.56, 1.0, 0.72, 1.0)

    local sc_sub_lbl = share_card:AddChild(Text(FONT_BODY, 12, is_zh and "自定义着色器参数 · 留空标题可生成极致短码" or "Custom shader params · blank title = shortest code"))
    sc_sub_lbl:SetRegionSize(SW - 24, 16)
    sc_sub_lbl:SetHAlign(G.ANCHOR_LEFT)
    sc_sub_lbl:SetPosition(0, 124, 0)
    sc_sub_lbl:SetColour(0.66, 0.72, 0.80, 0.92)

    local function MakeOptionalTextEdit(parent, px, py, ew, eh, limit, prompt_s)
        local bg_i = parent:AddChild(Image("images/global.xml", "square.tex"))
        bg_i:ScaleToSize(ew, eh)
        bg_i:SetPosition(px, py, 0)
        bg_i:SetTint(0.03, 0.04, 0.06, 0.92)
        if TextEdit ~= nil then
            local ok_e, ed = pcall(function()
                return TextEdit(FONT_BODY, 14, "")
            end)
            if ok_e and ed then
                parent:AddChild(ed)
                ed:SetPosition(px, py, 0)
                if ed.SetRegionSize then ed:SetRegionSize(ew - 10, eh - 2) end
                if ed.SetHAlign then ed:SetHAlign(G.ANCHOR_LEFT) end
                if ed.SetTextLengthLimit then ed:SetTextLengthLimit(limit) end
                if ed.EnableScrollEditWindow then ed:EnableScrollEditWindow(true) end
                if ed.SetIdleTextColour then ed:SetIdleTextColour(0.92, 0.95, 0.98, 1.0) end
                if ed.SetEditTextColour then ed:SetEditTextColour(1.0, 0.96, 0.78, 1.0) end
                if ed.SetEditCursorColour then ed:SetEditCursorColour(1.0, 0.85, 0.45, 1.0) end
                if ed.SetTextPrompt then ed:SetTextPrompt(prompt_s, { 0.52, 0.56, 0.64, 0.85 }) end
                return ed, true
            end
        end
        local fb = parent:AddChild(Text(FONT_BODY, 13, prompt_s))
        fb:SetRegionSize(ew - 10, eh - 2)
        fb:SetHAlign(G.ANCHOR_LEFT)
        fb:SetPosition(px, py, 0)
        fb:SetColour(0.75, 0.82, 0.90, 0.92)
        return fb, false
    end

    local function ReadEditString(w, is_edit)
        if not w then return "" end
        if is_edit and type(w.GetLineEditString) == "function" then
            local ok_g, s = pcall(w.GetLineEditString, w)
            if ok_g and type(s) == "string" then return s end
        end
        if type(w.GetString) == "function" then
            local ok_g, s = pcall(w.GetString, w)
            if ok_g and type(s) == "string" then return s end
        end
        return ""
    end

    local sc_name_ed, sc_name_is_ed = MakeOptionalTextEdit(share_card, 0, 98, SW - 24, 24, 36, is_zh and "方案标题（可选，如：暗黑童话）" or "Preset title (optional)")

    -- Share code preview strip + integrated Copy button on the right
    local sc_code_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_code_bg:ScaleToSize(SW - 24, 32)
    sc_code_bg:SetPosition(0, 62, 0)
    sc_code_bg:SetTint(0.05, 0.08, 0.11, 0.94)

    local sc_code_txt = share_card:AddChild(Text(FONT_NUM, 13, ""))
    sc_code_txt:SetRegionSize(SW - 120, 24)
    sc_code_txt:SetHAlign(G.ANCHOR_LEFT)
    sc_code_txt:SetPosition(-44, 62, 0)
    sc_code_txt:SetColour(0.54, 1.0, 0.74, 0.98)

    local sc_status_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_status_bg:ScaleToSize(SW - 24, 38)
    sc_status_bg:SetPosition(0, -162, 0)
    sc_status_bg:SetTint(0.10, 0.13, 0.17, 0.88)

    local sc_status_txt = share_card:AddChild(Text(FONT_BODY, 13, ""))
    sc_status_txt:SetRegionSize(SW - 32, 34)
    sc_status_txt:SetPosition(0, -162, 0)
    sc_status_txt:SetColour(0.80, 0.88, 0.96, 0.95)

    local function SetShareStatus(kind, msg)
        if kind == "ok" then
            sc_status_bg:SetTint(0.09, 0.24, 0.15, 0.94)
            sc_status_txt:SetColour(0.52, 1.0, 0.70, 1.0)
        elseif kind == "err" then
            sc_status_bg:SetTint(0.32, 0.10, 0.10, 0.94)
            sc_status_txt:SetColour(1.0, 0.68, 0.68, 1.0)
        else
            sc_status_bg:SetTint(0.10, 0.14, 0.20, 0.90)
            sc_status_txt:SetColour(0.82, 0.90, 1.0, 0.96)
        end
        sc_status_txt:SetString(msg)
    end

    local sc_copy_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_copy_bg:ScaleToSize(82, 24)
    sc_copy_bg:SetPosition(SW * 0.5 - 57, 62, 0)
    sc_copy_bg:SetTint(0.18, 0.32, 0.22, 0.96)

    local sc_copy_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_copy_btn.scale_on_focus = false
    sc_copy_btn.move_on_click = false
    sc_copy_btn:ForceImageSize(82, 24)
    sc_copy_btn:SetPosition(SW * 0.5 - 57, 62, 0)
    sc_copy_btn:SetText(is_zh and "复制分享码" or "Copy Code")
    sc_copy_btn:SetTextSize(13)
    sc_copy_btn:SetFont(FONT_BODY)
    sc_copy_btn:SetTextColour(0.70, 1.0, 0.80, 1.0)
    sc_copy_btn:SetOnClick(function()
        if type(api.ExportShareCode) ~= "function" then return end
        local zh_now = IsChineseLocale()
        local title_in = ReadEditString(sc_name_ed, sc_name_is_ed)
        local code, copied, diffs = api.ExportShareCode(title_in)
        sc_code_txt:SetString(code)
        if copied then
            SetShareStatus("ok", zh_now
                and string.format("已复制到剪贴板！（%d 字符 · 含 %d 项参数差异）", #code, diffs or 0)
                or string.format("Copied to clipboard! (%d chars, %d diffs)", #code, diffs or 0))
        else
            if G.print then G.print("[HyperSmooth Share] " .. tostring(code)) end
            SetShareStatus("info", zh_now
                and string.format("已生成分享码（%d 字，已输出控制台；装新版DLL可一键复制）", #code)
                or string.format("Generated (%d chars, printed to console; DLL enables 1-click copy)", #code))
        end
        if panel_refresh_fn then panel_refresh_fn() end
    end)

    local sc_json_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_json_bg:ScaleToSize(SW - 24, 24)
    sc_json_bg:SetPosition(0, 28, 0)
    sc_json_bg:SetTint(0.14, 0.20, 0.28, 0.94)

    local sc_json_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_json_btn.scale_on_focus = false
    sc_json_btn.move_on_click = false
    sc_json_btn:ForceImageSize(SW - 24, 24)
    sc_json_btn:SetPosition(0, 28, 0)
    sc_json_btn:SetText(is_zh and "导出全部方案至 hypersmooth_presets.json" or "Export All to hypersmooth_presets.json")
    sc_json_btn:SetTextSize(13)
    sc_json_btn:SetFont(FONT_BODY)
    sc_json_btn:SetTextColour(0.76, 0.92, 1.0, 1.0)
    sc_json_btn:SetOnClick(function()
        if type(api.ExportJsonLibrary) == "function" then
            local zh_now = IsChineseLocale()
            local _, cnt = api.ExportJsonLibrary()
            SetShareStatus("ok", zh_now
                and string.format("已导出 hypersmooth_presets.json（共 %d 套方案）", cnt or 0)
                or string.format("Exported hypersmooth_presets.json (%d presets)", cnt or 0))
        end
    end)

    local sc_div = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_div:ScaleToSize(SW - 24, 2)
    sc_div:SetPosition(0, 6, 0)
    sc_div:SetTint(0.76, 0.62, 0.34, 0.40)

    local sc_imp_lbl = share_card:AddChild(Text(FONT_BODY, 14, is_zh and "导入其他玩家的方案（先校验、再应用、最后保存）" or "Import Player Preset (Validate -> Apply -> Save)"))
    sc_imp_lbl:SetRegionSize(SW - 24, 20)
    sc_imp_lbl:SetHAlign(G.ANCHOR_LEFT)
    sc_imp_lbl:SetPosition(0, -14, 0)
    sc_imp_lbl:SetColour(0.92, 0.94, 0.98, 0.96)

    local sc_imp_ed, sc_imp_is_ed = MakeOptionalTextEdit(share_card, 0, -44, SW - 24, 26, 384, is_zh and "粘贴 HyperSmooth 分享码（留空则读剪贴板）" or "Paste HS1-... share code (blank = read clipboard)")

    local function GetImportCandidateText()
        local s = ReadEditString(sc_imp_ed, sc_imp_is_ed)
        if type(s) == "string" and #(s:match("^%s*(.-)%s*$") or "") > 0 then
            return s
        end
        if type(api.ReadClipboard) == "function" then
            local cb = api.ReadClipboard()
            if type(cb) == "string" and #cb > 0 then
                if sc_imp_is_ed and sc_imp_ed.SetString then pcall(sc_imp_ed.SetString, sc_imp_ed, cb) end
                return cb
            end
        end
        return ""
    end

    -- Button 1: Dry-Run Validate ONLY (touches ZERO live settings!)
    local sc_chk_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_chk_bg:ScaleToSize(96, 26)
    sc_chk_bg:SetPosition(-118, -82, 0)
    sc_chk_bg:SetTint(0.18, 0.22, 0.30, 0.94)

    local sc_chk_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_chk_btn.scale_on_focus = false
    sc_chk_btn.move_on_click = false
    sc_chk_btn:ForceImageSize(96, 26)
    sc_chk_btn:SetPosition(-118, -82, 0)
    sc_chk_btn:SetText(is_zh and "导入前校验" or "Validate")
    sc_chk_btn:SetTextSize(14)
    sc_chk_btn:SetFont(FONT_BODY)
    sc_chk_btn:SetTextColour(0.78, 0.92, 1.0, 1.0)
    sc_chk_btn:SetOnClick(function()
        if type(api.ValidateShareCode) ~= "function" then return end
        local zh_now = IsChineseLocale()
        local raw = GetImportCandidateText()
        local ok, err, st = api.ValidateShareCode(raw)
        if ok and st then
            local nm = (#st.name > 0) and ("[" .. st.name .. "] ") or ""
            SetShareStatus("ok", zh_now
                and string.format("校验通过：%s基底=预设%d · 改动%d项（尚未覆盖）", nm, st.base_id or 0, st.diff_count or 0)
                or string.format("Valid: %sbase=%d, %d diffs (not applied yet)", nm, st.base_id or 0, st.diff_count or 0))
        else
            SetShareStatus("err", tostring(err or (zh_now and "校验失败（原配置未改动）" or "Invalid code (config unchanged)")))
        end
    end)

    -- Button 2: Transactional Paste & Safe Import (Validate -> Atomic Apply -> Persist!)
    local sc_go_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_go_bg:ScaleToSize(140, 26)
    sc_go_bg:SetPosition(4, -82, 0)
    sc_go_bg:SetTint(0.28, 0.22, 0.11, 0.96)

    local sc_go_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_go_btn.scale_on_focus = false
    sc_go_btn.move_on_click = false
    sc_go_btn:ForceImageSize(140, 26)
    sc_go_btn:SetPosition(4, -82, 0)
    sc_go_btn:SetText(is_zh and "安全导入并应用" or "Safe Import")
    sc_go_btn:SetTextSize(14)
    sc_go_btn:SetFont(FONT_BODY)
    sc_go_btn:SetTextColour(1.0, 0.90, 0.60, 1.0)
    sc_go_btn:SetOnClick(function()
        if type(api.ImportShareCode) ~= "function" then return end
        local zh_now = IsChineseLocale()
        local raw = GetImportCandidateText()
        local ok, err, st = api.ImportShareCode(raw)
        if ok and st then
            local nm = (#st.name > 0) and ("[" .. st.name .. "] ") or ""
            SetShareStatus("ok", zh_now
                and string.format("校验并导入成功：%s已应用 %d 项并保存", nm, st.diff_count or 0)
                or string.format("Imported %s(%d params applied & saved)", nm, st.diff_count or 0))
            if panel_refresh_fn then panel_refresh_fn() end
        else
            SetShareStatus("err", tostring(err or (zh_now and "导入中止：原配置未被覆盖" or "Import aborted: config untouched")))
        end
    end)

    -- Button 3: 1-Click Undo Last Import
    local sc_undo_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_undo_bg:ScaleToSize(80, 26)
    sc_undo_bg:SetPosition(124, -82, 0)
    sc_undo_bg:SetTint(0.22, 0.15, 0.18, 0.92)

    local sc_undo_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_undo_btn.scale_on_focus = false
    sc_undo_btn.move_on_click = false
    sc_undo_btn:ForceImageSize(80, 26)
    sc_undo_btn:SetPosition(124, -82, 0)
    sc_undo_btn:SetText(is_zh and "撤销导入" or "Undo")
    sc_undo_btn:SetTextSize(14)
    sc_undo_btn:SetFont(FONT_BODY)
    sc_undo_btn:SetTextColour(0.95, 0.78, 0.82, 1.0)
    sc_undo_btn:SetOnClick(function()
        if type(api.UndoLastImport) == "function" and api.UndoLastImport() then
            local zh_now = IsChineseLocale()
            SetShareStatus("info", zh_now and "已撤销上次导入，恢复原配置" or "Reverted to previous config")
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    local sc_hint = share_card:AddChild(Text(FONT_BODY, 12, is_zh and "坏码/越界值均在校验层拦截 · 控制台支持 c_hsshare / c_hsjson" or "Corrupt codes blocked pre-apply · Console: c_hsshare / c_hsjson"))
    sc_hint:SetRegionSize(SW - 24, 18)
    sc_hint:SetPosition(0, -120, 0)
    sc_hint:SetColour(0.64, 0.70, 0.78, 0.90)

    local function RefreshShareCardHeader(snap, zh_now)
        local pnames = zh_now and preset_labels_zh or preset_labels_en
        local cur_nm = (type(snap.scheme_name) == "string" and #snap.scheme_name > 0)
            and snap.scheme_name
            or (pnames[snap.preset] or (zh_now and "自定义" or "Custom"))
        sc_cur_lbl:SetString((zh_now and "当前方案：" or "Active: ") .. cur_nm)
        sc_saved_txt:SetString(zh_now and "已保存" or "Saved")
        sc_sub_lbl:SetString(zh_now
            and "自定义着色器参数 · 留空标题可生成极致短码"
            or "Custom shader params · blank title = shortest code")
        sc_imp_lbl:SetString(zh_now
            and "导入其他玩家的方案（先校验、再应用、最后保存）"
            or "Import Player Preset (Validate -> Apply -> Save)")
        sc_hint:SetString(zh_now
            and "坏码/越界值均在校验层拦截 · 控制台支持 c_hsshare / c_hsjson"
            or "Corrupt codes blocked pre-apply · Console: c_hsshare / c_hsjson")
        if sc_name_is_ed and sc_name_ed and sc_name_ed.SetTextPrompt then
            pcall(sc_name_ed.SetTextPrompt, sc_name_ed, zh_now and "方案标题（可选，如：暗黑童话）" or "Preset title (optional)", { 0.52, 0.56, 0.64, 0.85 })
        end
        if sc_imp_is_ed and sc_imp_ed and sc_imp_ed.SetTextPrompt then
            pcall(sc_imp_ed.SetTextPrompt, sc_imp_ed, zh_now and "粘贴 HyperSmooth 分享码（留空则读剪贴板）" or "Paste HS1-... share code (blank = read clipboard)", { 0.52, 0.56, 0.64, 0.85 })
        end
        if share_card_visible and type(api.PreviewShareCode) == "function" then
            local preview = api.PreviewShareCode()
            if type(preview) == "string" then
                sc_code_txt:SetString(preview)
            end
        end
    end

    share_open_btn:SetOnClick(function()
        share_card_visible = not share_card_visible
        if share_card_visible then
            if panel_refresh_fn then panel_refresh_fn() end
            SetShareStatus("info", IsChineseLocale()
                and "已就绪：点击“复制分享码”分享当前画面，或粘贴他人分享码先校验后导入"
                or "Ready: click Copy Code to share, or paste a code to validate & import")
            share_card:Show()
            share_card:MoveToFront()
        else
            share_card:Hide()
        end
    end)

    -- =========================================================================
    -- TAB 2: Sharpen — Complete BCAS Pro + Ink Deconv + Bilateral + AURA (14 Knobs)
    -- =========================================================================
    local tab2 = tab_roots[2]
    local tab2_defs = {
        { key = "sharp",         flag = "cas", step = 0.15,  fmt = "%.0f%%", scale = 100, zh = "BCAS 双边锐化强度",     en = "BCAS Bilateral Sharp" },
        { key = "ink",           flag = "cas", step = 0.15,  fmt = "%.0f%%", scale = 100, zh = "手绘墨线反卷积强度",    en = "Ink Deconv Strength" },
        { key = "deconv_gate",   flag = "cas", step = 0.005, fmt = "%.3f",   scale = 1,   zh = "墨线边缘触发门限",      en = "Ink Edge Gate" },
        { key = "deconv_penetr", flag = "cas", step = 0.10,  fmt = "%.0f%%", scale = 100, zh = "墨线暗部穿透率",        en = "Ink Dark Penetration" },
        { key = "anti_ring",     flag = "cas", step = 0.10,  fmt = "%.0f%%", scale = 100, zh = "AURA 防白边光晕抑制",   en = "AURA Anti-Ringing" },
        { key = "dark_prot",     flag = "cas", step = 0.05,  fmt = "%.0f%%", scale = 100, zh = "暗部防死黑保护",        en = "Dark Edge Protection" },
        { key = "chroma_prot",   flag = "cas", step = 0.10,  fmt = "%.0f%%", scale = 100, zh = "色彩边缘防色散保护",    en = "Chroma Edge Protect" },
        { key = "noise_reduce",  flag = "cas", step = 0.10,  fmt = "%.0f%%", scale = 100, zh = "平坦区防放噪抑制",      en = "Flat Noise Reduction" },
        { key = "range_sigma",   flag = "cas", step = 0.04,  fmt = "%.2f",   scale = 1,   zh = "双边值域核 RangeSigma", en = "Bilateral RangeSigma" },
        { key = "spatial_sigma", flag = "cas", step = 0.15,  fmt = "%.2f",   scale = 1,   zh = "双边空间核 SpatialSig", en = "Bilateral SpatialSig" },
        { key = "noise_floor",   flag = "cas", step = 0.004, fmt = "%.3f",   scale = 1,   zh = "噪声基底阈值",          en = "Noise Floor" },
        { key = "ar_l_over",     flag = "cas", step = 0.005, fmt = "%.3f",   scale = 1,   zh = "AURA 亮边过冲上限",     en = "AURA Light Overshoot" },
        { key = "ar_d_over",     flag = "cas", step = 0.005, fmt = "%.3f",   scale = 1,   zh = "AURA 暗边过冲上限",     en = "AURA Dark Overshoot" },
        { key = "dither",        flag = "cas", step = 0.25,  fmt = "%.0f%%", scale = 100, zh = "8-bit TPDF 防色带抖动", en = "8-Bit TPDF Dither" },
    }
    local tab2_keys = {}
    for i = 1, #tab2_defs do
        tab2_keys[i] = tab2_defs[i].key
        AddStepperRow(tab2, 148 - (i - 1) * 28, 24, tab2_defs[i])
    end
    AddGroupResetButton(tab2, -262, "重置本页 BCAS 锐化与墨线参数为默认", "Reset BCAS & Ink Defaults", tab2_keys)

    -- =========================================================================
    -- TAB 3: Color — Coexist Banner + 4 Quick Style Starters + 12 Grading Knobs
    -- =========================================================================
    local tab3 = tab_roots[3]
    local coexist_bg = tab3:AddChild(Image("images/global.xml", "square.tex"))
    coexist_bg:ScaleToSize(W - 22, 20)
    coexist_bg:SetPosition(0, 150, 0)
    coexist_bg:SetTint(0.10, 0.18, 0.24, 0.86)

    local coexist_txt = tab3:AddChild(Text(FONT_BODY, 13, ""))
    coexist_txt:SetRegionSize(W - 28, 18)
    coexist_txt:SetPosition(0, 150, 0)
    coexist_txt:SetColour(0.76, 0.92, 1.0, 0.95)
    coexist_txt:SetClickable(false)

    local mood_defs = {
        { id = "neutral", zh = "中性原色", en = "Neutral" },
        { id = "warm",    zh = "暖阳特调", en = "Warm"    },
        { id = "cool",    zh = "清冷护眼", en = "Cool"    },
        { id = "film",    zh = "胶片电影", en = "Filmic"  },
    }
    local mood_btns = {}
    for i = 1, #mood_defs do
        local md = mood_defs[i]
        local mx = pp_start + (i - 1) * (pp_w + pp_gap)
        local mbg = tab3:AddChild(Image("images/global.xml", "square.tex"))
        mbg:ScaleToSize(pp_w, 24)
        mbg:SetPosition(mx, 123, 0)
        mbg:SetTint(0.18, 0.16, 0.22, 0.90)

        local mbtn = tab3:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        mbtn.scale_on_focus = false
        mbtn.move_on_click = false
        mbtn:ForceImageSize(pp_w, 24)
        mbtn:SetPosition(mx, 123, 0)
        mbtn:SetText(is_zh and md.zh or md.en)
        mbtn:SetTextSize(14)
        mbtn:SetFont(FONT_BODY)
        mbtn:SetTextColour(0.95, 0.92, 0.82, 1.0)
        mbtn:SetTextFocusColour(1.0, 0.98, 0.72, 1.0)
        mbtn:SetOnClick(function()
            if type(api.ApplyColorMood) == "function" then
                api.ApplyColorMood(md.id)
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end)
        mood_btns[i] = { id = md.id, zh = md.zh, en = md.en, btn = mbtn }
    end

    local tab3_defs = {
        { key = "exposure",     flag = "tone", step = 0.05, fmt = "%+.2f",  scale = 1,   zh = "画面曝光 (EV)",         en = "Exposure (EV)" },
        { key = "temp",         flag = "tone", step = 0.08, fmt = "%+.2f",  scale = 1,   zh = "白平衡色温 (冷/暖)",    en = "White Balance Temp" },
        { key = "tint",         flag = "tone", step = 0.05, fmt = "%+.2f",  scale = 1,   zh = "白平衡色调 (绿/品红)",  en = "White Balance Tint" },
        { key = "contrast",     flag = "tone", step = 0.03, fmt = "%+.0f%%",scale = 100, zh = "画面对比度",            en = "Contrast" },
        { key = "lightness",    flag = "tone", step = 0.02, fmt = "%+.2f",  scale = 1,   zh = "全局明度偏移",          en = "Lightness Offset" },
        { key = "gamma",        flag = "tone", step = 0.04, fmt = "%.2f",   scale = 1,   zh = "伽马曲线 (Gamma)",      en = "Gamma Curve" },
        { key = "sat",          flag = "tone", step = 0.05, fmt = "%.0f%%", scale = 100, zh = "全局饱和度",            en = "Saturation" },
        { key = "vibrance",     flag = "tone", step = 0.05, fmt = "%+.0f%%",scale = 100, zh = "自然鲜艳度 (Vibrance)", en = "Smart Vibrance" },
        { key = "filmic",       flag = "tone", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "ACES 电影高光曲线",     en = "ACES Filmic Curve" },
        { key = "hl_desat",     flag = "tone", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "高光防溢色去饱和",      en = "Highlight Desat" },
        { key = "sun_fill",     flag = "tone", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "左上角天光定向补光",    en = "Directional Sun Fill" },
        { key = "original_mix", flag = "tone", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "原画混合度 (回退比例)", en = "Original Image Mix" },
    }
    local tab3_keys = {}
    for i = 1, #tab3_defs do
        tab3_keys[i] = tab3_defs[i].key
        AddStepperRow(tab3, 93 - (i - 1) * 28, 24, tab3_defs[i])
    end
    AddGroupResetButton(tab3, -262, "重置本页电影调色为中性原色", "Reset Color Grading to Neutral", tab3_keys)

    -- =========================================================================
    -- TAB 4: Pro ASC CDL (Slope / Offset / Power x RGB) + Film Vignette & Grain (11)
    -- =========================================================================
    local tab4 = tab_roots[4]
    AddSectionBanner(tab4, 150, "ASC CDL 三段专业调色 (斜率/暗部/幂次) + 电影氛围", "ASC CDL Color Grading (Slope/Offset/Power) + Film", { 0.16, 0.14, 0.22, 0.86 })
    local tab4_defs = {
        { key = "slope_r",  flag = "tone", step = 0.04, fmt = "%.2f",   scale = 1,   zh = "CDL 高光斜率 · 红 (Slope R)",  en = "CDL Slope Red" },
        { key = "slope_g",  flag = "tone", step = 0.04, fmt = "%.2f",   scale = 1,   zh = "CDL 高光斜率 · 绿 (Slope G)",  en = "CDL Slope Green" },
        { key = "slope_b",  flag = "tone", step = 0.04, fmt = "%.2f",   scale = 1,   zh = "CDL 高光斜率 · 蓝 (Slope B)",  en = "CDL Slope Blue" },
        { key = "offset_r", flag = "tone", step = 0.02, fmt = "%+.2f",  scale = 1,   zh = "CDL 暗部偏移 · 红 (Offset R)", en = "CDL Offset Red" },
        { key = "offset_g", flag = "tone", step = 0.02, fmt = "%+.2f",  scale = 1,   zh = "CDL 暗部偏移 · 绿 (Offset G)", en = "CDL Offset Green" },
        { key = "offset_b", flag = "tone", step = 0.02, fmt = "%+.2f",  scale = 1,   zh = "CDL 暗部偏移 · 蓝 (Offset B)", en = "CDL Offset Blue" },
        { key = "power_r",  flag = "tone", step = 0.04, fmt = "%.2f",   scale = 1,   zh = "CDL 中间调幂 · 红 (Power R)",  en = "CDL Power Red" },
        { key = "power_g",  flag = "tone", step = 0.04, fmt = "%.2f",   scale = 1,   zh = "CDL 中间调幂 · 绿 (Power G)",  en = "CDL Power Green" },
        { key = "power_b",  flag = "tone", step = 0.04, fmt = "%.2f",   scale = 1,   zh = "CDL 中间调幂 · 蓝 (Power B)",  en = "CDL Power Blue" },
        { key = "vignette", flag = "tone", step = 0.03, fmt = "%.0f%%", scale = 100, zh = "电影边缘暗角 (Vignette)",      en = "Edge Vignette" },
        { key = "grain",    flag = "tone", step = 0.04, fmt = "%.0f%%", scale = 100, zh = "动态胶片颗粒 (8Hz 时钟)",      en = "Animated Film Grain" },
    }
    local tab4_keys = {}
    for i = 1, #tab4_defs do
        tab4_keys[i] = tab4_defs[i].key
        AddStepperRow(tab4, 120 - (i - 1) * 32, 27, tab4_defs[i])
    end
    AddGroupResetButton(tab4, -262, "重置本页 CDL 与电影氛围为默认", "Reset CDL & Film Atmosphere", tab4_keys)

    -- =========================================================================
    -- TAB 5: Multi-Level Glow Pyramid & Atmospheric FX (12 Knobs)
    -- =========================================================================
    local tab5 = tab_roots[5]
    AddSectionBanner(tab5, 150, "多级 Kawase 柔光金字塔 + 空气散射/光束 (开泛光生效)", "Multi-Level Glow Pyramid & Air FX (Bloom ON)", { 0.22, 0.16, 0.10, 0.86 })
    local tab5_defs = {
        { key = "bloom_str",      flag = "soft_bloom", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "柔光泛光总强度",          en = "Bloom Intensity" },
        { key = "bloom_thresh",   flag = "soft_bloom", step = 0.04, fmt = "%.2f",   scale = 1,   zh = "起晕高亮阈值",            en = "Bloom Threshold" },
        { key = "bloom_knee",     flag = "soft_bloom", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "柔光软肩过渡 (防炸白)",   en = "Soft Knee Shoulder" },
        { key = "bloom_chr",      flag = "soft_bloom", step = 0.05, fmt = "%+.0f%%",scale = 100, zh = "辉光色彩饱和度",          en = "Glow Saturation" },
        { key = "bloom_prot",     flag = "soft_bloom", step = 0.05, fmt = "%.0f%%", scale = 100, zh = "高光防洗白屏幕保护",      en = "Screen Protect" },
        { key = "bloom_warmth",   flag = "soft_bloom", step = 0.15, fmt = "%+.0f%%",scale = 100, zh = "辉光冷暖偏移 (蓝/暖橙)",  en = "Glow Warmth" },
        { key = "bloom_spread",   flag = "soft_bloom", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "光晕扩散半径 (紧致/宽)",  en = "Halo Spread" },
        { key = "bloom_compress", flag = "soft_bloom", step = 0.05, fmt = "%.0f%%", scale = 100, zh = "辉光高光保色压肩",        en = "Highlight Compress" },
        { key = "glow_tail",      flag = "soft_bloom", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "夜空长尾衰减抬升",        en = "Long-Tail Lift" },
        { key = "light_wrap",     flag = "soft_bloom", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "暗部空气光包裹散射",      en = "Dark Air Light Wrap" },
        { key = "glow_rim",       flag = "soft_bloom", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "光源附近 1px 轮廓光",     en = "Luminous Edge Rim" },
        { key = "god_rays",       flag = "soft_bloom", step = 0.15, fmt = "%.0f%%", scale = 100, zh = "日晷斜向空气光束",        en = "Sun Crepuscular Rays" },
    }
    local tab5_keys = {}
    for i = 1, #tab5_defs do
        tab5_keys[i] = tab5_defs[i].key
        AddStepperRow(tab5, 121 - (i - 1) * 30, 26, tab5_defs[i])
    end
    AddGroupResetButton(tab5, -262, "重置本页柔光与空气光效为默认", "Reset Glow & Air FX Defaults", tab5_keys)

    -- =========================================================================
    -- TAB 6: Native World 3D Surface Light (9) + Fluid Ground Shadows v2 (11)
    --        Organized with clean 2-pill sub-switcher so every row stays spacious!
    -- =========================================================================
    local bool_filter_widgets = {}
    local UpdateTab6SubVisual = nil
    do
        local tab6 = tab_roots[6]
        local tab6_sub = 1
        local tab6_sub_roots = {
            tab6:AddChild(Widget("HS_Tab6_Light")),
            tab6:AddChild(Widget("HS_Tab6_Shadows")),
        }
        tab6_sub_roots[2]:Hide()
        local tab6_sub_btns = {}
        local tab6_sub_defs = {
            { id = 1, x = -94, zh = "3D 立体受光与细节 (9)", en = "3D Surface Light (9)" },
            { id = 2, x =  94, zh = "动态流体阴影 v2 (11)",  en = "Fluid Shadows v2 (11)" },
        }
        UpdateTab6SubVisual = function(zh_now)
            for i = 1, 2 do
                local sb = tab6_sub_btns[i]
                if sb then
                    sb.btn:SetText(zh_now and sb.zh or sb.en)
                    if tab6_sub == sb.id then
                        sb.bg:SetTint(0.22, 0.34, 0.46, 0.96)
                        sb.btn:SetTextColour(0.98, 0.96, 0.78, 1.0)
                    else
                        sb.bg:SetTint(0.12, 0.13, 0.17, 0.86)
                        sb.btn:SetTextColour(0.72, 0.76, 0.82, 0.92)
                    end
                end
            end
        end
        for i = 1, 2 do
            local sd = tab6_sub_defs[i]
            local sbg = tab6:AddChild(Image("images/global.xml", "square.tex"))
            sbg:ScaleToSize(184, 24)
            sbg:SetPosition(sd.x, 152, 0)
            sbg:SetTint(0.12, 0.13, 0.17, 0.86)
            local sbtn = tab6:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
            sbtn.scale_on_focus = false
            sbtn.move_on_click = false
            sbtn:ForceImageSize(184, 24)
            sbtn:SetFont(FONT_BODY)
            sbtn:SetTextSize(13)
            sbtn:SetText(is_zh and sd.zh or sd.en)
            sbtn:SetPosition(sd.x, 152, 0)
            sbtn:SetOnClick(function()
                tab6_sub = sd.id
                for j = 1, 2 do
                    if j == tab6_sub then tab6_sub_roots[j]:Show() else tab6_sub_roots[j]:Hide() end
                end
                if panel_refresh_fn then panel_refresh_fn() end
            end)
            tab6_sub_btns[i] = { id = sd.id, zh = sd.zh, en = sd.en, bg = sbg, btn = sbtn }
        end

        local tab6_light_defs = {
            { key = "mip_bias",       flag = "mip_sharp",    step = 0.05, fmt = "%+.2f",  scale = 1,   zh = "纹理防虚化 MipLodBias",    en = "Texture MipLodBias" },
            { key = "ao_str",         flag = "ao",           step = 0.15, fmt = "%.0f%%", scale = 100, zh = "实体接地接触影强度",       en = "Entity Contact AO" },
            { key = "env_str",        flag = "env",          step = 0.15, fmt = "%.0f%%", scale = 100, zh = "近岸与雨天湿土强度",       en = "Shoreline & Rain Wet" },
            { key = "dark_str",       flag = "dark_clarity", step = 0.15, fmt = "%.0f%%", scale = 100, zh = "暗夜与洞穴护眼提亮",       en = "Dark/Cave Clarity" },
            { key = "surface_light",  native_ws = true,      step = 0.15, fmt = "%.0f%%", scale = 100, zh = "3D 实体立体受光强度",      en = "3D Entity Surface Light" },
            { key = "shade_warm",     native_ws = true,      step = 0.10, fmt = "%.0f%%", scale = 100, zh = "受光面冷暖 (0冷/50中/暖)", en = "Lit Face Warmth" },
            { key = "shade_contrast", native_ws = true,      step = 0.10, fmt = "%.0f%%", scale = 100, zh = "立体明暗反差幅度",         en = "3D Relief Contrast" },
            { key = "shade_cool",     native_ws = true,      step = 0.10, fmt = "%.0f%%", scale = 100, zh = "背光面独立冷蓝偏色",       en = "Backlight Cool Blue" },
            { key = "shade_dark",     native_ws = true,      step = 0.10, fmt = "%.0f%%", scale = 100, zh = "背光面独立压暗深度",       en = "Backlight Dark Depth" },
        }
        local tab6_shad_defs = {
            { key = "shadow_density", native_ws = true,      step = 0.10, fmt = "%.0f%%", scale = 100, zh = "动态斜投影长影浓度",       en = "Projected Shadow Density" },
            { key = "shadow_length",  native_ws = true,      step = 0.10, fmt = "%.2fx",  scale = 1,   zh = "长影拉伸倍率 (影长)",      en = "Shadow Length Multiplier" },
            { key = "shadow_soft",    native_ws = true,      step = 0.15, fmt = "%.2f",   scale = 1,   zh = "9-Tap 半影边缘柔和度",     en = "9-Tap Penumbra Softness" },
            { key = "shadow_fade",    native_ws = true,      step = 0.10, fmt = "%.0f%%", scale = 100, zh = "高度渐变衰减 (根实梢虚)",  en = "Height Penumbra Fade" },
            { key = "shadow_sway",    native_ws = true,      step = 0.10, fmt = "%.0f%%", scale = 100, zh = "树冠微风连续摇曳幅度",     en = "Canopy Wind Sway Amp" },
            { key = "shadow_smooth",  native_ws = true,      step = 0.10, fmt = "%.0f%%", scale = 100, zh = "光影轨迹平滑阻尼 (dt)",    en = "Light Trajectory Damping" },
            { key = "shadow_tint_r",  native_ws = true,      step = 0.08, fmt = "%.2f",   scale = 1,   zh = "长影色彩 · 红 (Tint R)",   en = "Shadow Tint Red" },
            { key = "shadow_tint_g",  native_ws = true,      step = 0.08, fmt = "%.2f",   scale = 1,   zh = "长影色彩 · 绿 (Tint G)",   en = "Shadow Tint Green" },
            { key = "shadow_tint_b",  native_ws = true,      step = 0.08, fmt = "%.2f",   scale = 1,   zh = "长影色彩 · 蓝 (Tint B)",   en = "Shadow Tint Blue" },
        }
        local tab6_keys = {}
        for i = 1, #tab6_light_defs do
            tab6_keys[#tab6_keys + 1] = tab6_light_defs[i].key
            AddStepperRow(tab6_sub_roots[1], 118 - (i - 1) * 34, 28, tab6_light_defs[i])
        end
        for i = 1, #tab6_shad_defs do
            tab6_keys[#tab6_keys + 1] = tab6_shad_defs[i].key
            AddStepperRow(tab6_sub_roots[2], 122 - (i - 1) * 31, 25, tab6_shad_defs[i])
        end

        local function AddBoolTuneRow(parent, ry, row_h, bd)
            local rbg = parent:AddChild(Image("images/global.xml", "square.tex"))
            rbg:ScaleToSize(W - 22, row_h)
            rbg:SetPosition(0, ry, 0)
            rbg:SetTint(0.11, 0.12, 0.16, 0.78)

            local rbtn = parent:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
            rbtn.scale_on_focus = false
            rbtn.move_on_click = false
            rbtn:ForceImageSize(W - 22, row_h)
            rbtn:SetPosition(0, ry, 0)
            rbtn:SetOnClick(function()
                if type(api.ToggleTuneBool) == "function" then
                    api.ToggleTuneBool(bd.key)
                    if panel_refresh_fn then panel_refresh_fn() end
                end
            end)

            local rlbl = parent:AddChild(Text(FONT_BODY, 14, is_zh and bd.zh or bd.en))
            rlbl:SetRegionSize(284, 22)
            rlbl:SetHAlign(G.ANCHOR_LEFT)
            rlbl:SetPosition(-39, ry, 0)
            rlbl:SetClickable(false)

            local rval = parent:AddChild(Text(FONT_BODY, 14, ""))
            rval:SetRegionSize(74, 22)
            rval:SetHAlign(G.ANCHOR_RIGHT)
            rval:SetPosition(143, ry, 0)
            rval:SetClickable(false)

            bool_filter_widgets[#bool_filter_widgets + 1] = { key = bd.key, zh = bd.zh, en = bd.en, bg = rbg, lbl = rlbl, val = rval }
        end

        local tab6_bool_defs = {
            { key = "shadow_hd",             zh = "1.0× 全分辨率阴影画布 (消跳格)", en = "1.0x Full-Res Shadow Canvas" },
            { key = "shadow_skip_nightmare", zh = "影怪 / 暗影生物纯净免投影",      en = "No Shadow on Shadow Creatures" },
        }
        for i = 1, #tab6_bool_defs do
            tab6_keys[#tab6_keys + 1] = tab6_bool_defs[i].key
            AddBoolTuneRow(tab6_sub_roots[2], 122 - (8 + i) * 31, 25, tab6_bool_defs[i])
        end

        AddGroupResetButton(tab6, -262, "重置本页立体受光与动态阴影为默认", "Reset 3D Lighting & Fluid Shadows", tab6_keys)

        -- =========================================================================
        -- TAB 7: Native Ocean Caustics (4) + Independent Reversible Vanilla Filters (6)
        -- =========================================================================
        local tab7 = tab_roots[7]
        AddSectionBanner(tab7, 150, "原生海面焦散波光与浅滩沙底 (零额外实体 · 0过绘)", "Native Procedural Ocean Caustics & Sandy Shallows", { 0.08, 0.20, 0.24, 0.88 })
        local tab7_ocean_defs = {
            { key = "glint_str",     native_ws = true, step = 0.10, fmt = "%.0f%%", scale = 100, zh = "海面焦散波光强度",       en = "Ocean Caustics Strength" },
            { key = "glint_density", native_ws = true, step = 0.04, fmt = "%.2f",   scale = 1,   zh = "焦散光网锐利增益",       en = "Caustic Web Gain" },
            { key = "glint_grain",   native_ws = true, step = 0.15, fmt = "%.2f",   scale = 1,   zh = "波光细碎颗粒频率",       en = "Caustic Grain Scale" },
            { key = "glint_soft",    native_ws = true, step = 0.10, fmt = "%.0f%%", scale = 100, zh = "浅滩沙底与海色融合",     en = "Shallow Sand & Sea Blend" },
        }
        for i = 1, #tab7_ocean_defs do
            AddStepperRow(tab7, 121 - (i - 1) * 30, 26, tab7_ocean_defs[i])
        end

        AddSectionBanner(tab7, -2, "原版滤镜独立接管 & 夜间火堆投影 (无损可逆)", "Reversible Vanilla Filters & Night Light Shadows", { 0.14, 0.18, 0.24, 0.86 })
        AddStepperRow(tab7, -31, 26, { key = "vanilla_cc", step = 0.10, fmt = "%.0f%%", scale = 100, zh = "官方季节滤镜强度 (LUT)", en = "Vanilla ColourCube LUT" })
        AddStepperRow(tab7, -61, 26, { key = "snow_cap",   step = 0.10, fmt = "%.0f%%", scale = 100, zh = "冬季积雪覆盖上限",       en = "Winter Snow Overlay Cap" })

        local bool_filter_defs = {
            { key = "sanity_color", zh = "低理智保色 (去灰暗滤镜)",   en = "Clean Low-Sanity Color" },
            { key = "distort_free", zh = "低理智去波纹晃动",          en = "No Sanity Distortion" },
            { key = "sand_filter",  zh = "夏季沙尘暴风沙护眼隐藏",    en = "Hide Sandstorm Dust" },
            { key = "shadow_night", zh = "夜间火堆/提灯反方向真投影", en = "Night Torch/Fire Shadows" },
        }
        for i = 1, #bool_filter_defs do
            AddBoolTuneRow(tab7, -91 - (i - 1) * 30, 26, bool_filter_defs[i])
        end

        AddGroupResetButton(tab7, -262, "重置本页海面波光与原版滤镜为默认", "Reset Ocean & Vanilla Filters",
            { "glint_str", "glint_density", "glint_grain", "glint_soft", "vanilla_cc", "snow_cap", "sanity_color", "distort_free", "sand_filter", "shadow_night" })
    end

    -- =========================================================================
    -- TAB 8: Extensions — HS_EXT_V1 Standard Shader Plugin Manager & GPU Profiler
    --        Zero-boilerplate auto-UI generated from third-party plugin metadata!
    -- =========================================================================
    local tab8 = tab_roots[8]
    local ext_selected_idx = 1
    local ext_param_page = 1
    local EXT_ROWS_PER_PAGE = 8

    -- Top summary + Master Bypass toggle
    local ext_sum_bg = tab8:AddChild(Image("images/global.xml", "square.tex"))
    ext_sum_bg:ScaleToSize(262, 24)
    ext_sum_bg:SetPosition(-58, 152, 0)
    ext_sum_bg:SetTint(0.10, 0.16, 0.24, 0.90)

    local ext_sum_txt = tab8:AddChild(Text(FONT_BODY, 13, ""))
    ext_sum_txt:SetRegionSize(254, 20)
    ext_sum_txt:SetHAlign(G.ANCHOR_LEFT)
    ext_sum_txt:SetPosition(-58, 152, 0)
    ext_sum_txt:SetColour(0.76, 0.92, 1.0, 0.98)

    local ext_byp_bg = tab8:AddChild(Image("images/global.xml", "square.tex"))
    ext_byp_bg:ScaleToSize(110, 24)
    ext_byp_bg:SetPosition(134, 152, 0)
    ext_byp_bg:SetTint(0.14, 0.30, 0.20, 0.94)

    local ext_byp_btn = tab8:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_byp_btn.scale_on_focus = false
    ext_byp_btn.move_on_click = false
    ext_byp_btn:ForceImageSize(110, 24)
    ext_byp_btn:SetPosition(134, 152, 0)
    ext_byp_btn:SetText(is_zh and "扩展:运行中" or "Ext: Active")
    ext_byp_btn:SetTextSize(13)
    ext_byp_btn:SetFont(FONT_BODY)
    ext_byp_btn:SetTextColour(0.68, 1.0, 0.80, 1.0)
    ext_byp_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" or type(api.SetExtMasterBypass) ~= "function" then return end
        local cur = api.GetExtSnapshot()
        api.SetExtMasterBypass(not (cur.master_bypass == true))
        if panel_refresh_fn then panel_refresh_fn() end
    end)

    -- Core baseline GPU comparison line
    local ext_core_bg = tab8:AddChild(Image("images/global.xml", "square.tex"))
    ext_core_bg:ScaleToSize(W - 22, 18)
    ext_core_bg:SetPosition(0, 128, 0)
    ext_core_bg:SetTint(0.08, 0.10, 0.14, 0.84)

    local ext_core_txt = tab8:AddChild(Text(FONT_NUM, 12, ""))
    ext_core_txt:SetRegionSize(W - 30, 16)
    ext_core_txt:SetPosition(0, 128, 0)
    ext_core_txt:SetColour(0.68, 0.78, 0.88, 0.94)

    -- Empty-state friendly card (shown when 0 third-party plugins registered)
    local ext_empty_root = tab8:AddChild(Widget("HS_ExtEmpty"))
    local ext_empty_bg = ext_empty_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_empty_bg:ScaleToSize(W - 22, 150)
    ext_empty_bg:SetPosition(0, 20, 0)
    ext_empty_bg:SetTint(0.10, 0.12, 0.17, 0.86)

    local ext_empty_txt = ext_empty_root:AddChild(Text(FONT_BODY, 14, ""))
    ext_empty_txt:SetRegionSize(W - 42, 132)
    ext_empty_txt:SetPosition(0, 20, 0)
    ext_empty_txt:SetColour(0.80, 0.86, 0.94, 0.94)

    -- Active Plugin Browser & Controls container
    local ext_body_root = tab8:AddChild(Widget("HS_ExtBody"))

    -- Selector strip: [<]  [1/N] Name v1.0  [>]
    local ext_prev_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_prev_bg:ScaleToSize(30, 24)
    ext_prev_bg:SetPosition(-174, 100, 0)
    ext_prev_bg:SetTint(0.16, 0.18, 0.24, 0.94)

    local ext_prev_btn = ext_body_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_prev_btn.scale_on_focus = false
    ext_prev_btn.move_on_click = false
    ext_prev_btn:ForceImageSize(30, 24)
    ext_prev_btn:SetPosition(-174, 100, 0)
    ext_prev_btn:SetText("<")
    ext_prev_btn:SetTextSize(16)
    ext_prev_btn:SetFont(FONT_NUM)
    ext_prev_btn:SetTextColour(0.92, 0.94, 1.0, 1.0)
    ext_prev_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" then return end
        local n = #(api.GetExtSnapshot().plugins or {})
        if n > 0 then
            ext_selected_idx = ((ext_selected_idx - 2) % n) + 1
            ext_param_page = 1
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    local ext_sel_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_sel_bg:ScaleToSize(310, 24)
    ext_sel_bg:SetPosition(0, 100, 0)
    ext_sel_bg:SetTint(0.14, 0.16, 0.22, 0.92)

    local ext_sel_txt = ext_body_root:AddChild(Text(FONT_BODY, 14, ""))
    ext_sel_txt:SetRegionSize(302, 20)
    ext_sel_txt:SetPosition(0, 100, 0)
    ext_sel_txt:SetColour(1.0, 0.92, 0.66, 1.0)

    local ext_next_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_next_bg:ScaleToSize(30, 24)
    ext_next_bg:SetPosition(174, 100, 0)
    ext_next_bg:SetTint(0.16, 0.18, 0.24, 0.94)

    local ext_next_btn = ext_body_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_next_btn.scale_on_focus = false
    ext_next_btn.move_on_click = false
    ext_next_btn:ForceImageSize(30, 24)
    ext_next_btn:SetPosition(174, 100, 0)
    ext_next_btn:SetText(">")
    ext_next_btn:SetTextSize(16)
    ext_next_btn:SetFont(FONT_NUM)
    ext_next_btn:SetTextColour(0.92, 0.94, 1.0, 1.0)
    ext_next_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" then return end
        local n = #(api.GetExtSnapshot().plugins or {})
        if n > 0 then
            ext_selected_idx = (ext_selected_idx % n) + 1
            ext_param_page = 1
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    -- Plugin Action Row: [Enable/Disable]  [Stage + Live GPU ms]  [Reset]
    local ext_en_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_en_bg:ScaleToSize(92, 24)
    ext_en_bg:SetPosition(-143, 70, 0)
    ext_en_bg:SetTint(0.14, 0.28, 0.20, 0.94)

    local ext_en_btn = ext_body_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_en_btn.scale_on_focus = false
    ext_en_btn.move_on_click = false
    ext_en_btn:ForceImageSize(92, 24)
    ext_en_btn:SetPosition(-143, 70, 0)
    ext_en_btn:SetTextSize(13)
    ext_en_btn:SetFont(FONT_BODY)
    ext_en_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" or type(api.SetExtPluginEnabled) ~= "function" then return end
        local list = api.GetExtSnapshot().plugins or {}
        local p = list[ext_selected_idx]
        if p and not p.faulted then
            api.SetExtPluginEnabled(p.id, not p.enabled)
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    local ext_gpu_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_gpu_bg:ScaleToSize(192, 24)
    ext_gpu_bg:SetPosition(3, 70, 0)
    ext_gpu_bg:SetTint(0.08, 0.13, 0.19, 0.90)

    local ext_gpu_txt = ext_body_root:AddChild(Text(FONT_NUM, 12, ""))
    ext_gpu_txt:SetRegionSize(186, 20)
    ext_gpu_txt:SetPosition(3, 70, 0)
    ext_gpu_txt:SetColour(0.60, 0.96, 0.84, 0.98)

    local ext_rst_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_rst_bg:ScaleToSize(86, 24)
    ext_rst_bg:SetPosition(146, 70, 0)
    ext_rst_bg:SetTint(0.24, 0.18, 0.12, 0.92)

    local ext_rst_btn = ext_body_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_rst_btn.scale_on_focus = false
    ext_rst_btn.move_on_click = false
    ext_rst_btn:ForceImageSize(86, 24)
    ext_rst_btn:SetPosition(146, 70, 0)
    ext_rst_btn:SetText(is_zh and "重置参数" or "Reset")
    ext_rst_btn:SetTextSize(13)
    ext_rst_btn:SetFont(FONT_BODY)
    ext_rst_btn:SetTextColour(1.0, 0.88, 0.62, 1.0)
    ext_rst_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" or type(api.ResetExtPlugin) ~= "function" then return end
        local list = api.GetExtSnapshot().plugins or {}
        local p = list[ext_selected_idx]
        if p then
            api.ResetExtPlugin(p.id)
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    -- Meta / Author / Fault line
    local ext_meta_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_meta_bg:ScaleToSize(W - 22, 24)
    ext_meta_bg:SetPosition(0, 40, 0)
    ext_meta_bg:SetTint(0.09, 0.11, 0.15, 0.84)

    local ext_meta_txt = ext_body_root:AddChild(Text(FONT_BODY, 12, ""))
    ext_meta_txt:SetRegionSize(W - 30, 22)
    ext_meta_txt:SetPosition(0, 40, 0)
    ext_meta_txt:SetColour(0.76, 0.82, 0.90, 0.94)

    -- 8-row reusable dynamic parameter pool (float steppers + bool toggles)
    local ext_param_rows = {}
    for r = 1, EXT_ROWS_PER_PAGE do
        local row_idx = r
        local ry = 10 - (r - 1) * 32
        local rroot = ext_body_root:AddChild(Widget("HS_ExtParamRow_" .. r))

        local rbg = rroot:AddChild(Image("images/global.xml", "square.tex"))
        rbg:ScaleToSize(W - 22, 28)
        rbg:SetPosition(0, ry, 0)
        rbg:SetTint(0.11, 0.12, 0.16, 0.80)

        local rlbl = rroot:AddChild(Text(FONT_BODY, 14, ""))
        rlbl:SetRegionSize(214, 24)
        rlbl:SetHAlign(G.ANCHOR_LEFT)
        rlbl:SetPosition(-76, ry, 0)
        rlbl:SetColour(0.90, 0.92, 0.96, 1.0)

        local mbg = rroot:AddChild(Image("images/global.xml", "square.tex"))
        mbg:ScaleToSize(28, 22)
        mbg:SetPosition(66, ry, 0)
        mbg:SetTint(0.19, 0.20, 0.26, 0.94)

        local mbtn = rroot:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        mbtn.scale_on_focus = false
        mbtn.move_on_click = false
        mbtn:ForceImageSize(28, 22)
        mbtn:SetPosition(66, ry, 0)
        mbtn:SetText("-")
        mbtn:SetTextSize(16)
        mbtn:SetFont(FONT_NUM)
        mbtn:SetTextColour(0.92, 0.92, 0.95, 1.0)

        local vbtn = rroot:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        vbtn.scale_on_focus = false
        vbtn.move_on_click = false
        vbtn:ForceImageSize(68, 22)
        vbtn:SetPosition(116, ry, 0)
        vbtn:SetTextSize(13)
        vbtn:SetFont(FONT_NUM)
        vbtn:SetTextColour(1.0, 0.90, 0.60, 1.0)

        local pbg = rroot:AddChild(Image("images/global.xml", "square.tex"))
        pbg:ScaleToSize(28, 22)
        pbg:SetPosition(166, ry, 0)
        pbg:SetTint(0.19, 0.20, 0.26, 0.94)

        local pbtn = rroot:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        pbtn.scale_on_focus = false
        pbtn.move_on_click = false
        pbtn:ForceImageSize(28, 22)
        pbtn:SetPosition(166, ry, 0)
        pbtn:SetText("+")
        pbtn:SetTextSize(16)
        pbtn:SetFont(FONT_NUM)
        pbtn:SetTextColour(0.92, 0.92, 0.95, 1.0)

        local function GetBoundParam()
            if type(api.GetExtSnapshot) ~= "function" then return nil, nil end
            local list = api.GetExtSnapshot().plugins or {}
            local p = list[ext_selected_idx]
            if not p then return nil, nil end
            local idx = (ext_param_page - 1) * EXT_ROWS_PER_PAGE + row_idx
            return p, p.params[idx]
        end

        mbtn:SetOnClick(function()
            local p, pd = GetBoundParam()
            if not (p and pd and type(api.SetExtPluginParam) == "function") then return end
            local cur = p.values[pd.key] or pd.default
            api.SetExtPluginParam(p.id, pd.key, cur - pd.step)
            if panel_refresh_fn then panel_refresh_fn() end
        end)
        pbtn:SetOnClick(function()
            local p, pd = GetBoundParam()
            if not (p and pd and type(api.SetExtPluginParam) == "function") then return end
            local cur = p.values[pd.key] or pd.default
            api.SetExtPluginParam(p.id, pd.key, cur + pd.step)
            if panel_refresh_fn then panel_refresh_fn() end
        end)
        vbtn:SetOnClick(function()
            local p, pd = GetBoundParam()
            if not (p and pd and type(api.SetExtPluginParam) == "function") then return end
            if pd.type == "bool" then
                local cur = (p.values[pd.key] or 0) > 0.5
                api.SetExtPluginParam(p.id, pd.key, not cur)
            else
                api.SetExtPluginParam(p.id, pd.key, pd.default)
            end
            if panel_refresh_fn then panel_refresh_fn() end
        end)

        rroot:Hide()
        ext_param_rows[r] = { root = rroot, lbl = rlbl, mbg = mbg, mbtn = mbtn, vbtn = vbtn, pbg = pbg, pbtn = pbtn }
    end

    -- Pager strip (only visible if plugin has >8 params)
    local ext_pager_root = ext_body_root:AddChild(Widget("HS_ExtPager"))
    local ext_pg_prev = ext_pager_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_pg_prev.scale_on_focus = false
    ext_pg_prev.move_on_click = false
    ext_pg_prev:ForceImageSize(72, 22)
    ext_pg_prev:SetPosition(-110, -254, 0)
    ext_pg_prev:SetText("<")
    ext_pg_prev:SetTextSize(13)
    ext_pg_prev:SetFont(FONT_BODY)
    ext_pg_prev:SetOnClick(function()
        if ext_param_page > 1 then
            ext_param_page = ext_param_page - 1
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    local ext_pg_txt = ext_pager_root:AddChild(Text(FONT_NUM, 12, "1 / 1"))
    ext_pg_txt:SetPosition(0, -254, 0)
    ext_pg_txt:SetColour(0.78, 0.84, 0.92, 0.94)

    local ext_pg_next = ext_pager_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_pg_next.scale_on_focus = false
    ext_pg_next.move_on_click = false
    ext_pg_next:ForceImageSize(72, 22)
    ext_pg_next:SetPosition(110, -254, 0)
    ext_pg_next:SetText(">")
    ext_pg_next:SetTextSize(13)
    ext_pg_next:SetFont(FONT_BODY)
    ext_pg_next:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" then return end
        local list = api.GetExtSnapshot().plugins or {}
        local p = list[ext_selected_idx]
        if p then
            local pages = math.max(1, math.ceil(#p.params / EXT_ROWS_PER_PAGE))
            if ext_param_page < pages then
                ext_param_page = ext_param_page + 1
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end
    end)
    ext_pager_root:Hide()

    local function RefreshExtTab(zh_now)
        if type(api.GetExtSnapshot) ~= "function" then return end
        local es = api.GetExtSnapshot()
        local list = es.plugins or {}
        local n = #list

        ext_sum_txt:SetString(zh_now
            and string.format("HS_EXT_V%d 扩展：已装 %d · 运行 %d · %.2fms", es.abi_version or 1, n, es.active_count or 0, es.total_gpu_ms or 0)
            or string.format("HS_EXT_V%d: %d inst · %d on · %.2fms", es.abi_version or 1, n, es.active_count or 0, es.total_gpu_ms or 0))

        if es.master_bypass then
            ext_byp_bg:SetTint(0.42, 0.16, 0.12, 0.96)
            ext_byp_btn:SetText(zh_now and "已全部旁路" or "BYPASSED")
            ext_byp_btn:SetTextColour(1.0, 0.72, 0.62, 1.0)
        else
            ext_byp_bg:SetTint(0.12, 0.28, 0.18, 0.94)
            ext_byp_btn:SetText(zh_now and "总开关:运行" or "Ext: Active")
            ext_byp_btn:SetTextColour(0.64, 1.0, 0.78, 1.0)
        end

        local cb = es.core_bloom or {}
        local cf = es.core_final or {}
        if cb.has_dll or cf.has_dll then
            local mode_s = (cf.is_hw or cb.is_hw) and "GPU" or "CPU"
            ext_core_txt:SetString(zh_now
                and string.format("核心基准(%s)：柔光 %.2fms · 终合 %.2fms · 扩展合计 %.2fms", mode_s, cb.ema_ms or 0, cf.ema_ms or 0, es.total_gpu_ms or 0)
                or string.format("Core(%s): Bloom %.2fms · Final %.2fms · Ext %.2fms", mode_s, cb.ema_ms or 0, cf.ema_ms or 0, es.total_gpu_ms or 0))
        else
            ext_core_txt:SetString(zh_now
                and "未加载新版 dinput8.dll：插件纯 Lua 正常运行（装 DLL 可看硬件 GPU 耗时）"
                or "No dinput8.dll: Plugins run in pure Lua (install DLL for HW GPU ms)")
        end

        if n == 0 then
            ext_empty_root:Show()
            ext_body_root:Hide()
            ext_empty_txt:SetString(zh_now
                and "当前未检测到第三方 HS_EXT_V1 扩展模组\n\n第三方开发者只需通过 GLOBAL.HyperSmooth.RegisterPlugin\n注册算法 .ksh 与参数表，即可自动生成本页实时滑块、\n接入标准昼夜/雨雪上下文总线并获得异常熔断保护。\n\n控制台输入 c_hsext() 可查看管线与拒绝日志"
                or "No third-party HS_EXT_V1 shader plugins detected.\n\nAuthors register a .ksh + param schema via\nGLOBAL.HyperSmooth.RegisterPlugin() to get automatic\nlive sliders, shared world context uniforms, fault\nisolation, and per-pass GPU timing.\n\nConsole: c_hsext()")
            return
        end

        ext_empty_root:Hide()
        ext_body_root:Show()
        if ext_selected_idx > n then ext_selected_idx = n end
        if ext_selected_idx < 1 then ext_selected_idx = 1 end
        local p = list[ext_selected_idx]

        local pname = zh_now and p.name_zh or p.name_en
        ext_sel_txt:SetString(string.format("[%d/%d] %s  v%s", ext_selected_idx, n, pname, p.version or "1.0"))
        ext_rst_btn:SetText(zh_now and "重置参数" or "Reset")

        if p.faulted then
            ext_en_bg:SetTint(0.44, 0.10, 0.10, 0.96)
            ext_en_btn:SetText(zh_now and "已熔断隔离" or "ISOLATED")
            ext_en_btn:SetTextColour(1.0, 0.65, 0.65, 1.0)
        elseif p.enabled and not es.master_bypass then
            ext_en_bg:SetTint(0.12, 0.34, 0.20, 0.96)
            ext_en_btn:SetText(zh_now and "已启用" or "ENABLED")
            ext_en_btn:SetTextColour(0.54, 1.0, 0.72, 1.0)
        elseif p.enabled and es.master_bypass then
            ext_en_bg:SetTint(0.32, 0.24, 0.10, 0.92)
            ext_en_btn:SetText(zh_now and "旁路暂停" or "BYPASSED")
            ext_en_btn:SetTextColour(1.0, 0.86, 0.50, 1.0)
        else
            ext_en_bg:SetTint(0.16, 0.16, 0.20, 0.90)
            ext_en_btn:SetText(zh_now and "未启用" or "DISABLED")
            ext_en_btn:SetTextColour(0.72, 0.76, 0.82, 0.92)
        end

        local gpu = p.gpu or {}
        local g_label
        if gpu.has_dll then
            g_label = string.format("%s · %.2fms %s", p.stage, gpu.ema_ms or 0, gpu.is_hw and "GPU" or "CPU")
        else
            g_label = string.format("%s · Lua", p.stage)
        end
        ext_gpu_txt:SetString(g_label)

        if p.faulted then
            ext_meta_bg:SetTint(0.30, 0.09, 0.09, 0.92)
            ext_meta_txt:SetColour(1.0, 0.72, 0.72, 1.0)
            ext_meta_txt:SetString((zh_now and "熔断原因：" or "Fault: ") .. tostring(p.fault_msg or "error"))
        else
            ext_meta_bg:SetTint(0.09, 0.11, 0.15, 0.84)
            ext_meta_txt:SetColour(0.76, 0.82, 0.90, 0.94)
            local desc = zh_now and p.desc_zh or p.desc_en
            local excl = p.exclusive_group and ((zh_now and " · 互斥:" or " · grp:") .. p.exclusive_group) or ""
            ext_meta_txt:SetString(string.format("%s · by %s%s%s", p.id, p.author or "?", excl, (#desc > 0) and (" — " .. desc) or ""))
        end

        local params = p.params or {}
        local total_pages = math.max(1, math.ceil(#params / EXT_ROWS_PER_PAGE))
        if ext_param_page > total_pages then ext_param_page = total_pages end
        local start_i = (ext_param_page - 1) * EXT_ROWS_PER_PAGE
        for r = 1, EXT_ROWS_PER_PAGE do
            local rw = ext_param_rows[r]
            local pd = params[start_i + r]
            if pd then
                rw.root:Show()
                rw.lbl:SetString(zh_now and pd.zh or pd.en)
                local v = p.values[pd.key]
                if v == nil then v = pd.default end
                if pd.type == "bool" then
                    rw.mbg:Hide(); rw.mbtn:Hide(); rw.pbg:Hide(); rw.pbtn:Hide()
                    local on = v > 0.5
                    rw.vbtn:SetText(on and (zh_now and "开" or "ON") or (zh_now and "关" or "OFF"))
                    rw.vbtn:SetTextColour(on and 0.48 or 0.70, on and 1.0 or 0.74, on and 0.68 or 0.80, 1.0)
                else
                    rw.mbg:Show(); rw.mbtn:Show(); rw.pbg:Show(); rw.pbtn:Show()
                    rw.vbtn:SetText(string.format(pd.fmt or "%.2f", v * (pd.scale or 1)))
                    rw.vbtn:SetTextColour(1.0, 0.90, 0.60, 1.0)
                end
            else
                rw.root:Hide()
            end
        end

        if total_pages > 1 then
            ext_pager_root:Show()
            ext_pg_prev:SetText(zh_now and "< 上页" or "< Prev")
            ext_pg_next:SetText(zh_now and "下页 >" or "Next >")
            ext_pg_txt:SetString(string.format("%d / %d", ext_param_page, total_pages))
        else
            ext_pager_root:Hide()
        end
    end

    -- =========================================================================
    -- Bottom Action Bar: 1-Click Vanilla Pure Restore + Close
    -- =========================================================================
    local bot_y = -H * 0.5 + 24
    local reset_bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    reset_bg:ScaleToSize(206, 26)
    reset_bg:SetPosition(-86, bot_y, 0)
    reset_bg:SetTint(0.32, 0.24, 0.12, 0.92)

    local reset_btn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    reset_btn.scale_on_focus = false
    reset_btn.move_on_click = false
    reset_btn:ForceImageSize(206, 26)
    reset_btn:SetPosition(-86, bot_y, 0)
    reset_btn:SetText(is_zh and "一键恢复原版纯净" or "Restore Vanilla Pure")
    reset_btn:SetTextSize(16)
    reset_btn:SetFont(FONT_BODY)
    reset_btn:SetTextColour(1.0, 0.88, 0.58, 1.0)
    reset_btn:SetTextFocusColour(1.0, 0.96, 0.78, 1.0)
    reset_btn:SetOnClick(function()
        api.SetPreset(0)
        if panel_refresh_fn then panel_refresh_fn() end
    end)

    local hide_bg = panel:AddChild(Image("images/global.xml", "square.tex"))
    hide_bg:ScaleToSize(156, 26)
    hide_bg:SetPosition(104, bot_y, 0)
    hide_bg:SetTint(0.16, 0.16, 0.20, 0.92)

    local hide_btn = panel:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    hide_btn.scale_on_focus = false
    hide_btn.move_on_click = false
    hide_btn:ForceImageSize(156, 26)
    hide_btn:SetPosition(104, bot_y, 0)
    hide_btn:SetText(is_zh and "隐藏面板" or "Close Panel")
    hide_btn:SetTextSize(16)
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
        local zh_now = (snap.ui_zh ~= nil) and (snap.ui_zh == true) or IsChineseLocale()
        local exp_ver = tostring(snap.expected_dll_ver or "2.8.0")
        if snap.dll_status == "ok" then
            dll_bg:SetTint(0.08, 0.20, 0.14, 0.86)
            dll_txt:SetColour(0.50, 1.0, 0.68, 0.96)
            dll_txt:SetString(zh_now
                and string.format("已直连 bin64/dinput8.dll v%s (%s)", tostring(snap.dll_ver or exp_ver), snap.has_world_studio and "原生3D光影+长影+海面就绪" or "基础桥接")
                or string.format("bin64/dinput8.dll v%s Connected (%s)", tostring(snap.dll_ver or exp_ver), snap.has_world_studio and "Native 3D/Shadow/Sea OK" or "Base Bridge"))
        elseif snap.dll_status == "outdated" then
            dll_bg:SetTint(0.46, 0.24, 0.06, 0.94)
            dll_txt:SetColour(1.0, 0.90, 0.42, 1.0)
            dll_txt:SetString(zh_now
                and string.format("旧版 dinput8.dll (%s)，请手动替换 v%s 防崩溃", tostring(snap.dll_ver or "old"), exp_ver)
                or string.format("Outdated dinput8.dll (%s) -> Please Update to v%s", tostring(snap.dll_ver or "old"), exp_ver))
        else
            dll_bg:SetTint(0.36, 0.19, 0.07, 0.90)
            dll_txt:SetColour(1.0, 0.82, 0.48, 1.0)
            dll_txt:SetString(zh_now
                and string.format("未检测到新版 dinput8.dll (后处理可用 · 3D光影需装v%s)", exp_ver)
                or string.format("No v%s dinput8.dll (PostFX OK · 3D/Shadow needs DLL)", exp_ver))
        end

        local net_s = ""
        if snap.net_connected and (snap.ping_avg or -1) >= 0 then
            if zh_now then
                net_s = ((snap.loss_cur or 0) >= 0.5)
                    and string.format(" | 延迟%dms(%.0f%%)", snap.ping_avg, snap.loss_cur)
                    or string.format(" | 延迟%dms", snap.ping_avg)
            else
                net_s = ((snap.loss_cur or 0) >= 0.5)
                    and string.format(" | %dms(%.0f%%)", snap.ping_avg, snap.loss_cur)
                    or string.format(" | %dms", snap.ping_avg)
            end
        end
        local t1 = zh_now
            and string.format("[%s] 帧率%.0f · 低帧%.0f · 忙时%.1fms%s",
                tostring(snap.render_backend or "D3D9"),
                snap.avg_fps or 0, snap.low1_fps or 0, snap.busy_ms or 0, net_s)
            or string.format("[%s] FPS %.0f · 1%%L %.0f · Busy %.1fms%s",
                tostring(snap.render_backend or "D3D9"),
                snap.avg_fps or 0, snap.low1_fps or 0, snap.busy_ms or 0, net_s)
        if (snap.vram_mb or 0) > 0.5 then
            t1 = t1 .. string.format(" · %.0fMB", snap.vram_mb)
        end
        tele1:SetString(t1)

        local t2
        if zh_now then
            local lat_s = snap.d3d9ex_active
                and string.format("队列%d/%d", snap.cur_lat or 1, snap.orig_lat or 3)
                or (snap.has_dll and string.format("队列%d", snap.cur_lat or 3) or "无DLL")
            local vb_s = ((snap.vb_hit_pct or 0) > 0)
                and string.format("缓冲池%.0f%%", snap.vb_hit_pct)
                or string.format("光照%.1fx", snap.lm_scale or 0.5)
            t2 = string.format("%s · %s · 优先%d · 异步存%d · 防漏+%d",
                lat_s, vb_s, snap.rpc_imm or 0, snap.async_save_ok or 0, snap.flick_rescues or 0)
        else
            local lat_s = snap.d3d9ex_active
                and string.format("Lat %d/%d", snap.cur_lat or 1, snap.orig_lat or 3)
                or (snap.has_dll and string.format("Lat %d", snap.cur_lat or 3) or "NoDLL")
            local vb_s = ((snap.vb_hit_pct or 0) > 0)
                and string.format("VB %.0f%%", snap.vb_hit_pct)
                or string.format("LM %.1fx", snap.lm_scale or 0.5)
            t2 = string.format("%s · %s · QoS %d · Save %d(%d) · Flick+%d",
                lat_s, vb_s, snap.rpc_imm or 0, snap.async_save_ok or 0, snap.async_save_comp or 0, snap.flick_rescues or 0)
        end
        tele2:SetString(t2)
        if active_tab == 8 then
            RefreshExtTab(zh_now)
        end
    end

    local function RefreshStates()
        local snap = api.GetSnapshot()
        local zh_now = (snap.ui_zh ~= nil) and (snap.ui_zh == true) or IsChineseLocale()
        local fl = snap.flags or {}
        local tn = snap.tune or {}
        local slots = snap.slot_saved or {}

        title:SetString(zh_now and "HyperSmooth v2.8.0 画质光影工坊" or "HyperSmooth v2.8.0 Visual Studio")
        reset_btn:SetText(zh_now and "一键恢复原版纯净" or "Restore Vanilla Pure")
        hide_btn:SetText(zh_now and "隐藏面板" or "Close Panel")
        share_open_btn:SetText(zh_now and "分享 / 导入画质方案码 (HS1 / JSON)" or "Share / Import Preset Code (HS1 / JSON)")
        sc_title:SetString(zh_now and "HyperSmooth · 画质方案分享" or "HyperSmooth · Share Preset")
        sc_copy_btn:SetText(zh_now and "复制分享码" or "Copy Code")
        sc_json_btn:SetText(zh_now and "导出全部方案至 hypersmooth_presets.json" or "Export All to hypersmooth_presets.json")
        sc_chk_btn:SetText(zh_now and "导入前校验" or "Validate")
        sc_go_btn:SetText(zh_now and "安全导入并应用" or "Safe Import")
        sc_undo_btn:SetText(zh_now and "撤销导入" or "Undo")
        RefreshShareCardHeader(snap, zh_now)
        UpdateTab6SubVisual(zh_now)
        RefreshExtTab(zh_now)

        for i = 1, #section_banners do
            local sb = section_banners[i]
            sb.txt:SetString(zh_now and sb.zh or sb.en)
        end

        for i = 1, #preset_btns do
            local pb = preset_btns[i]
            pb.btn:SetText(zh_now and preset_labels_zh[pb.id] or preset_labels_en[pb.id])
            if snap.preset == pb.id then
                pb.bg:SetTint(0.45, 0.34, 0.14, 0.95)
                pb.btn:SetTextColour(1.0, 0.92, 0.62, 1.0)
            else
                pb.bg:SetTint(0.14, 0.14, 0.18, 0.88)
                pb.btn:SetTextColour(0.74, 0.77, 0.82, 0.95)
            end
        end

        for i = 1, #tab_btns do
            local tb = tab_btns[i]
            tb.btn:SetText(zh_now and tb.zh or tb.en)
            if active_tab == tb.id then
                tb.bg:SetTint(0.24, 0.36, 0.48, 0.96)
                tb.btn:SetTextColour(0.98, 0.96, 0.78, 1.0)
            else
                tb.bg:SetTint(0.12, 0.13, 0.17, 0.86)
                tb.btn:SetTextColour(0.72, 0.76, 0.82, 0.92)
            end
        end

        for i = 1, 3 do
            local sw = slot_widgets[i]
            local has_s = slots[i] == true
            sw.sbtn:SetText(zh_now and ("存槽" .. i) or ("Sav" .. i))
            sw.lbtn:SetText(zh_now and (has_s and ("读槽" .. i) or ("空槽" .. i)) or (has_s and ("Lod" .. i) or ("Emp" .. i)))
            if has_s then
                sw.lbg:SetTint(0.12, 0.26, 0.32, 0.94)
                sw.lbtn:SetTextColour(0.56, 0.96, 1.0, 1.0)
            else
                sw.lbg:SetTint(0.12, 0.13, 0.16, 0.76)
                sw.lbtn:SetTextColour(0.52, 0.56, 0.62, 0.84)
            end
        end

        if snap.bcas_coexist == true then
            coexist_bg:SetTint(0.42, 0.28, 0.08, 0.92)
            coexist_txt:SetColour(1.0, 0.88, 0.48, 1.0)
            coexist_txt:SetString(zh_now
                and "检测到 BCAS Studio 共存：后处理与原生光影已自动让位"
                or "BCAS Studio Detected: PostFX & World Studio Auto-Yielded")
        else
            coexist_bg:SetTint(0.10, 0.18, 0.24, 0.86)
            coexist_txt:SetColour(0.74, 0.92, 1.0, 0.95)
            coexist_txt:SetString(zh_now
                and "单 Pass 全自由电影调色 (零硬编码覆盖 · 点击数值单项复位)"
                or "1-Pass Free Cinema Grading (Zero Hardcodes · Click Value Reset)")
        end

        for i = 1, #mood_btns do
            local mb = mood_btns[i]
            mb.btn:SetText(zh_now and mb.zh or mb.en)
        end
        for i = 1, #tab_reset_btns do
            local rb = tab_reset_btns[i]
            rb.btn:SetText(zh_now and rb.zh or rb.en)
        end

        for i = 1, #all_steppers do
            local sw = all_steppers[i]
            sw.lbl:SetString(zh_now and sw.zh or sw.en)
            local rawv = tonumber(tn[sw.key]) or 0
            sw.val_btn:SetText(string.format(sw.fmt, rawv * sw.scale))
            local active = true
            if sw.flag ~= nil then
                active = (fl[sw.flag] == true)
            elseif sw.native_ws then
                active = (snap.has_world_studio == true) and (snap.preset ~= 0)
            end
            if active then
                sw.bg:SetTint(0.11, 0.14, 0.18, 0.82)
                sw.lbl:SetColour(0.92, 0.95, 0.98, 1.0)
                sw.val_btn:SetTextColour(0.45, 1.0, 0.72, 1.0)
            else
                sw.bg:SetTint(0.10, 0.10, 0.13, 0.68)
                sw.lbl:SetColour(0.62, 0.65, 0.70, 0.88)
                sw.val_btn:SetTextColour(0.56, 0.60, 0.66, 0.85)
            end
        end

        local on_txt  = zh_now and "[开]" or "[ON]"
        local off_txt = zh_now and "[关]" or "[OFF]"

        for i = 1, #bool_filter_widgets do
            local bw = bool_filter_widgets[i]
            bw.lbl:SetString(zh_now and bw.zh or bw.en)
            local on = (tonumber(tn[bw.key]) or 0) > 0.5
            bw.val:SetString(on and on_txt or off_txt)
            if on then
                bw.bg:SetTint(0.12, 0.22, 0.16, 0.84)
                bw.lbl:SetColour(0.92, 0.98, 0.94, 1.0)
                bw.val:SetColour(0.42, 1.0, 0.62, 1.0)
            else
                bw.bg:SetTint(0.11, 0.12, 0.15, 0.74)
                bw.lbl:SetColour(0.75, 0.78, 0.82, 0.92)
                bw.val:SetColour(0.55, 0.58, 0.64, 0.88)
            end
        end

        for i = 1, #row_widgets do
            local rw = row_widgets[i]
            rw.lbl:SetString(zh_now and rw.zh or rw.en)
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
            ew.lbl:SetString(zh_now and ew.zh or ew.en)
            if ew.key == "hudmode" then
                local show = (snap.hud_show ~= false)
                local detail = (snap.hud_mode == "detail")
                local htxt = not show
                    and (zh_now and "[隐藏]" or "[Off]")
                    or (detail and (zh_now and "[详细]" or "[Detail]") or (zh_now and "[简洁]" or "[Compact]"))
                ew.val:SetString(htxt)
                if show then
                    ew.lbl:SetColour(0.88, 0.96, 1.0, 1.0)
                    ew.val:SetColour(0.42, 0.92, 1.0, 1.0)
                else
                    ew.lbl:SetColour(0.68, 0.72, 0.76, 0.90)
                    ew.val:SetColour(0.55, 0.58, 0.64, 0.88)
                end
            else
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
        local ui_zh = IsChineseLocale()
        G.print(ui_zh
            and ("[HyperSmooth] 实时画质光影工坊 -> " .. (vis and "已打开" or "已关闭"))
            or ("[HyperSmooth] Live Visual Studio Panel -> " .. (vis and "OPEN" or "CLOSED")))
    end
end)
