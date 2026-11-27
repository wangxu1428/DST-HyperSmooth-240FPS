# DST HyperSmooth 技术参考卷（Vol.1）：《异星工厂》（Factorio FFF）百万实体与 2D 渲染底层架构深挖及 DST 映射手册

- **文献维护**：DST HyperSmooth Architecture Team
- **归档路径**：[`docs/research/REF_VOL1_FACTORIO_FFF.md`](REF_VOL1_FACTORIO_FFF.md)（技术参考卷 Vol.1；续卷见 [`REF_VOL2_4_SODIUM_FOLIA_POE2.md`](REF_VOL2_4_SODIUM_FOLIA_POE2.md)）
- **对标工程基线**：DST HyperSmooth / HyperShade Client v3.9 – v4.0（架构演进路线见 [`../V4_0_ARCHITECTURE_ROADMAP.md`](../V4_0_ARCHITECTURE_ROADMAP.md)）与 HyperCore Server
- **证据与洁净室准则**：本文献仅收录经 Wube Software 官方开发日志（Factorio Friday Facts, FFF）及核心引擎主程（kovarex、posila、Rseding91、boskid、Harkonnen）公开证实的工程机制、实测数据与失败复盘（Postmortem），严格区分 **【原厂公开实证】** 与 **【DST 迁移推演】**；仅提取计算机图形学、缓存体系结构与并发系统公有原理，严禁复制或链接任何专有/受限代码。

---

## 一、为什么《异星工厂》是饥荒（DST）底层攻坚的头号跨界参考？

《饥荒联机版》（Don't Starve Together, DST）在保持原版 Lua 5.1 虚拟机与 Steam 创意工坊生态完全兼容的前提下，推进原生 C++ 引擎级性能优化时，面临的底层计算机体系结构约束与《异星工厂》（Factorio）高度同构：

1. **严格的 2D/2.5D 图集与画家遮挡顺序**：两者均以海量带 Alpha 通道的 2D 精灵（Sprite）与多层部件拼装为核心，无法简单依赖不透明 3D Early-Z 硬件剔除，极度依赖高吞吐图集合批（Sprite Batching）与确定性画家层序控制。
2. **单核确定性模拟包袱**：Factorio 因全图锁步联机（Lockstep Determinism）限制，核心模拟长期受困于单线程与内存延迟墙；DST 则因单线程原版 Lua 5.1 VM 与 C++ 实体组件强耦合而受困于主线程串行瓶颈。
3. **极端动静混合负载**：两者的后期超大规模基地均包含数千至数万级建筑、容器、作物、飞行物与粒子，90% 实体处于半静态或周期性状态，却容易因朴素每帧轮询耗尽帧预算。

---

## 二、六大核心 FFF 原理解剖与实测证据链（含官方失败复盘）

### 2.1 拓扑不变性与相对偏移编码（FFF #176）

- **一手文献**：[Friday Facts #176 - Belts optimization for 0.15](https://www.factorio.com/blog/post/fff-176)
- **【原厂公开实证】**：
  - **原始痛点**：大工厂传送带上常驻 20,000+ 移动物品，若每 Tick 遍历每个物品推进其绝对世界坐标，CPU 开销随物品总数呈线性 `O(N)` 增长。
  - **核心算法与数据结构**：
    1. **合并连续段（`TransportLine`）**：将 20–100 节同速相连的传送带合并为共享同一紧凑数组的逻辑传输线。
    2. **相对间距替代绝对坐标**：传送带未受阻或受阻段之前，物品间的相对拓扑顺序恒定不变。放弃存储每个物品的绝对坐标，改存 **相邻物品间距（Inter-item Gap）** 与首尾两端间隙（Terminal Gaps）。
    3. **均摊 `O(1)` 步进与单调索引缓存**：整条传输线上的 200 个物品每帧推进时，只需对首尾两个整数做一次加减，中间物品内存零触碰；遇到末端堵塞压缩间隙时，缓存“最后一个正间隙索引”（该索引在堵塞期只会单调递减而不会递增），使整条线维持关于物品数量的均摊常数时间复杂度。

### 2.2 内存延迟墙、软件预取与“去实体化”降维（FFF #204 / FFF #209）

- **一手文献**：[Friday Facts #204 - Another day, another optimisation](https://factorio.com/blog/post/fff-204) ｜ [Friday Facts #209 - Optimisation is a way of life](https://forums.factorio.com/viewtopic.php?t=52875)
- **【原厂公开实证】**：
  - **核心硬件判据**：kovarex 与 Rseding91 反复证实，大规模实体更新的真正瓶颈 **不是 CPU 算术逻辑单元（ALU），而是 DRAM 内存访问延迟与缓存未命中（Cache Miss）**。CPU 执行单实体逻辑仅需数纳秒，但一次跨页指针追逐（Pointer Chasing）等待主存读取动辄停顿 60–100 纳秒。
  - **三大实战手段与实测收益**：
    1. **显式软件预取（Software Prefetching, FFF #204）**：在遍历活跃实体链表时，先向内存控制器发出预取 **下一个实体** 数据的指令，紧接着执行 **当前实体** 的更新逻辑，用当前计算时间掩盖下一节点的内存读取延迟。经 3,600 Tick × 20 轮箱线图基准实测，非纯传送带大档获得 **+9% 至 +13%** 的全局更新提速（代码改动极小且高度隔离，但必须逐架构实测防负优化）。
    2. **热结构体瘦身（Struct Shrinking, FFF #204）**：缩减组装机（Crafting Machine）单对象内存体积以提升单个 CPU Cache Line 容纳密度，直接带来额外 **+2%** 的全局性能提升。
    3. **彻底“去实体化”（De-entitization, FFF #209）**：早期烟雾与粒子继承自通用多态 `Entity` 基类，背负虚表、块索引与堆分配开销。Rseding91 将其彻底剥离实体树，降维成仅占 **32 字节** 的纯数据 `TrivialSmoke` 紧凑连续数组顺序扫描。

### 2.3 MESI 缓存一致性铁律与多线程成败边界（FFF #215 / FFF #421）

- **一手文献**：[Friday Facts #215 - Multithreading issues](https://factorio.com/blog/post/fff-215) ｜ [Friday Facts #421 - Optimizations 2.0](https://www.factorio.com/blog/post/fff-421)
- **【原厂公开实证】**：
  - **经典踩坑复盘 1（盲目并行写反而更慢，FFF #215）**：
    - kovarex 尝试将逻辑上近乎独立的“列车、电网、传送带”拆到多线程并行更新，实测结果 **比单线程串行更慢**。
    - **底层根因（Cache Invalidation / False Sharing）**：由于未接管底层内存分配器做分域隔离，互不相关的实体对象极易落在同一个 CPU 缓存行或内存页上。一旦多核并发写入，硬件 MESI 协议被迫在核心私有 L1/L2 缓存间高频广播失效并同步刷回，总线风暴吞噬全部并行红利。
  - **正向标杆 1（8 线程只读渲染准备线性加速，FFF #215）**：
    - 与模拟并行失败形成鲜明对比，Factorio 的 **`prepare logic`（每帧遍历世界收集 Sprite 绘制指令 `DrawOrder`）最高开启 8 线程并行却表现极佳**。
    - **成功原因**：`prepare logic` 对游戏世界内存是 **100% 纯只读（Read-Only）**。各核心 L1/L2 缓存可同时持有同一内存行的共享副本（Shared 态），零缓存行失效，各自向线程私有缓冲区顺序追加绘制记录。
  - **正向标杆 2 vs 踩坑复盘 2（连通分量隔离 vs 跨网桥接，FFF #421）**：
    - **成功案例（14.9 倍提速）**：boskid 将互不相连的信号网络与传送带读取器拆分为严格无交集的连通分量更新组并紧凑排布内存，在 77,000 个运算器 / 6,000 个信号网络的压测存档中实现 **14.9× 加速**（耗时从 131 秒骤降至 8.2 秒，满载全核 CPU）。
    - **失败复盘（电网多线程流产）**：当同一台机器同时跨接两个独立电网时，为防非确定性失步必须动态合并更新组，叠加内存带宽触顶，最终未能跑赢单线程。

### 2.4 事件休眠、引用计数分桶与“幽灵预测 + 视锥保护带”（FFF #421 & 2.0 更新日志）

- **一手文献**：[Friday Facts #421 - Optimizations 2.0](https://www.factorio.com/blog/post/fff-421) ｜ [Factorio 2.0.0 Version History](https://wiki.factorio.com/Version_history/2.0.0)
- **【原厂公开实证】**：
  - **手段 1：彻底消灭空转轮询（Roboport 40 倍提速）**：剔除无任务指令塔的每 Tick 轮询改为事件唤醒后，实测大档指令塔每 Tick 平均耗时从 **1.0 ms 暴降至 0.025 ms**（单模块 40 倍加速）。同理，将 15,000 盏受控常亮灯免除每 Tick 电路轮询直接省下 **1.2 ms/Tick**。
  - **手段 2：引用计数信号量 + 分桶轮询（Radar Update Buckets）**：多雷达重叠覆盖同一区块时维护整数引用计数 `keep_revealed`（相当于计数信号量而非布尔锁）。`counter >= 1` 的活跃区块散列进多个更新桶（Update Buckets），**每 Tick 仅遍历 1 个桶**；归零时立即移出桶停止更新，实测整体提升 **+3.6% UPS**。
  - **手段 3：低频权威逻辑 + 高频“幽灵预测渲染”（Smarter Worker Robots）**：
    - 废除上万架物流机器人每 Tick 询问“到了吗？往前挪一步”的朴素状态机，改为预先计算直线飞行到达时刻并预约唤醒。
    - **双频分离**：真实机器人（Real Robot）移动中最多每 **20 Ticks** 更新一次逻辑，静止充电/悬停时每 **60 Ticks** 更新一次；而屏幕上玩家看到的丝滑连续飞行，完全由渲染器根据速度实时插值/外推的 **“幽灵机器人（Ghost Robot）”** 承担。
  - **手段 4：低频更新引发的边缘裁剪 Bug 与视锥保护带铁证**：
    - 当真实机器人的模拟包围盒每 20 Tick 才跳变一次时，靠近屏幕视野边缘飞入的机器人会因“模拟坐标仍在屏外、幽灵视觉坐标已进屏内”而被渲染空间搜索漏采导致边缘凭空闪烁。
    - Wube 官方在 2.0 版本日志中留下的精确解法：统一将实体渲染搜索框外扩 6 格保护带（`Unified entity_renderer_search_box_limits to 6 from all sides due to reduced update rate optimization of robots`）。

### 2.5 现代 2D 渲染管线、虚拟图集流送与环形卷轴缓冲（FFF #251 / #264 / #333）

- **一手文献**：[Friday Facts #251 - A Fistful of Frames](https://www.factorio.com/blog/post/fff-251) ｜ [Friday Facts #264 - Texture streaming](https://steamcommunity.com/ogg/427520/announcements/detail/2932299116257170530) ｜ [Friday Facts #333 - Terrain scrolling](https://www.factorio.com/blog/post/fff-333)
- **【原厂公开实证】**：
  - **虚拟纹理图集流送（Virtual Texture Mapping, FFF #264）**：
    - 高清 2D 精灵动辄吞噬 2.4 GB+ 显存，未塞进同一图集的零散贴图会频繁打断 Sprite Batching。
    - posila 引入单张物理驻留虚拟大纹理（Mega-Atlas）：所有可见精灵共享同一纹理坐标空间以维持单次合批；当玩家大幅拉远镜头同屏出现海量不同建筑时，由于精灵已被缩小，自动按需流送低分辨率 Mipmap 层级填入物理图集，既不爆显存又不断合批。
  - **反直觉 GPU 实测结论（透明像素 `discard` 无用论，FFF #264）**：
    - 团队专门编写了 Overdraw（过度绘制）热力可视化工具，测试在像素着色器中用 `discard` 丢弃全透明边缘像素。在 GTX 1060、Vega 64 乃至核显 Intel HD 5500 上的实测表明：**在现代 GPU 的 2D 画家混合管线中，`discard` 全透明像素几乎没有任何性能收益**，真正有效的是紧凑几何裁剪框与减少状态切换。
  - **NES 式环形卷轴缓存（Toroidal Scrolling Buffer, FFF #333）**：
    - 复杂地形过渡规则无法每帧全屏重画；旧方案缓存上一帧整屏贴图，静止时复制耗费 2 ms，一旦移动镜头重绘整张缓冲在 1600×900 分辨率下直接飙至 **5 ms GPU 时间**。
    - 终极解法：固定缓冲纹理不动，采用环形回绕坐标（Circular / Toroidal Wrap）滚动视口，镜头平移时 **每帧只增量渲染新露出的 L 型窄边条**，将平移镜头开销压至极限。

### 2.6 存档冻结的“99% 内存扫表定律”与写时复制边界（FFF #408 & Rseding91 实证）

- **一手文献**：[Non-blocking saves Technical Discussion (Rseding91, 2022–2026)](https://forums.factorio.com/viewtopic.php?t=103848)
- **【原厂公开实证】**：
  - **核心实证**：针对“把数据拷进内存缓冲，再开后台线程压缩写盘即可消除存档卡顿”的常见直觉，主程 Rseding91 明确给出结论：**“这本来就是现有的保存方式。存档耗时的 99% 全花在遍历游戏内存对象图填充那块内存缓冲区上；等缓冲区填完时，后台压缩和写盘线程早就干完了。”**
  - **跨平台边界**：Linux 端之所以能实现实验性的无感存档（`non-blocking-saving`），完全仰仗操作系统内核 `fork()` 系统调用与页表级硬件写时复制（Copy-On-Write）；而 Windows 内核缺乏同等轻量的进程内地址空间 CoW 快照原语（`PssCaptureSnapshot` 面向调试设计，存在低优先级内存配置惩罚且无法零成本直接按原指针遍历）。

---

## 三、对照校准矩阵（【DST 迁移推演】）：将 Factorio 工程准则映射到 DST HyperSmooth v4.0 与 HyperCore

结合 DST HyperSmooth v3.9/v4.0 源码基线与实机剖析证据（详见 [`../V4_0_ARCHITECTURE_ROADMAP.md`](../V4_0_ARCHITECTURE_ROADMAP.md)），提炼出以下七条可直接指导研发与验证的工程准则：

| Factorio 原厂铁律与出处 | DST 现存对应痛点（实机日志与代码剖析） | HyperSmooth v4.0 / HyperCore 落地与架构校准方案 |
| :--- | :--- | :--- |
| **1. 只读渲染采集并行 vs 并发写伪共享**（FFF #215） | 主线程串行构建 `AnimRenderCmd`；多线程若写共享结构易触发 Cache Invalidation | 严格锁定“模拟内存 100% 只读 + 线程私有 64B 对齐分片输出”契约（延续 `SkeletalMtPool` 优势），严禁后台线程写回 Klei `Entity` 或 `lua_State`。 |
| **2. 幽灵预测渲染 + 搜索框保护带**（FFF #421 & 2.0） | 主线程偶发 17–38 ms 波动拖垮高刷新率呈现；固定倍率保护带缺乏严格物理边界 | 规划“低频权威快照 + 高频有限阻尼外推呈现”；参照 `entity_renderer_search_box_limits`，保护带外扩半径按 `v_max * max_snapshot_age + entity_radius` 动态计算，超界自动钳位停止外推。 |
| **3. 链表遍历软件预取 + 32B 去实体化**（FFF #204 / #209） | 大基地 1,500–1,700+ 实体指针追逐 Cache Miss；雨雪/花粉等纯视觉实体背负完整 C++/Lua 对象开销 | 在 C++ 空间网格遍历与命令扫描循环中审慎评估 `_mm_prefetch`（须经 A/B 实测验证正收益）；纯客户端环境粒子继续推进 `VFX240` 紧凑 POD 数组化。 |
| **4. 连通分量与依赖分段**（FFF #421） | 画家顺序中穿插真实硬件 `Stencil`（`GL_ALWAYS+REPLACE` / `GL_GEQUAL` 遮罩/水面/裁剪）与混合状态，盲目全局抽离会破坏遮挡语义 | 采纳 **Render Dependency Graph + 分段排序（Segmented Sort）**：以真实 FBO/Stencil/非交换混合为硬屏障切分区段，仅在段内无冲突区间执行合批与无重叠位域排序。 |
| **5. 环形卷轴缓存与放弃无效 `discard`**（FFF #264 / #333） | `WorldShadow V2`（4096×4096 FBO）随镜头平移每帧重绘数十批次；着色器微优化误区 | 为世界空间阴影遮罩（`native_plugin/world_shadow_engine.h`）规划 **Toroidal 环形卷轴增量更新**（平移镜头仅增量重绘新进入的 L 型边缘带，静态实体阴影复用）；严禁在 `anim.ksh` 盲目插入透明像素 `discard`。 |
| **6. 引用计数分桶与消除空转轮询**（FFF #421） | 第三方重型模组在刷新制作栏时逐配方重复轮询（单帧可达十余毫秒） | 在 Lua 桥接层引入 **Tick/事件脏纪元缓存（Epoch Memoization）** 与分桶延迟刷新，背包/佩戴状态未变时直接 `O(1)` 命中缓存。 |
| **7. 存档 99% 扫表定律**（Rseding91 实证） | 虽已有 `native_plugin/async_save_commit.h` 异步压缩落盘，客户端仍出现 `scripts/dumper.lua` 约 94 ms 冻结 | 印证 Windows 下异步写盘仅解决最后 1%：必须全面启用客户端 C++ `FastDataDumper` 极速扫表（配合超限安全回退），从根源压低 Lua 对象图遍历耗时。 |

---

## 四、技术参考卷目录索引

1. **Vol.1（本卷）**：[`REF_VOL1_FACTORIO_FFF.md`](REF_VOL1_FACTORIO_FFF.md) —— 《异星工厂》（Factorio FFF）内存延迟墙、只读并行渲染准备、幽灵外推保护带、环形卷轴缓冲与 99% 存档扫表定律。
2. **Vol.2–Vol.4**：[`REF_VOL2_4_SODIUM_FOLIA_POE2.md`](REF_VOL2_4_SODIUM_FOLIA_POE2.md) —— Minecraft `Sodium`/`Iris`（保留态显存竞技场、GFNI 增量半透明排序）、`Folia`/`Minestom`（空间孤岛分片与广播包去重）、《流放之路 2》（Radiance Cascades `O(1)` 多光源软阴影与立体代理法线）。
3. **项目总架构演进路线图**：[`../V4_0_ARCHITECTURE_ROADMAP.md`](../V4_0_ARCHITECTURE_ROADMAP.md) —— 结合真机 RTX 4060 实测证据校准的 v4.0 分阶段演进蓝图。
