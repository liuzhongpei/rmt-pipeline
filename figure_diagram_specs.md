# EdgeNet-CoM Paper Figure Specifications

This document contains four figure briefs for generating publication-quality
principle diagrams with an external image or diagram model. The figure labels
should remain in English so that they can be inserted into the English IEEE
Transactions manuscript. The explanatory notes are in Chinese for review.

## Common Visual Rules

### 中文原理要求

- 采用 IEEE Transactions 风格的二维矢量框图：白色背景、黑色文字、细边框、直角或小圆角矩形。
- 使用统一配色：控制面蓝色、数据面绿色、并行处理橙色、应用场景紫色或灰色；颜色只用于分层，不表达性能高低。
- 所有箭头必须有明确方向；实线表示原型已实现并测试，虚线表示部署接口、建议扩展或未实现能力。
- 避免 3D、照片、装饰性渐变、发光、复杂卫星插画和不可读的小字。
- 图中文字必须短而准确；不要写 `P4 hardware switch`、`in-pipeline coding`、`3GPP-compliant UPF`、`line-rate` 或 `real satellite experiment`。
- 图例至少说明：`solid = implemented/evaluated`，`dashed = proposed or deployment boundary`。

### Common English Prompt Suffix

> Create a clean IEEE Transactions-style 2D vector architecture diagram for a
> technical paper. Use a white background, restrained blue/green/orange/gray
> palette, black sans-serif labels, consistent box sizes, orthogonal arrows,
> high contrast, and no decorative gradients or 3D effects. Keep every label
> short and legible at two-column print size. Use solid arrows for implemented
> and evaluated functions, dashed arrows or gray dashed boxes for proposed
> extensions and deployment boundaries. Add a compact legend: solid =
> implemented/evaluated; dashed = proposed or not implemented. Do not imply
> hardware P4 execution, in-pipeline network coding, 3GPP conformance, NIC
> line rate, or a real satellite experiment.

## Figure 1. Overall Architecture with Parallel Processing

### 目的

说明 EdgeNet-CoM 的四层整体结构，并明确数据面中的并行处理如何与单包
parse-classify-act 流程连接。该图应是论文的总架构图，读者看完后应能区分
控制面、编译器、RMT 数据面、并行队列，以及数据面外的 coding harness。

### 中文构图说明

从上到下或从左到右布置四个层次：

1. **Control plane**：`Synthetic/training data + policy tables` →
   `Float32 MLP training` → `Q16.16 quantization` → `Model/table loader`。
   旁边标注模型权重、QoS 映射、IPv4 LPM 表和 coding profile 表。
2. **P4-inspired compiler**：`P4-like source` → `Parser FSM + table entries +
   stage metadata + DNN extern binding`。明确这是软件编译器，不是硬件交换机。
3. **Software RMT data plane**：入口标为 `N6 post-decapsulation Ethernet /
   IPv4 / TCP / TLS packet`，先经过 `Parse/FSM`，再经过四个 match-action
   stages：`Classify (DNN extern)` → `Local QoS mark` → `IPv4 LPM route` →
   `MAC rewrite / deparser`，输出为 `marked and forwarded packet`。
4. **Parallel execution engine**：入口数据先到 `Single dispatcher`，再到
   `RSS 5-tuple hash`，分发到 `SPSC ring q0 ... qN-1`，每个队列连接一个
   `core-pinned worker`。每个 worker 运行相同的四-stage RMT pipeline，所有
   worker 共享只读的 DNN 权重和 policy tables。标注：`same TCP flow → same
   queue`、`FIFO per queue`，并注明排序保证依赖 `single dispatcher` 假设。
5. 在 RMT 输出旁画一条**虚线**分支到 `Coding policy harness (outside RMT
   fast path)`，依次连接 `class metadata` → `profile lookup` →
   `generation accounting` → `RLNC/XOR encode-decode tests`。标注：`no repair
   packet injection into RMT deparser`。
6. 右下角用灰色虚线框放置部署边界：`N3/N9 GTP-U decapsulation`、`SMF/PFCP
   authorization`、`real NIC I/O`、`satellite-link feedback`，并统一标注
   `future deployment work / not implemented`。

### 可直接给制图模型的英文 Prompt

> Draw the overall EdgeNet-CoM architecture as four horizontal layers. At the
> top, show a blue **Control Plane**: “synthetic/training data + policy tables”
> flows to “Float32 MLP training”, then “Q16.16 quantization”, then “model and
> table loader”. Next show a **P4-inspired compiler**: “P4-like source” flows to
> “parser FSM, match-action tables, stage metadata, DNN extern binding”. Do not
> depict a hardware switch.
>
> In the center, show the green **Software RMT Data Plane**. The solid input is
> “N6 post-decapsulation Ethernet/IPv4/TCP/TLS packet”. It first passes through
> “Parse/FSM”, then through four match-action stages: “Stage 0 classify:
> Q16.16 DNN extern”, “Stage 1 local QoS mark”, “Stage 2 IPv4 LPM route”, and
> “Stage 3 MAC rewrite/deparser”, producing “marked and forwarded packet”.
>
> Below or beside the data plane, show the orange **Parallel Execution Engine**:
> “single dispatcher” -> “RSS 5-tuple hash” -> “SPSC rings q0 ... qN-1” ->
> multiple “core-pinned worker” boxes. Put the same four-stage RMT pipeline
> inside each worker, connect all workers to shared read-only “DNN weights and
> policy tables”, and annotate “same TCP flow -> same queue”, “FIFO per queue”,
> and “ordering guarantee assumes one dispatcher”.
>
> From the marked output, draw a dashed branch to a gray **Coding Policy Harness
> (outside RMT fast path)**: “class metadata” -> “profile lookup” -> “generation
> accounting” -> “RLNC/XOR tests”. Add the note “no repair-packet injection into
> RMT deparser”. At the lower right, add a dashed gray deployment-boundary box
> containing “N3/N9 GTP-U decapsulation”, “SMF/PFCP authorization”, “real NIC
> I/O”, and “satellite-link feedback”; label it “future deployment work / not
> implemented”. Add the solid/dashed legend.

### 建议图注

**Fig. 1. Overall EdgeNet-CoM architecture with flow-affine parallel execution.**
The evaluated software RMT path uses a four-stage parse-classify-act traversal;
RSS hashing, SPSC queues, and core-pinned workers provide parallel execution
under a single-dispatcher ordering assumption. The coding policy harness is
connected to the marked output but remains outside the RMT fast path.

## Figure 2. Soft-RMT-Pipeline Processing Flow

### 目的

把一个数据包在 Soft-RMT pipeline 中的实际处理过程画清楚，尤其是 parser
bypass、四个 match-action stages、PHV、VLIW stage execution 和 deparser。
此图关注“一个包怎么走”，不再承担总架构图的控制面和部署场景信息。

### 中文构图说明

建议采用横向主流程加上下分支：

1. 入口：`Raw Ethernet frame at N6` → `Parser FSM`。
2. Parser 依次判断：`ethertype == IPv4?`、`ipv4.proto == TCP?`、
   `tcp.dport == 443?`、`TLS header valid?`、`tls.type == 23?`。
3. 任一判断为否，进入灰色 `bypass / default policy`，不执行 DNN。普通非
   TLS 流量绕过分类和 QoS stage；TLS handshake 仍可到达表，但 classify_t
   采用 no-op/default 行为。后续 route/deparser 是否转发或丢弃由 route table 决定。
4. 通过判断后，抽取四个字段：`ipv4.len`、`tcp.window`、`tls.length`、
   `tls.type`，写入 `PHV / metadata`。
5. 四个绿色 stage 串联：
   - `Stage 0: classify_t` → `DNN infer` → `meta.class`；
   - `Stage 1: qos_t` → `class exact match` → `meta.qos + local ipv4.tos mark`；
   - `Stage 2: route` → `IPv4 destination LPM` → `egress port or drop`；
   - `Stage 3: mac_rewrite` → `egress-port exact match` → `src/dst MAC rewrite`。
6. 末端为 `deparser` → `forwarded output` 或 `drop`。
7. 每个 stage 旁边放一个小 inset：`table lookup` → `16-wide VLIW ALUs`，
   上方写 `Phase 1: read PHV`，下方写 `Phase 2: write-back/commit`，说明同一
   stage 内 ALU 并行、stage 之间按顺序执行。
8. 图中明确标注：`encrypted TLS payload is not read`；`N3/N9 GTP-U parsing is
   outside the evaluated path`。

### 可直接给制图模型的英文 Prompt

> Draw a horizontal packet-processing flow named “Soft-RMT-Pipeline Processing
> Flow”. Start with “raw Ethernet frame at N6” entering a parser FSM. Show
> decision diamonds in this order: “ethertype = IPv4?”, “IPv4 protocol = TCP?”,
> “TCP destination port = 443?”, “TLS header valid?”, and “TLS type = 23
> application_data?”. Route every negative branch to a gray “bypass / default
> policy” path that skips DNN inference. Annotate that non-TLS packets bypass the
> classifier and QoS stage, while TLS handshake packets use the classify_t
> no-op/default behavior.
>
> For the positive path, show feature extraction into a “PHV + metadata” box
> containing exactly “ipv4.len, tcp.window, tls.length, tls.type”. Then show four
> sequential green pipeline stages: “Stage 0 classify_t: Q16.16 DNN infer ->
> meta.class”; “Stage 1 qos_t: exact class match -> meta.qos + local ipv4.tos
> mark”; “Stage 2 route: IPv4 destination LPM -> egress port or drop”; and
> “Stage 3 mac_rewrite: egress-port match -> MAC rewrite”. End at “deparser ->
> forwarded output” or “drop”. Add the note “encrypted TLS payload is not read”.
>
> Add a small inset connected to the stages: “table lookup -> 16-wide VLIW ALUs”,
> with “Phase 1: read PHV” and “Phase 2: write-back/commit”. Make clear that ALUs
> are parallel within one stage while the four stages execute sequentially. Add
> a dashed note “N3/N9 GTP-U parsing is outside the evaluated path”. Use solid
> arrows for the implemented path and gray bypass arrows for unsupported traffic.

### 建议图注

**Fig. 2. Soft-RMT packet-processing flow.** Supported N6 Ethernet/IPv4/TCP/TLS
application-data packets traverse parser, fixed-point classification, local QoS
marking, destination-based routing, and MAC rewriting; non-TLS traffic follows
the bypass/default path and TLS handshakes use the no-op classification action.
VLIW ALUs execute in parallel within a stage using two-phase read/write-back
semantics, while stages execute sequentially.

## Figure 3. DNN Training, Quantization, and Inference Flow

### 目的

区分控制面训练流程和数据面推理流程，说明为什么数据面可以只使用整数运算，
以及四个可见 header features 如何变成 service class。该图不要画成通用深度
学习宣传图，必须体现实现中的 4-24-4 MLP 和 Q16.16。

### 中文构图说明

采用上下两条泳道：上方 `Control Plane / offline`，下方 `Data Plane / per packet`。

**上方控制面：**

`labeled synthetic feature vectors` → `normalize by [1500, 65536, 16384, 256]`
→ `Float32 MLP: 4 inputs -> 24 ReLU hidden units -> 4 logits`
→ `softmax cross-entropy + SGD` → `trained W1,b1,W2,b2`
→ `Q16.16 quantization: scale 2^16, round, saturate`
→ `rmt_dnn_load()` → `read-only shared model weights`。

**下方数据面：**

`parsed header features` → `integer normalization: (f_i << 16)/s_i`
→ `24 hidden MACs + ReLU` → `4 output logits`
→ `argmax (no softmax)` → `class c0/c1/c2/c3`。

右侧画 class-to-action mapping：

- `c0 Web` → `local mark 0 / BE` → `XOR k=8,n=9` in harness;
- `c1 Video` → `local mark 32 / intended CS4` → `RLNC k=8,n=9`;
- `c2 VoIP` → `local mark 46 / intended EF` → `RLNC k=4,n=5`;
- `c3 Bulk` → `local mark 10 / intended AF11` → `no coding`。

在 mapping 旁标注：`local mark is a prototype test value; production DSCP
encoding must shift into the DS field and preserve ECN bits`。用虚线表示 coding
harness 分支，并写 `profile selection only; no in-pipeline repair emission`。

底部放三个小限制标签：`no payload decryption`、`integer-only data-plane
inference`、`float training remains in control plane`。

### 可直接给制图模型的英文 Prompt

> Draw a two-lane diagram titled “DNN Training, Quantization, and Inference in
> EdgeNet-CoM”. The upper blue lane is “Control Plane / offline”. Show: “labeled
> synthetic feature vectors” -> “normalize by scales [1500, 65536, 16384, 256]”
> -> “Float32 MLP: 4 inputs, 24 ReLU hidden units, 4 output logits” -> “softmax
> cross-entropy + SGD” -> “trained W1,b1,W2,b2” -> “Q16.16 quantization: scale
> 2^16, round, saturate” -> “rmt_dnn_load()” -> “read-only shared model weights”.
>
> The lower green lane is “Data Plane / per packet”. Show: “parsed header
> features [ipv4.len, tcp.window, tls.length, tls.type]” -> “integer
> normalization (f_i << 16)/s_i” -> “24 hidden MACs + ReLU” -> “4 output
> logits” -> “argmax; no softmax” -> “service class c0..c3”. Add the note “no
> payload decryption; integer-only data-plane inference”.
>
> On the right, show a compact class-to-action table: “c0 Web -> local mark 0,
> intended BE, harness XOR k=8,n=9”; “c1 Video -> local mark 32, intended CS4,
> harness RLNC k=8,n=9”; “c2 VoIP -> local mark 46, intended EF, harness RLNC
> k=4,n=5”; “c3 Bulk -> local mark 10, intended AF11, harness no coding”. Use a
> dashed connector for the coding harness and label it “profile selection only;
> no in-pipeline repair emission”. Add a footnote: “prototype local marks are
> test values; production DSCP encoding must shift into the DS field and preserve
> ECN bits”.

### 建议图注

**Fig. 3. Control-plane training and data-plane DNN inference.** The control
plane trains a Float32 4-24-4 MLP and installs Q16.16 parameters. The data plane
normalizes four visible header features, performs integer-only inference, and
exposes the argmax class to local QoS actions; coding uses the class only in the
connected harness.

## Figure 4. Satellite-UPF Application Scenario

### 目的

展示 EdgeNet-CoM 的 motivating deployment context，同时清楚地区分已评估的
N6 placement、未实现的 N3/N9 GTP-U 处理、实际实现的 destination-LPM 路由、
以及 proposed 的 class-aware path/coding policy。该图是场景图，不应把概念路径
画成已完成的卫星实验。

### 中文构图说明

建议从左到右画网络路径：

1. 左侧画 `UEs / satellite access network`，旁边可有 `encrypted HTTPS/TLS`
   图标，但不要画明文 payload。
2. 进入 `N3/N9: GTP-U over UDP/2152`，用灰色虚线框标注
   `decapsulation required before current parser`。
3. 解封装后进入绿色实线框 `N6 reference point: decapsulated Ethernet /
   IPv4 / TCP / TLS`，再进入 `EdgeNet-CoM software RMT pipeline`。
4. RMT pipeline 内简写为 `parse -> Q16.16 classify -> local QoS mark ->
   destination LPM -> MAC rewrite`，输出 `marked packet + class metadata`。
5. 输出连接到一个 `Gateway / UPF forwarding boundary`，再分为四条概念路径：
   `GEO relay`、`LEO spot-beam path`、`inter-satellite link (ISL)`、
   `terrestrial backhaul`。在路径旁写 delay/capacity/loss 是异构的，但不要
   放未经测量的性能数字。
6. 在 forwarding boundary 上方放蓝色虚线 `Operator policy / proposed Mode B`，
   内容为 `(dst_prefix, class) + link monitoring -> class-aware path choice`。
   同时在 RMT 下方放虚线 `Coding policy harness -> per-class RLNC/XOR profile`
   并注明 `generation accounting; repair emission and receiver integration not
   implemented`。
7. 右侧画 `service endpoints / terrestrial cloud`，并用小标签说明四类策略：
   `Web: BE/XOR`、`Video: CS4/RLNC`、`VoIP: EF/RLNC`、`Bulk: AF11/no coding`。
   这些标签旁加 `illustrative local policy, not standardized entitlement`。
8. 右下角放限制说明：`prototype evaluated at post-decapsulation N6 on a
   commodity x86 host; no satellite-link trace, real NIC, SMF/PFCP, or 3GPP UPF
   conformance evaluation`。

### 可直接给制图模型的英文 Prompt

> Draw a left-to-right satellite-UPF application scenario for the EdgeNet-CoM
> paper. On the left, show “UEs / satellite access network” sending encrypted
> HTTPS/TLS traffic. Then show a gray dashed boundary labeled “N3/N9: GTP-U over
> UDP/2152; decapsulation required before current parser”. After decapsulation,
> show a solid green “N6 reference point: decapsulated Ethernet/IPv4/TCP/TLS”
> entering “EdgeNet-CoM software RMT pipeline”. Inside that box write the compact
> flow “parse -> Q16.16 classify -> local QoS mark -> destination LPM -> MAC
> rewrite”, and output “marked packet + class metadata”.
>
> To the right, show a “Gateway / UPF forwarding boundary” with four heterogeneous
> conceptual paths: “GEO relay”, “LEO spot-beam path”, “inter-satellite link
> (ISL)”, and “terrestrial backhaul”. Annotate only “different delay, capacity,
> and loss”; do not add measured satellite performance numbers.
>
> Above the forwarding boundary, add a blue dashed “Operator policy / proposed
> Mode B” box: “(dst_prefix, class) + link monitoring -> class-aware path choice”.
> Below the RMT pipeline, add a dashed “Coding policy harness” branch leading to
> “per-class RLNC/XOR profile; generation accounting”, with the note “repair
> emission and receiver integration not implemented”. On the right, show service
> endpoints / terrestrial cloud and four small policy labels: “Web: BE/XOR”,
> “Video: CS4/RLNC”, “VoIP: EF/RLNC”, and “Bulk: AF11/no coding”; label them
> “illustrative local policy, not standardized entitlement”.
>
> Add a bottom gray note: “evaluated at post-decapsulation N6 on commodity x86;
> no satellite-link trace, real NIC, SMF/PFCP, or 3GPP UPF conformance
> evaluation”. Use solid arrows only for the evaluated N6 software path and
> dashed arrows/boxes for N3/N9 handling, class-aware path selection, link
> monitoring, and coding packet emission.

### 建议图注

**Fig. 4. Satellite-UPF application scenario and deployment boundary.** The
prototype is evaluated at the post-decapsulation N6 reference point. It writes a
local QoS mark and performs destination-based forwarding, while class-aware path
selection and coding emission are shown as policy extensions. GEO, LEO, ISL, and
terrestrial paths represent the motivating heterogeneous-link context rather
than measured satellite experiments.

## Suggested Placement in the Manuscript

- **Fig. 1**: Section 4.1, immediately after the first paragraph introducing the four layers.
- **Fig. 2**: Section 4.2 or 4.4, after the parser and stage-execution description.
- **Fig. 3**: Section 4.3, before Algorithm 1 or immediately after the network-architecture paragraph.
- **Fig. 4**: Section 3.3 or Section 6.2, where the N6 placement and deployment boundary are discussed.

## Numbering and Integration Note

The current manuscript already contains three figure placeholders. To avoid
duplicate numbering, use this replacement plan:

1. Replace the current architecture placeholder labelled Figure 1 with the new
   overall architecture/parallel-processing diagram.
2. Replace the current parser-state placeholder labelled Figure 2 with the new
   Soft-RMT pipeline-flow diagram; retain the parser FSM decision graph as a
   small inset if it remains useful.
3. Insert the new DNN diagram as Figure 3.
4. Insert the satellite-UPF scenario as Figure 4.
5. The current classification-coding integration placeholder (currently Figure
   3) should either be folded into Figure 1 as the dashed coding-harness branch,
   or retained as a separate Figure 5. If retained, update its caption and all
   in-text references consistently.

When the external model returns the diagrams, export them as high-resolution
SVG/PDF (preferred) or 600-dpi PNG. Before inserting them, verify that every
label matches the manuscript terminology, that solid/dashed semantics are
preserved, and that no generated icon implies unsupported hardware or satellite
measurements.
