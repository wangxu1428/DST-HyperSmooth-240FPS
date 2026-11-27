<p align="right">
  <strong>简体中文</strong> | <a href="README_EN.md">English</a>
</p>

# DST HyperShade — 240FPS & Visual Engine（原 DST HyperSmooth）

> **《饥荒联机版》（Don't Starve Together 64-bit）纯客户端旗舰级超帧性能引擎、独立渲染管线、视觉风格闭环与开放着色器底座**  
> **Client-Only 240Hz Performance Engine, Native OpenGL Rendering Pipeline, Visual Closed-Loop (`HS1-`) & Open `HS_EXT_V1` Platform**  
> 服务端套件（Windows `iphlpapi.dll` 与 Linux x86_64 `libhypercore.so`）请切换分支 👉 [`server-hypercore`](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/tree/server-hypercore)（[Releases 下载](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases)）
>
> *说明：对外品牌现升级为 **`HyperShade · 饥荒超帧与视觉引擎`**（简称 `HS`），内部目录 `dst_smooth_highfps`、已有 `HS1-` 分享码、玩家自存槽 `hypersmooth_slot_1..3.cfg` 与原生接口名均保持 100% 向后兼容。*

---

## 核心定位：四大支柱 × 玩家优先铁律

1. **画面流畅（240Hz Engine & Native GL）**
   - **全场景 240Hz 同相补帧**：主世界/特效/UI 三通道 2D 骨骼、关延迟补偿世界坐标（含自适应 De-Jitter）、全引擎 VFX/旧版粒子、动态光源、动态地面影、跟拍镜头、`FollowText` 气泡与滚动面板。
   - **Native OpenGL ES2 硬件直通 + 32MB 双轨 GPU VBPool + 2D 精灵/UI 多纹理合批**：自动探测硬件 OpenGL 支持并保留 D3D9 兜底，显著压低巨型基地与 Boss 爆发期的 Draw Call 与缓冲重建卡顿。
   - **零争抢 RTSS 级稳帧 (`FrameLimiter::Wait`)** + **Async `libdeflate+Base64` 后台零堵帧存盘队列** + **`FastJson` / `FastDataDumper` / `FastAStar` / `libdeflate` 极速加载与单人本地建房加速** + **负向 Stat 缓存 (`fs_neg_cache.h`)**。
2. **操作低延迟（Ultra-Low Latency & Causal RPC QoS）**
   - **因果安全 3 通道 `RakNet::RPC4` QoS + 实时网络监控**：动态将社交/图鉴广播隔离至 Ch1/Ch2，确保战斗、走位、切装备、小体积 ModRPC 与含实体 ClientRPC 100% 独占 `Ch0 IMMEDIATE_PRIORITY`；500ms 实时采样真实 Ping/丢包率并直显 HUD。
   - **D3D9Ex 硬件 1 帧低延迟队列 + 240Hz 边沿直发与急甩防漏 (`Ctrl+LMB` / WASD)** + **RakNet 零停顿事件唤醒**。
3. **全参数 117 旋钮实时画质光影工坊 · 内置 6 套预设面板 · 事务级 `HS1-` 分享生态（默认 100% 原版纯净）**
   - **内置 6 套官方实调「预设面板」（100% 原创调校 · 零第三方预设）**：在面板顶部或简易模式一键打开「预设面板」（或控制台 `c_hspreset()`），涵盖 **原版纯净 / 护眼高清 / 电影绘本 / 清透日系 / 篝火暖夜 / 林间晨雾**，点选即刻生效、可继续滑块微调、支持一键撤销。**原版纯净模式严格保持科雷原生 `0.25×` BloomSampler 采样，魔光护符/火把/萤火虫亮度与无 Mod 原版 100% 一致，不附加任何视觉效果。**
   - **一楼 · 单 Pass 全自由电影后处理**：BCAS Pro 双边锐化、手绘墨线反卷积、AURA 防白边光晕、高分辨率边缘/地皮微纹理增强、电影调色、ASC CDL（Slope/Offset/Power）、多级辉光金字塔/丁达尔光束、动态胶片颗粒、8-bit TPDF 防色带抖动，以及完全独立可逆的原版滤镜接管（理智色/扭曲/积雪上限/沙尘暴）。
   - **二楼 & 三楼 · 原生 C++ 独立世界空间渲染管线**：
     - **世界空间独立 ShadowMask FBO 流体阴影 + 360° 多光源篝火/提灯动态投影**：最多 3 盏近场火源实时径向投影、雨雪四季衰减、满月冷蓝影、黄昏火影门控、低矮杂物影过滤、闪电/瞬光动态影，以及无重影原版脚底圆影自动隐藏/还原；
     - **沿岸真·平面水面倒影（M3）+ PBR 立体海洋光学 + 体积光雾**：内陆/洞穴 0 开销平面水面倒影、3 层解析法线 PBR 海浪/天空倒影/米氏碎金高光、林间/洞穴及可选全域基地 3D 水中木阳光柱、流动云影/树冠斑驳影与受光地雾/海界迷雾；
     - **真·白名单植被微风与穿行拨草（M4）+ 无震动打击感 & 原生 VFX 氛围粒子**：严格标签白名单防建筑果冻、零镜头晃动卡肉前冲/物理落地碎屑火星、篝火升腾火星、空气光尘孢子与地皮脚步微尘；
     - **崩溃侦探（Crash Detective）+ 双屏防甩出鼠标锁（M1 + M2）**：异常自动输出中英双语 `hypersmooth_crash_report.txt`（含模块+RVA 堆栈与 Lua Mod 归因），可选双屏鼠标锁在暂停/控制台/切屏/崩溃时 100% 自动释放。
   - **严格三阶段事务分享码（`HS1-`）& 可读 JSON 预设库（`hypersmooth_presets.json`）**：支持可选 UTF-8 标题、CRC-16 校验与 Win32 一键复制剪贴板；导入严守 **「只读校验 → 原子应用 → 落盘」**，坏码/截断/越界绝不覆盖现有配置，并支持 **一键撤销导入** 与 3 个玩家自存槽。*（`HS1-` 保证参数配置与开关状态的精确复现；最终画面像素会受显卡驱动、分辨率、游戏场景与渲染后端影响。）*
   - **视觉风格闭环（`Visual Closed-Loop` · 0 新增 DLL，内置于 `dinput8.dll`）**：
     - 玩家复制任意游戏/电影/ReShade 截图（`Win+Shift+S` / `Ctrl+C` / 复制图片文件）后，在分享卡片点「吸取剪贴板截图」或输入 `c_hslook()`，后台线程经 Windows 原生剪贴板 + WIC 解码缩放（Native OpenGL 同步配合 `glBlitFramebuffer + PBO + glFenceSync` 非阻塞采样当前帧），自动区分**通用 Look Matching（跨游戏统计迁移）**与**同场景精确匹配（Robust Huber 逐点优化）**并附带分项置信度；
     - 严格通过**能力白名单（Capability Whitelist）**仅映射到 HyperShade 自有且稳定可控的渲染管线参数，一键输出可复现参数状态、可继续微调、可一键撤销的 `HS1-` 分享码；
     - 不读取、不解析、不复制任何第三方 Mod 的着色器、素材或代码，仅从用户提供的图像提取视觉统计特征生成自有参数预设，从工程上显著降低知识产权风险（但不构成法律结论）。
4. **开放标准：`HS_EXT_V1` 标准 Shader 扩展接口 + 硬件 GPU 实时计费**
   - **兼容标准，而不是兼容每一个模组**：第三方开发者只需编写纯数学 `.frag`（`vec4 hs_main(vec2 uv, vec4 color)`）并用 [tools/hs_pack_shader.py](tools/hs_pack_shader.py) 一键打包校验为 `.ksh`，在 `modmain.lua` 通过 `GLOBAL.HyperSmooth.RegisterPlugin(spec)` 声明式注册，即可自动获得：
     - 6 阶段确定性渲染图排序（`PRE_BLOOM → BLOOM → POST_BLOOM → COLOR_GRADE → POST_SHARPEN → FINAL_FX`）与互斥组仲裁；
     - 共享昼夜/日晷/雨雪/洞穴/理智/相机上下文总线（`HS_CTX_TIME/SUN/ENV/CAM`）；
     - 侧栏 **Tab 8「扩展」自动生成实时滑块**、一键总旁路、单插件异常熔断隔离；
     - **`dinput8.dll` 4 帧异步 `GL_TIME_ELAPSED` 硬件 GPU 耗时计费（ms，0 管线停顿）** 与 `hypersmooth_presets.json` 自动持久化。
   - 详见开发者规范 [docs/HS_EXT_V1_SPEC.md](docs/HS_EXT_V1_SPEC.md) 与官方可加载参考插件模组 [sdk/example_hs_crt_vintage/](sdk/example_hs_crt_vintage/)。

---

## 全自动双语支持 / Auto-Locale Bilingual UI

- 开机自动识别科雷官方语言（`zh` / `zhr` / `zht` 显示简体中文，英语及其他语言自动显示 English），并兜底兼容第三方汉化包。
- 局内支持零重启实时中英互切（侧栏语言切换或控制台 `c_hshud("zh" | "en" | "auto")`），同步覆盖简易/专业双模式、6 套预设面板、117 项旋钮、分享与视觉闭环卡片、Tab 8 扩展插件、`设置 -> 控制` 热键页与右上角 HUD。

---

## 玩家安装与更新指南 / Player Installation

### 第一步：安装 / 更新客户端 Mod
- **Steam 创意工坊**：订阅并启用 **HyperShade · 饥荒超帧与视觉引擎（原 HyperSmooth）**（或将压缩包内 `mods/dst_smooth_highfps` 复制至游戏 `mods/dst_smooth_highfps`）。

### 第二步：安装 / 更新 `bin64/dinput8.dll`（工坊不会自动更新 DLL，大版本升级请务必同步；亦可通过 `DNH` 伴侣自动拉取）
- **方式 A（推荐 · 一键安装/更新）**：
  1. 下载并解压最新版 `DST_HyperShade_v2.9.7-beta1_Release.zip`；
  2. 先关闭饥荒游戏，双击根目录的 **`一键安装与更新.bat`**，按提示回车即自动完成路径识别、旧版备份、新版覆盖与 SHA-256 校验（选 `2` 可一键彻底卸载恢复纯净原版）。
- **方式 B（手动安装）**：
  1. 将 `bin64/dinput8.dll` 复制到 `<Don't Starve Together>/bin64/`（与 `dontstarve_steam_x64.exe` 同目录）；
  2. 进图按面板快捷键（或输入 `c_hsmenu()`），看到顶部绿条 `● 原生引擎已连接 v2.9.7（3D 光影可用）` 即全部就绪。

> **兼容提示**：
> - **`DontStarveLuaJIT2` (工坊 `3444078585`)**：自动识别并避让 `mimalloc lua_newstate`，零冲突开箱即用。
> - **独立版 `DST-BCAS-Studio`**：自动检测 `BCAS_State` 并优雅让位后处理与原生光影，互不覆盖。
> - **驱动级安全护盾**：内置 `glShaderSource` 空指针拦截，防止畸形第三方 `.ksh` 触发 OpenGL 驱动闪退。

---

## 局内常用指令 / In-Game Console Commands

- `c_hsmenu()`：打开/关闭实时画质光影工坊侧栏（简易模式 / 专业模式 / 预设面板，亦可在 `设置 -> 控制` 底部录制任意快捷键或鼠标侧键）
- `c_hspreset()` / `c_hspreset(1..6 | "id")`：查看或一键切换 6 套官方实调预设（`vanilla` / `eyecare_hd` / `storybook` / `crisp_daylight` / `cozy_firelight` / `misty_canopy`）
- `c_hsshare("方案名")` / `c_hsshare("HS1-...")`：导出并复制带标题的分享码 / 事务校验并安全导入分享码
- `c_hslook()` / `c_hslook("general"|"exact"|"frame"|"caps"|"undo")`：视觉风格闭环（从剪贴板截图/图片或同帧画面拟合自有管线参数生成 `HS1-` 分享码 / 查看能力白名单 / 一键撤销）
- `c_hsjson("save"|"load"|"reload"|"list", "名称")`：管理 `hypersmooth_presets.json` 人类可读批量方案库
- `c_hsext()` / `c_hsext("bypass", true|false)`：查看 `HS_EXT_V1` 第三方扩展插件状态、GPU 耗时与一键总旁路
- `c_hsclip()` / `c_hsvisual()` / `c_hslatency()` / `c_hsopt("off")` / `c_hshud("simple"|"detail"|"zh"|"en"|"auto")`

---

## 安全性与开源构建 / Safety & Build

- **零磁盘篡改与严格隔离**：完整无锁透传 `System32\dinput8.dll` 全部 6 个导出函数；严格按进程名隔离，仅在 `dontstarve_steam(_x64).exe` / `dontstarve_rail(_x64).exe` 生效；全部跳板执行 W^X 只读固化 (`PAGE_EXECUTE_READ`)，61/61 AOB 签名具备 Fail-Closed 熔断保护。
- **纯开源构建与验证**：
  - 原生 DLL：`tools/build_native.ps1`（Zig `x86_64-windows-gnu`, `-std=c++20 -O3`，纯 UCRT 零外部运行库依赖）+ `python tools/test_redteam_client.py` + `tools/test_redteam_client.exe`（41/41）+ `tools/test_world_shadow_gpu.exe`（13/13 Native OpenGL + ANGLE D3D9 双后端实机 GPU 校验）+ `tools/test_gpu_clarity.exe`（4/4 GPU Compute 清晰度验证）
  - 着色器包：`python tools/build_shaders.py`（74/74 原版与模组 `.ksh` 二进制逐字节校验 + 实机 OpenGL 编译验证）
  - Lua 回归：`python tools/test_client_lua_v24.py`（真实 64 位 DST Lua 5.1 虚拟机 35/35 全链路自动化测试，含 3,141 控件压力乱点 0 报错）

---

## 技术溯源、独立演进与开源授权 / Provenance & Licensing

本项目公开留存完整的**技术来源文献映射**、**Git 独立演进记录与实机验证套件**，以及**第三方代码/依赖库/素材授权台账**：

- **技术来源与独立开发演进白皮书**：[`docs/PROVENANCE_AND_ARCHITECTURE.md`](docs/PROVENANCE_AND_ARCHITECTURE.md)（[English Version](docs/PROVENANCE_AND_ARCHITECTURE_EN.md)）
  - 完整列出原生世界空间正交 ShadowMask FBO（SIGGRAPH 1978/2005 PCSS + SPIE 1993 蓝噪声）、平面水面倒影、Dual-Kawase/Karis 泛光、ACES/CDL/TPDF、AZDO 合批环、PCHIP 流体骨骼等每一模块对应的公开论文、教材、规范及架构演进决策（ADR），且 v2.9.x 原生投影/倒影零 Lua 实体克隆。
- **v4.0 架构演进路线图与跨界技术参考卷**：
  - [`docs/V4_0_ARCHITECTURE_ROADMAP.md`](docs/V4_0_ARCHITECTURE_ROADMAP.md) —— 基于真机实测校准的 v4.0 分阶段演进蓝图（Phase 1 同上下文持久 PBO 上传环与双线程卡顿采样、Phase 2 驱动隐式同步消除与 SIMD 合批、Phase 3+ 保留态场景与次世代光影规划）；
  - [`docs/research/REF_VOL1_FACTORIO_FFF.md`](docs/research/REF_VOL1_FACTORIO_FFF.md) —— 技术参考卷 Vol.1：《异星工厂》（Factorio FFF）百万实体与 2D 渲染底层架构映射；
  - [`docs/research/REF_VOL2_4_SODIUM_FOLIA_POE2.md`](docs/research/REF_VOL2_4_SODIUM_FOLIA_POE2.md) —— 技术参考卷 Vol.2–Vol.4：Sodium / Iris / Folia / Minestom / 《流放之路 2》洁净室原理研究与 DST 全栈映射。
- **第三方依赖、开源适配与素材授权台账**：[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) & [`LICENSE`](LICENSE)
  - 静态链接库：`libdeflate v1.23`（MIT）、`mimalloc v2.1.7`（MIT）；
  - MIT 开源项目致谢与引用边界：`DST-BCAS-Studio`（Copyright 2026 Nanmianyi / 楠眠已，MIT License，含独立版自动共存礼让）、`DST-KeyBind-UI`（rtk0c / liolok，MIT License）；
  - 仓库分发的全部 `.ksh` / `.tex` / `.xml` / 蓝噪声表均由仓内源码脚本 100% 确定性生成，不打包分发任何科雷官方受版权保护资源。
