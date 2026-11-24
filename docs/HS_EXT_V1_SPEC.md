# HyperSmooth Standard Shader Extension Specification (`HS_EXT_V1`)

> **设计原则：兼容标准，而不是兼容每一个模组。**  
> 第三方开发者只需专注自己的着色器数学算法与声明式参数表，不必重复研究或适配科雷底层 `PostProcessor`、昼夜/气候 Lua 状态、UI 控件、存盘逻辑与 GPU 性能监控。

---

## 1. 四层总体架构

```text
[ 第三方光影算法 ]    [ 第三方锐化算法 ]    [ 第三方视觉特效 ]
          \                  |                  /
           ▼                 ▼                 ▼
┌───────────────────────────────────────────────────────────┐
│              标准 Shader 扩展接口 (HS_EXT_V1)              │
│       参数注册 · 6 执行阶段 · 互斥组 · 生命周期 · 熔断隔离    │
└───────────────────────────────────────────────────────────┘
                             ▼
┌───────────────────────────────────────────────────────────┐
│                    HyperSmooth Core                       │
│   渲染图拓扑调度 · 标准上下文总线 · 240Hz 引擎 · 硬件 GPU 计费 │
└───────────────────────────────────────────────────────────┘
                             ▼
┌───────────────────────────────────────────────────────────┐
│                    玩家实时控制台                           │
│    Tab 8 自动滑块 · 实时 GPU 耗时(ms) · 总旁路 · JSON 持久化 │
└───────────────────────────────────────────────────────────┘
```

### 明确的能力与引擎边界
- **Tier 1（纯 Lua 创意工坊标准，现已完整落地）**：无需安装 `dinput8.dll` 即可 100% 运行。适合全屏锐化（RCAS/Anime4K-Lite/LumaSharpen）、电影/胶片调色、自定义多 Pass 离屏泛光/模糊、CRT/水墨/像素化/描边/镜头滤镜等所有基于 `PostProcessor` 颜色帧与可选 `BloomSampler`/`Smoke`/自定义贴图的算法。
- **Native 硬件增强（安装 `dinput8.dll` 后自动启用）**：对所有带 `// @HS_EXT_ID:` 标记或注册了 Uniform 探针的第三方 Pass，使用 **4 帧异步 `GL_TIME_ELAPSED` 硬件计时环（0.000 ms 管线停顿）** 在控制台 Tab 8 实时展示每个插件的真实 GPU 耗时（ms）。
- **暂不支持直接无损迁移的算法**：依赖硬件深度缓冲（真 3D Z-Buffer SSAO/SSR）或依赖上一帧历史缓冲的时序算法（TAA/运动模糊），因科雷 `PostProcessor` 不暴露深度与历史 RT，仍需针对性适配或等待后续 Tier 2 原生扩展标准。

---

## 2. 一分钟上手流程

### 第 1 步：只写纯数学 `.frag`
新建 `src/my_fx.frag`，只实现 `vec4 hs_main(vec2 uv, vec4 color)`：

```glsl
// Built-ins provided automatically by hs_pack_shader.py:
//   SAMPLER[0] (SRC_IMAGE), SCREEN_PARAMS (w, h, 1/w, 1/h)
//   U0..U3 (aliases to HSEXT_<ID>_U0..U3), HS_CTX_TIME/SUN/ENV/CAM
vec4 hs_main(vec2 uv, vec4 color)
{
    float strength = U0.x;
    vec3 inverted = vec3(1.0) - color.rgb;
    return vec4(mix(color.rgb, inverted, strength), 1.0);
}
```

### 第 2 步：一键打包为科雷 `.ksh` 并做实机 OpenGL 校验
```bash
python tools/hs_pack_shader.py src/my_fx.frag \
    --id acme.my_fx \
    --out shaders/acme_my_fx.ksh \
    --vecs 1 \
    --context TIME \
    --validate-gl
```
- 自动注入 `// @HS_EXT_ID: acme.my_fx`（零配置对接 `dinput8.dll` 硬件 GPU 耗时统计）。
- 自动按科雷 `shaders.zip` 1:1 真二进制格式打包并校验 NUL 结尾与 Uniform 激活。

### 第 3 步：在第三方模组 `modmain.lua` 顶层声明式注册（加载顺序无关！）
```lua
Assets = {
    Asset("SHADER", "shaders/acme_my_fx.ksh"),
}

local SPEC = {
    abi = 1,
    id = "acme.my_fx",
    version = "1.0.0",
    author = "Acme",
    name = { zh = "我的特效", en = "My Effect" },
    stage = "FINAL_FX",
    order = 100,
    exclusive_group = nil,
    default_enabled = false,
    shader = (MODROOT or "") .. "shaders/acme_my_fx.ksh",
    context = { "TIME" },
    params = {
        { key = "strength", slot = "U0.x", type = "float",
          min = 0.0, max = 1.0, step = 0.05, default = 0.50,
          scale = 100, fmt = "%.0f%%",
          label = { zh = "特效强度", en = "Strength" } },
    },
}

-- 无论 HyperSmooth 先加载还是后加载，均可幂等无竞争挂载！
-- 注意：科雷对 GLOBAL 启用了 strict.lua 严格模式，读写自定义全局键必须使用 rawget/rawset！
local hs_q = GLOBAL.rawget(GLOBAL, "HS_EXT_QUEUE")
if type(hs_q) ~= "table" then
    hs_q = {}
    GLOBAL.rawset(GLOBAL, "HS_EXT_QUEUE", hs_q)
end
table.insert(hs_q, SPEC)
local hs_api = GLOBAL.rawget(GLOBAL, "HyperSmooth")
if type(hs_api) == "table" and type(hs_api.RegisterPlugin) == "function" then
    hs_api.RegisterPlugin(SPEC)
end
```
完整可运行示例见 [sdk/example_hs_crt_vintage/](../sdk/example_hs_crt_vintage/modmain.lua)。

---

## 3. `HS_EXT_V1` 规范字段详解

### 3.1 六大标准执行阶段 (`stage`)
按管线从前到后的确定性顺序：

| Stage | 执行位置 | 典型适用算法 |
| :--- | :--- | :--- |
| `PRE_BLOOM` | 原版 `Bloom` 之前 | 线性空间去噪、前级亮度预处理 |
| `BLOOM` | `Bloom` / `HS_Bloom` 之后 | 自定义辉光/泛光（建议配 `exclusive_group = "bloom"`） |
| `POST_BLOOM` | `Bloom` 之后、`ColourCube` 之前 | 辉光后空气雾、镜头光晕 |
| `COLOR_GRADE` | `ColourCube` / `MoonPulseGrading` 之后、`HS_VisualFinal` 之前 | 3D/2D LUT 调色、胶片色调映射、昼夜氛围滤镜 |
| `POST_SHARPEN` | `HS_VisualFinal` 之后 | 第三方锐化（RCAS/Anime4K）、抗锯齿（FXAA） |
| `FINAL_FX` | 最终阶段（HUD/UI 之前） | CRT 扫描线、复古色散、画幅黑边、漫画/水墨风格化 |

同阶段内按 `(order 升序, id 字典序)` 严格确定性排序。

### 3.2 标准共享上下文总线 (`context`)
Core 每帧统一计算一次，仅在至少有 1 个启用插件订阅时上传：

| Key | GLSL Uniform | 分量语义 `(x, y, z, w)` |
| :--- | :--- | :--- |
| `"TIME"` | `uniform vec4 HS_CTX_TIME;` | `(real_time_sec, dt, frame_mod_60/60, world_day_time_01)` |
| `"SUN"` | `uniform vec4 HS_CTX_SUN;` | `(sun_screen_x, sun_screen_y, sun_elev_01, night_factor_01)` |
| `"ENV"` | `uniform vec4 HS_CTX_ENV;` | `(rain_rate, snow_level, is_cave_0_or_1, player_sanity_01)` |
| `"CAM"` | `uniform vec4 HS_CTX_CAM;` | `(cam_zoom_dist, cam_heading_rad, screen_aspect, is_paused)` |

### 3.3 参数表 (`params`, 最多 16 项)
- 自动打包进 `HSEXT_<ID>_U0` .. `HSEXT_<ID>_U3`（最多 4 个 `vec4`）。
- 支持 `type = "float"`（`min < max`、`step`、`default`、`scale`、`fmt`）与 `type = "bool"`（`0.0` / `1.0`）。
- `slot` 可显式指定 `"U0.x"` .. `"U3.w"`，留空则按声明顺序自动紧致分配。
- 参数值自动持久化在 `hypersmooth_presets.json` 的 `"plugins"` 节点中。

### 3.4 多 Pass 离屏子缓冲 (`subpasses`, 最多 4 级) & 额外采样器 (`samplers`, 最多 6 个)
- `subpasses[i]`：`{ name, shader, size_mode="RELATIVE"|"STATIC", w, h, base="BLOOM_SAMPLER"|"SUBPASS"|"TEXTURE"|"SMOKE", source, extra_samplers, use_params }`
- `samplers[i]`：`{ base="BLOOM_SAMPLER"|"SMOKE"|"TEXTURE"|"SUBPASS", name/path, filter="LINEAR"|"POINT", wrap="CLAMP"|"REPEAT" }`
- `SAMPLER[0]` 永远为当前管线主画面，`samplers` 依次绑定到 `SAMPLER[1..N]`。

### 3.5 生命周期钩子与异常熔断
- 可选回调：`on_init(ctx)`、`on_enable(ctx)`、`on_disable(ctx)`、`on_frame(ctx, values)`。
- 所有编译、排序、开关、Uniform 上传与生命周期回调均处于 `pcall` 隔离舱内。**任何单个插件抛错只会熔断隔离该插件并在 Tab 8 标红显示具体原因**，绝不影响游戏本体或其他插件运行。

---

## 4. 控制台指令与公开 Lua API

- 玩家 / 开发者调试命令：
  - `c_hsext()`：打印全部已注册/运行中/已隔离/被拒绝的插件及实时 GPU 耗时
  - `c_hsext("enable"|"disable"|"reset", "<plugin_id>")`
  - `c_hsext("set", "<plugin_id>", "<param_key>", <value>)`
  - `c_hsext("bypass", true|false)`：一键总旁路所有第三方扩展
- 公开 ABI 表 `GLOBAL.HyperSmooth`：
  - `ABI_VERSION` (`1`)、`STAGES`、`RegisterPlugin(spec)`、`GetPlugins()`、`GetPlugin(id)`、`SetPluginEnabled(id, en)`、`SetPluginParam(id, k, v)`、`ResetPlugin(id)`、`SetMasterBypass(b)`、`IsMasterBypass()`

---

## 5. GLSL 跨驱动兼容性检查清单（ANGLE D3D9 + NVIDIA/AMD 原生 OpenGL）
1. 必须以 `#if defined( GL_ES ) precision highp float; #endif` 开头。
2. 禁止使用以 `gl_` 开头的自定义变量名（ANGLE 严格保留前缀）。
3. 禁止使用 `textureSize()`、位运算或非恒定循环上界（GLES2 / D3D9 `ps_3_0` 不支持）。
4. 每个在 `.ksh` 头中声明的 `uniform` 与 `SAMPLER` 都必须被代码静态引用（`hs_pack_shader.py` 自动保活）。
5. 发布前务必使用 `--validate-gl` 跑一次本机 OpenGL 实机编译链接验证。
