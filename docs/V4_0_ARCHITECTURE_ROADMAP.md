# DST HyperSmooth / HyperShade v4.0 架构演进路线图与实证工程蓝图

- **维护团队**：DST HyperSmooth Architecture Team
- **基线版本**：DST HyperSmooth / HyperShade v3.9（Native WGL OpenGL 4.6 + `ARB_bindless_texture` + SSBO + Compute 4.3 + 12T Skeletal Pool + Elastic Pool）
- **配套文献**：
  - 开源溯源与前期架构决策（ADR）：[`PROVENANCE_AND_ARCHITECTURE.md`](PROVENANCE_AND_ARCHITECTURE.md)
  - 跨界参考卷 Vol.1（Factorio FFF）：[`research/REF_VOL1_FACTORIO_FFF.md`](research/REF_VOL1_FACTORIO_FFF.md)
  - 跨界参考卷 Vol.2–Vol.4（Sodium / Iris / Folia / Minestom / PoE2）：[`research/REF_VOL2_4_SODIUM_FOLIA_POE2.md`](research/REF_VOL2_4_SODIUM_FOLIA_POE2.md)
- **证据原则**：本文档严格区分 **【RTX 4060 真机实证已落地/进行中】** 与 **【远期研究目标/规划方案】**。一切性能预算须以同存档、同镜头、同画质 A/B 真机测试为准，不将未验证的理论模型写成实测成绩。

---

## 一、演进分期总览（实证驱动，先诊断后接管）

v4.0 并非一次性激进重写，而是按照 **“先用无锁堆栈探针抓出真实驱动/CPU 瓶颈 → 零架构风险逐项清零停顿 → 再审慎推进保留态场景与异步呈现”** 的实证闭环分阶段推进：

| 演进阶段 | 状态 | 核心交付与工程突破 | 关键验证依据 |
| :--- | :--- | :--- | :--- |
| **Phase 1** | **已交付（Shipped）** | 同上下文 3 帧持久一致 `GL_PIXEL_UNPACK_BUFFER` 纹理上传环（[`native_plugin/pbo_tex_ring.h`](../native_plugin/pbo_tex_ring.h)）+ 无锁双线程（MainThread + RenderThread）卡顿栈采样器 | RTX 4060 真机实测：GPU 满载下 2048×2048 DXT5（12 Mip）上传从 **6.13 ms 均值 / 9.23 ms 峰值 → 0.35 ms 均值 / 0.43 ms 峰值**（峰值降幅 21×），0 跨上下文 Bindless 同步风险 |
| **Phase 2** | **进行中（In Progress）** | 消除 Klei 隐式 `glGetError()` 全管线排空（`HookGlGetError`）+ `PreallocTexIdPool`（256 ID 池）+ 1/64 X-Ray QPC 降频 + 每绘制状态提升（消除 `__tls_init`）+ `SpriteBatcher` SSE2 向量化与 `kMaxBatchVerts` `32768 → 65520` + 消除 `GpuClarity`/`WorldShadow` 同步驱动查询 + Native GL 异步 GPU 计时器 | RTX 4060 实机取证与二进制反汇编实锤根因；双后端 GPU 测试套件 + 单元红队回归门禁验证 |
| **Phase 3+** | **远期规划（Planned）** | **Track A**：Sodium 0.6 `GFNI` 启发静态-动态归并 + `GlBufferArena` 保留态镜像；**Track B**：Factorio FFF #421 高频解耦呈现 + 动态物理视锥保护带 + `FBO_UI`；**Track C**：FFF #333 环形卷轴增量阴影 + PoE2 Radiance Cascades `O(1)` 多点光源阴影 | 须经旁路影子校验、像素差分回归、人工注入卡顿与无损 Fail-Closed 回退门禁分阶段验收 |

---

## 二、真机实证诊断与数据口径校准

### 2.1 日志统计口径校准（避免跨帧误判与重复开发）

在早期 v3.9 大基地日志分析中，必须严格避免以下四类常见统计归因误区：

1. **禁止混淆卡顿瞬时快照与稳态窗口均值**：例如 1,701 实体 / 24,453 骨骼属于尖峰卡顿帧快照（`busy=33.5 ms`、`anim=3.67 ms`），而 `busy=9.46 ms`、`rt=6.04 ms`、`sb=0.97/0.14 ms` 属于低密度稳态窗口（`ents=294`、`bones=4,949`），两者不能拼接为同一帧成绩。
2. **未归因残差不等于已证实的命令解释器税**：`rt` 减去已知合批与光影耗时得到的约 `4.73 ms` 只是未归因残差，不能凭猜测归咎于虚函数开销——这正是 Phase 1 优先落地双线程卡顿栈采样器的直接动因。
3. **Stencil 延迟提交已在底层就位，避免重复实现**：OpenGL ES 2.0 / 4.6 规范 §4.1.5 的精确无副作用模板延迟判定（`CanDeferStencilMaskWhenEnabled`、`CanDeferStencilOpWhenEnabled`、`CanDeferStencilFuncWhenEnabled`）**已在 [`native_plugin/gl_state_cache.h`](../native_plugin/gl_state_cache.h#L505-L522) 实现**并在模板 Hook 中接入。实机日志中剩余的模板打断（`GL_ALWAYS + GL_REPLACE` 写入遮罩、`GL_GEQUAL` 裁剪比较）属于**真实硬件渲染依赖屏障（Render Dependency Segments）**，严禁将其当作无效切换强行跳过。
4. **区分研究目标与已验证指标**：`200–240 FPS`、`1% Low > 180 FPS` 与亚 2 ms 渲染管线预算属于 v4.0 远期研究目标，须按阶段实机 A/B 验证；近期阶段优先稳步消除真机实锤的毫秒级停顿与高密度大基地掉帧。

### 2.2 RTX 4060 实机取证发现的五大真实瓶颈（Live Forensics）

通过 Phase 1 无锁双线程卡顿栈采样器、RTX 4060 独立 OpenGL 基准以及游戏 64 位二进制反汇编，我们在 1,500+ 实体 / 约 835K 顶点/帧的大基地实机运行中捕获了五处此前被掩盖的真实停顿根源：

1. **Smoking Gun #1 —— Klei 热路径隐式 `glGetError()` 强制排空 NVIDIA GPU 管线**：
   - Phase 1 的持久 PBO 环已经把 `glCompressedTexImage2D` 本身从 8.99 ms 压降至 0.02–0.50 ms，但大基地跑图时仍偶发 `tex=26.25 ms`（单张贴图 7.85–9.68 ms）卡顿。
   - 反汇编确认 Klei 的 `HWTexture::DeserializeTexture` 与 `HWBuffer::Init` 在完成上传后**紧接着同步调用 `glGetError()`**。
   - RTX 4060 实测证明：在重负载帧的 PBO 上传之后调用 `glGetError()` 会触发 NVIDIA 驱动全管线同步排空（单次独立压测在 97.4 ms GPU 帧中同步阻塞高达 **88.6 ms**；实机大基地中每张流送贴图拖出 **7.85–9.68 ms** 停顿）。
2. **Smoking Gun #2 —— `glGenTextures(1)` 引发 NVIDIA 多线程驱动同步往返停顿**：
   - RTX 4060 实测证明，在重负载帧中调用 `glGenTextures(1, &tex)` 会在 NVIDIA 多线程 OpenGL 驱动内部产生 **1.9–3.5 ms** 的同步往返停顿（等待驱动线程返回句柄 ID），而从预分配池弹出已生成 ID 仅需 **0.000 ms**。
3. **Smoking Gun #3 —— 100% 帧率高频 QPC 遥测与 50,000 次/帧 `__tls_init` 开销**：
   - 卡顿栈直接命中 `RtlQueryPerformanceCounter`、`__tls_init` 与 `FoliageTable::QueryClass`：渲染层 X-Ray 计时在 100% 帧上触发约 55,000 次 QPC 调用；`TryHandleSpriteBatchDraw` 在每帧 12,000+ 次精灵绘制中重复读取阴影/水面/风场配置与 `thread_local` 守卫，并无条件执行植被网格查询。
4. **Smoking Gun #4 —— 标量 20B 顶点拷贝与冗余 `std::sqrt` / 批次满溢分裂**：
   - 卡顿栈捕获 `SpriteBatcher::TryBatchGpuDrivenInternal` 每帧以标量方式拷贝 835K+ 个 20 字节 `GpuBatchVertex`，且在 `hasCamRight_` 已知时仍逐绘制执行 `std::sqrt`；同时 `kMaxBatchVerts = 32768` 在大基地每帧触发约 13 次 `FlushReason::Full` 强制切批。
5. **Smoking Gun #5 —— 后处理/阴影同步驱动查询与 Native GL GPU 计时盲区**：
   - 渲染线程卡顿栈捕获 `GpuClarityEngine::ProcessFrameWithMode` 与世界渲染阶段进入 NVIDIA 驱动等待（如 `glGetIntegerv` / `glIsEnabled` 同步查询往返）；同时 Native OpenGL 模式下此前未采集异步 GPU 耗时（`gpu=0.00ms` 盲区），掩盖了真实 GPU 压力（例如 Grade 3 虚拟 4K 清晰度管线与 4096×4096 ShadowMask 的显存与算力开销）。

---

## 三、Phase 1（已交付 Shipped）：同上下文持久 PBO 上传环 + 双线程卡顿栈采样器

### 3.1 架构实证校准：为何放弃早期“共享上下文上传”草案，改采同上下文 3 帧持久 PBO 环？

早期架构草案曾设想创建独立后台上传线程与共享上下文 `HGLRC_Upload`，上传完成后原子替换 `ARB_bindless_texture` 句柄。但经 RTX 4060 实机压测与驱动行为评估，跨上下文共享存在三重隐患：
- NVIDIA 驱动在跨共享上下文操作纹理命名空间与驻留态时仍可能发生内部隐式串行化；
- Bindless 句柄的跨上下文 `glFenceSync` + 生产者 `glFlush` + 旧句柄在途引用回收极易在上下文重建或切洞穴时引发竞态与黑贴图；
- 宿主原版上传函数未证明线程可重入，强行移入后台线程会放大第三方 Mod 兼容风险。

因此 Phase 1 在 [`native_plugin/pbo_tex_ring.h`](../native_plugin/pbo_tex_ring.h) 落地了更简洁、经实机验证的 **同上下文 3 帧在途持久一致 `GL_PIXEL_UNPACK_BUFFER` 环（`hs_pbo_tex::PersistentPboTexRing`）**：
- **显存布局**：通过 `glBufferStorage` 分配 `GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT` 单块 3 槽环形暂存缓冲（默认 3×16 MB = 48 MB，低显存档自动收缩至 3×8 MB / 3×4 MB，256 字节对齐，跳过 `<16 KB` 小 UI 字形避免负优化）。
- **零帧内停顿同步**：每帧仅在 `SwapBuffers` 边界插入 1 个帧级 `glFenceSync`，3 帧后回收对应环槽；CPU `memcpy` 写入持久映射区后直接向驱动发出异步 DMA 纹理拷贝命令，零 `glMapBuffer` 隐式阻塞。
- **Fail-Closed 门禁**：若驱动不支持 `ARB_buffer_storage`、显存不足、环槽溢出或 Fence 超时，无缝返回 `false` 退回原版直接上传路径，不丢贴图、不崩溃。
- **RTX 4060 真机实测成绩**：GPU 繁忙态下 2048×2048 DXT5（12 Mipmap）纹理上传耗时由 **6.13 ms 均值 / 9.23 ms 峰值** 降至 **0.35 ms 均值 / 0.43 ms 峰值**（均值提速 **17.5×**，峰值压低 **21×**）。

### 3.2 无锁双线程（MainThread + RenderThread）卡顿栈采样器

- 以独立低开销监视机制同时覆盖主线程与渲染线程，在超过卡顿阈值时无锁记录模块+RVA 堆栈样本并落入诊断日志，不干扰正常帧率。
- 直接揭示了 Phase 2 的全部五大实锤瓶颈（`glGetError` 排空、`glGenTextures(1)` 同步、`RtlQueryPerformanceCounter`、`__tls_init`、`FoliageTable::QueryClass`、`TryBatchGpuDrivenInternal` 标量拷贝及 `GpuClarityEngine` 驱动等待）。

---

## 四、Phase 2（进行中 In Progress）：驱动同步清零、合批器 SIMD 化与 GPU 停顿治理

Phase 2 严格针对 Phase 1 采样器与真机基准实锤的五大瓶颈逐一攻坚，保持 100% Fail-Closed 与 ANGLE D3D9 兜底不变：

### 4.1 消除热路径 `glGetError()` 全管线排空（`HookGlGetError`）

- 通过 `libGLESv2.dll` IAT Hook 接管游戏侧 `glGetError()`（插件内部诊断与自检继续直接调用原始 `g_fnGlGetError()`，严禁屏蔽自身错检）。
- **安全门控**：仅在 Native OpenGL 激活且处于局内游戏循环或异步纹理上传任务窗口（`tl_inAsyncTexJob || g_inGameplay`）时，以约 1 ns 直接返回 `GL_NO_ERROR`（`0`）并累加跳过遥测计数；初始化阶段、上下文创建期及 ANGLE 模式保持 100% 原始透传。
- **预期收益**：彻底斩断 `HWTexture::DeserializeTexture` 与 `HWBuffer::Init` 上传后的 NVIDIA 全管线排空，释放 Phase 1 PBO 环的完整异步红利（消除 7.85–9.68 ms/张的纹理流送掉帧）。

### 4.2 预分配纹理 ID 池（`hs_pbo_tex::PreallocTexIdPool`）

- 位于 [`native_plugin/pbo_tex_ring.h`](../native_plugin/pbo_tex_ring.h)（`kPoolCap = 256`、`kDefaultLowWater = 64`，固定栈数组零堆分配）。
- **`TryAcquire(n, outIds)`**：在 `HookGlGenTextures` 热路径中以 `O(n)` 弹出预分配纹理 ID，**0 次 GL 驱动调用、0.000 ms 停顿**；池余量不足时原子返回 `false` 无缝回退至直接 `glGenTextures`。
- **`RefillIfLow()`**：安排在 `SwapBuffers` 之后 GPU 自然排空窗口执行单次批量 `glGenTextures(kPoolCap - count_, ...)` 补水，避开关键渲染路径。
- **零泄漏生命周期**：`ReleaseGpu(deleteOnCurrentCtx)` 与 `ResetContext(newCtx, deleteOnCurrentCtx)` 在上下文切换/销毁时单次 `glDeleteTextures` 归还未使用 ID，且在同上下文 `MakeCurrent` 失败分支妥善保留存活 ID。

### 4.3 CPU 热路径减负：1/64 X-Ray 采样与每绘制状态提升

- **1/64 降频 X-Ray QPC 遥测**：在 `HookGameRender` 帧入口按 `(++counter & 63) == 0` 门控 `IsRtWorldXrayActive()`，仅在采样帧执行细粒度计时与 EMA 更新，直接削减 98.4% 的热路径 QPC 系统调用。
- **每绘制状态提升（Hoisting）与 `__tls_init` 消除**：
  - 将阴影、水面反射、植被风场配置及开关标志提升至每帧 / 切 Program 时刷新一次的 POD `thread_local` 帧缓存，避免在 12,000 次/帧的 `TryHandleSpriteBatchDraw` 内重复拉取配置与触发 `__tls_init` 静态初始化守卫。
  - 为 `FoliageTable` 增加内联 `IsEmpty()` 零开销快速拒绝，并用无 `libm` 的快速浮点向下取整优化 `QueryClass(wx, wz)`。

### 4.4 `SpriteBatcher` SSE2 向量化与批次上限调优

- **SSE2 向量化 20B 顶点拷贝**（[`native_plugin/sprite_batcher.h`](../native_plugin/sprite_batcher.h)）：
  - 在 `TryBatchGpuDrivenInternal` 的 `!trackMaxWy` 与 `trackMaxWy` 双路径中采用 `__restrict` 与 4× 展开的 `_mm_loadu_ps` / `_mm_storeu_ps`（`stride == 16` 快路径 + 尾部标量兜底），并通过将累加器放在 `_mm_max_ps(wyv, vMaxWy)` 第二操作数保证与标量 `if (wy > maxWy)` 逐位一致的 NaN 语义；索引拷贝同步加注 `__restrict` 触发 SSE2 `paddw` 向量化。
- **跳过冗余 `std::sqrt`**：当相机右向量 `hasCamRight_` 已就绪时直接跳过每绘制一次的向量长度开方归一化。
- **安全扩大合批容量**：将 `kMaxBatchVerts` 从 `32768` 调优至 **`65520`**（最大发射 `uint16_t` 索引 `65519 < 0xFFFF`，避开 Primitive Restart 保留值，且为 16/4/3 公倍数），`kMaxBatchIndices` 从 `65536` 扩至 **`131072`**（保留 `kMaxGpuBatchInsts = 8192` 以守住紧凑显存档 SSBO 下限），将大基地 `FlushReason::Full` 切批频率削减约一半。

### 4.5 消除 `GpuClarity` / `WorldShadow` 同步驱动查询与 Native GL 异步 GPU 计时

- **零同步查询快路径**：审计并消除 `GpuClarityEngine::ProcessFrameWithMode` 与 `WorldShadowEngine` 每帧热路径中的 `glGetIntegerv` / `glIsEnabled` / 状态轮询，提供可选的已缓存 GL 状态传入接口（默认 `nullptr` 保持独立调用方与测试 100% 兼容），并结合紧凑 AABB 裁剪剔除视域外延迟阴影批次。
- **Native GL 4 帧异步 GPU 计时环**：在 Native OpenGL 下建立零停顿异步 GPU 耗时采集（严格避开与 `HS_EXT_V1` 扩展计费及 `GpuClarity` 内部 `GL_TIME_ELAPSED` 的嵌套冲突，确保零 `GL_INVALID_OPERATION`），彻底消除 `gpu=0.00ms` 遥测盲区。

### 4.6 Stencil 边界确认（避免重复实现）

- OpenGL ES 2.0 / 4.6 规范 §4.1.5 模板延迟判定（`CanDeferStencilMaskWhenEnabled` / `CanDeferStencilOpWhenEnabled` / `CanDeferStencilFuncWhenEnabled`）**已在 [`native_plugin/gl_state_cache.h`](../native_plugin/gl_state_cache.h#L505-L522) 就位**；
- 剩余 `GL_ALWAYS + GL_REPLACE` 与 `GL_GEQUAL` 切换属于真实的硬件遮罩/裁剪屏障（Render Dependency Segments），Phase 2 严禁将其误判为冗余切换，留待 Phase 3 Track A 按区段语义安全优化。

---

## 五、Phase 3 及远期架构演进路线（Planned Roadmap）

在 Phase 1–2 清零显性驱动同步与 CPU 热路径税之后，面向 3,000–5,000+ 实体极限大基地与高刷新率目标，v4.0 中远期规划遵循严格的工程边界逐步演进。

### 5.1 明确否决的错误路线（架构避坑记录）

以下两条直觉方案经交叉论证在 DST 2.5D 语义下存在结构性缺陷，明确予以否决：

- **否决路线 A：静态建筑区块整图烘焙 + 软件化 Stencil**
  - **穿模死结**：DST 严格遵循从后往前的 2.5D 画家顺序。玩家、坐骑与怪物频繁穿插于静态箱子/墙体前后，区块静态烘焙必然导致角色穿模或被迫高频切碎退化。
  - **模板竞态**：Klei 依赖硬件 Stencil（`GL_ALWAYS + GL_REPLACE`/`GL_INCR` 配合 `GL_GEQUAL`/`GL_EQUAL`）实现动态遮挡剪影与水面裁剪；无像素锁的软件 Stencil 在重叠密集区必然产生读写竞态花屏。
- **否决路线 B：粗粒度 GL 命令流直接重放**
  - **动态穿插失效（Z-Popping）**：主线程卡顿期间死抱旧命令流重放会导致运动实体深度序错乱。
  - **视锥边缘黑边（Frustum Vacuum）**：旧命令流已被原版 CPU 视锥裁边，快速平移镜头时四边暴露空缺。
  - **未消除 `O(N)` 命令构建成本**：5,000 实体规模下单纯重放无法突破主线程命令提交上限。

### 5.2 Track A：保留态场景镜像与动静双流线性归并（HyperRender-Retained × Sodium 0.6 GFNI）

参考 [`Vol.2` Sodium 架构分析](research/REF_VOL2_4_SODIUM_FOLIA_POE2.md)（卷二 §2.1–§2.3）：

1. **持久化静态世界镜像（`GlBufferArena` + 增量事件）**：
   - 以 `EntityID + Generation` 唯一标识实体（杜绝指针复用与 ABA 问题），仅在 `Create / TransformDirty / AnimationDirty / MaterialDirty / VisibilityDirty / Destroy` 时发布增量；丢失增量自动触发安全全量重同步。
   - 将大基地静态建筑不可变的局部几何 `(x, y, u, v)` 常驻显卡本地 `Device-Local Arena`，经 `Persistent Staging Ring -> glCopyBufferSubData` 异步更新，并与 64B 对齐的动态实例 SSBO 解耦；对象、纹理与顶点槽位受 `Generation + Epoch + glFenceSync` 联合托管，GPU 消费完成前绝不提前回收。
   - **收益前提**：须先验证能否将变更捕获入口上移至实体生命周期/变换/AnimState 层（旁路影子对照原版提交）；若只能在每帧 `AnimRenderCmd` 做差异对比，则仅降低上传带宽而不能宣称消除了 CPU 遍历。
2. **Render Dependency Graph + 分段动静归并（Segmented Static-Dynamic Merge）**：
   - 以真实硬件屏障（FBO 切换、真实 Stencil 读写/比较、非交换混合、Scissor/Clear）为界，将帧拆分为合法的 **Render Dependency Segments**；严禁将不同区段的普通实体跨 Stencil 屏障全局抽离重排。
   - 在每个合法区段内，利用 DST 2.5D 相机不变量（参考 Sodium 0.6 `GFNI` 分隔平面定理：除非玩家按 Q/E 旋转相机 Yaw，否则静止平行立牌间的相对投影深度序帧间 100% 不变）：缓存静态实体的有序序列（Sorted Run），每帧仅对实际位移的少量动态部件重算深度并排序，再与静态有序流执行 `O(N_static + N_dynamic)` 线性归并。
   - **排序键位宽红线**：严禁将 32-bit 深度与 `OriginalOrder` 位域重叠拼接（例如错误的 `(Layer << 48) | FloatToSortableUint32(depth) | OriginalOrder` 会破坏排序正确性）；必须使用互不重叠的多字段键或稳定分段排序，确保同深度 `OriginalOrder` 严格不漂移。

### 5.3 Track B：低频权威快照 + 高频解耦呈现与动态视锥保护带（Factorio FFF #421 启发）

参考 [`Vol.1` Factorio FFF #421 幽灵预测渲染与视锥保护带](research/REF_VOL1_FACTORIO_FFF.md)（§2.4）：

1. **不可变三缓冲视觉快照与时间契约**：
   - 独立 `PresentThread` 仅消费已发布的不可变视觉快照（除 64B 实体记录外，完整覆盖 Pass、Program、材质、FBO、纹理代际、状态与区段依赖）；CPU 槽位交接严格使用 acquire/release 内存序，GPU 在途读取严格绑定 Fence/Barrier。
   - **动态物理视锥保护带**：放弃写死的固定倍率（如 `1.25×`），改按镜头最大位移速率、旋转/缩放、最大快照年龄与实体包围半径动态计算保护边界 `guard = v_max * max_snapshot_age + entity_radius`；超出保护边界时自动钳位停止外推，防止边缘闪烁与黑边。
   - **诚实的性能语义边界**：模拟/Lua/网络卡顿期间，独立呈现线程可维持高频平滑镜头与有限阻尼外推（对传送、出生、销毁严禁外推，超时自动冻结降级并平滑纠偏），但**绝不能凭空生成尚未发生的权威逻辑状态**。验收指标必须同时区分 **呈现 FPS、有效新状态频率、快照年龄与输入到画面延迟**。
2. **独立 `FBO_UI` 与全覆盖脏标记**：
   - 将 UI 合批重定向至独立 `FBO_UI` 叠加呈现，脏标记必须覆盖动画、计时器、文本刷新、键鼠输入与第三方 Mod 回调，避免 UI 停滞或文字果冻。
3. **可选远期探索 —— GPU Animation Evaluation**：
   - 在完全验证符号替换、换装覆盖、负缩放、循环与跨动作融合等 DST 特有 2D 骨骼语义的前提下，可探索将纯视觉部件求值下沉至 Compute Shader，同时永久保留 `SkeletalMtPool` 作为正确性差分参照与兼容后端。

### 5.4 Track C：次世代 2.5D 光影演进（FFF #333 环形阴影 + PoE2 Radiance Cascades）

参考 [`Vol.1` §2.5](research/REF_VOL1_FACTORIO_FFF.md) 与 [`Vol.4` PoE2 辐射级联](research/REF_VOL2_4_SODIUM_FOLIA_POE2.md)（卷四 §4.1–§4.3）：

1. **`WorldShadow V2` 环形卷轴增量更新（Toroidal Scrolling Buffer）**：
   - 针对现有 4096×4096 正交 ShadowMask FBO 随镜头平移全量重绘数十批次的 GPU 带宽开销，引入 FFF #333 环形回绕坐标更新：平移镜头时复用已有静态阴影，仅增量光栅化新露出的 L 型边缘窄带。
2. **世界空间 2D 辐射级联（Radiance Cascades + JFA 2D SDF）**：
   - 突破现行 `kMaxPointShadowLights = 3` 的逐光源几何绘制上限：将视野内全部发光体与遮挡锚点写入单一世界空间辐射/遮挡网格，通过 9 趟 JFA Compute 生成 2D SDF 并运行 5–6 层多级级联求值，实现关于光源数量的 **`O(1)` 复杂度、零蒙特卡洛噪点、零 TAA 拖影** 的百灯软阴影与间接漫反射（仅在 Compute 4.3 独显档开启，核显/ANGLE 自动回退现有 3 光源路径）。
3. **零显存立体代理法线（Proxy Volumetric Normals）**：
   - 在 `HD2D` 着色阶段利用现有部件偏移与世界高度实时合成半圆柱/椭球代理法线，0 MB 额外显存且 100% 兼容工坊皮肤。

### 5.5 Track D：深度诊断闭环与 HyperCore 专服协同

- **全链路主线程诊断**：结合 Windows ETW/WPR/WPA 采样对齐插件帧 ID 与 QPC，定位剩余未归因主线程长冻结；拆解 RakNet `RM3:0x87` 同步包各子阶段耗时；在 Lua 桥接层推进制作栏/重型模组轮询的 **Epoch 脏纪元缓存**；延续 Factorio “99% 存档扫表定律”推进全量 C++ `FastDataDumper` 覆盖。
- **HyperCore 专服演进红线**：严守单 `lua_State` 串行不变量，严禁跨线程并发读写 Lua VM；仅将 C++ `FastAStar` / `SpatialGrid` 空间孤岛并行（Folia 启发）、`ReplicaManager3` 公共位流单 Tick 缓存（Minestom `CachedPacket` 启发）与多 Shard 进程分片作为高确定性多核扩展路径。

---

## 六、工程红线、验证矩阵与 Fail-Closed 回退门禁

### 6.1 不可逾越的工程红线

1. **WGL 上下文单所有者红线**：未来移交主 `HGLRC` 至 `PresentThread` 时，必须先由原线程执行 `wglMakeCurrent(nullptr, nullptr)` 完整释放所有权后再接管，严禁双线程同时持有同一上下文。
2. **Steam Overlay 单窗口兼容红线**：`SwapBuffers` 必须始终保持在原游戏窗口 `HWND` 上调用，防止 Steam Overlay 钩子死锁或黑屏。
3. **纯客户端兼容与零破坏红线**：严禁替换原版 Lua 5.1 VM、严禁改写磁盘原版 EXE、严禁触碰或破坏玩家存档目录，始终保留完整 ANGLE D3D9 兜底路径。
4. **真实安全回退红线**：运行时关闭或异常熔断不能仅翻转布尔开关，必须按序停止快照发布、排空在途任务与 GPU 引用、释放/归还上下文资源并恢复原始调用链；若无法证明热回退 100% 安全，则仅允许安全重启恢复。

### 6.2 标准化 A/B 实机验收矩阵

所有 Phase 2 与 Phase 3+ 变更均须在 **同存档、同镜头轨迹、同画质预设、同模组清单与同驱动版本** 下执行 A/B 对照验证：

| 验证维度 | 覆盖内容与强制指标 |
| :--- | :--- |
| **自动化单元与双后端 GPU 套件** | `tools/test_redteam_client.exe`（CPU/WIC/边界红队）、`tools/test_world_shadow_gpu.exe`（Native OpenGL 4.6 + ANGLE D3D9 像素回读与 PBO/TexPool 真机校验）、`tools/test_redteam_client.py`（PE 导出与 58/58 AOB 唯一签名校验）、`tools/test_client_lua_v24.py`（真实 64 位 DST Lua 5.1 VM 回归） |
| **分级实体规模实机压测** | 1,500 / 1,700 / 5,000 实体场景（同步记录实际可见部件数与骨骼数）；采集平均 FPS、1% Low、P95/P99 帧时间、CPU/GPU 耗时、资源队列积压与 VRAM 峰值 |
| **鲁棒性与边界回归** | 人工注入 30 / 100 / 160 ms 主线程停顿、地面↔洞穴频繁切换、断线重连、窗口缩放/上下文切换、纹理热更新、箱子/墙/骑牛/阴影/水面/Stencil/UI 裁剪像素差分与 Fail-Closed 回退测试 |
