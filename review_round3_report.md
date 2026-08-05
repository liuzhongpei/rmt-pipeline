# Third-Round Peer Review Package — EdgeNet-CoM

**Paper:** EdgeNet-CoM: Service-Aware In-Network Computing for Satellite User Plane Functions
**Review Type:** Full simulated peer review (5-person panel: EIC + 3 peer reviewers + Devil's Advocate)
**Review Date:** 2026-08-04
**Manuscript under review:** `paper_innetwork_computing.md` (identical to the uploaded PDF extraction)
**Artifact verified:** this repository (`src/`, `include/`, `p4/`, `scripts/`) — all reviewers had read-only code-audit access

## Review Panel Provenance (#540)

All five reviewer personas ran on a single model family. Persona diversity is not model diversity — blind spots may be correlated across reviewers (Ren et al. 2026, arXiv:2607.13104 §5.2).

---

## 第 0 部分：中文执行摘要

**决定：Major Revision（大修）** —— 五位评审人（含主编与魔鬼代言人）一致建议大修。加权分 64–72（边缘区间），但存在多项可修复的重大问题，需再审。

**论文的真实优点（评审团共识）**：
- 声明边界（claim scoping）的纪律性在系统类论文中罕见——摘要末句、§5、§6 对合成数据、无真实流量、无 NIC/5G 集成等限制均有明确交代；
- Artifact 经代码级核验基本属实：seed 42、Table 6 生成器区间、Q16.16 一致性测试、16 路 VLIW 两阶段提交、computed-goto、P4 子集编译器、bench_all.sh 全部真实存在且与描述相符；
- 限制章节（§6）异常完整诚实，是论文最强的一节。

**阻断接收的三大问题**：
1. **PGO 6.8× 加速不可信（R1，Critical）**：Makefile 自述 PGO 仅比 `-O3 -flto` 快 10–20%，但 Table 8 将 ~6,300 ns → 933 ns 全部归因于 PGO，且未披露 baseline 编译选项。若 baseline 实为 -O0，则必须重述整个性能叙事。
2. **分类证据链空心化（EIC/R1/DA）**：训练与测试同源于一个"刻意分离"的合成生成器，且**当前版本没有任何基线**（注意：第二轮审稿时曾有一个 94.2% 的规则基线，本版本反而删除了）；无混淆矩阵、无多种子方差。99.1% 主要是生成器可分性的性质，不是模型质量的证据。仓库中 `real_traffic_test_report.md`（高斯校准分布上 97.0%）可直接补强却未被引用。
3. **"Satellite UPF" 架构前提断裂（R3，Critical）**：真实 5G UPF 的 N3/N9 接口流量是 GTP-U 封装（UDP 2152），当前解析器只能看到外层 UDP，永远触达不到内层 TLS 记录——分类器在真实部署点上根本不工作。此外论文没有评估任何卫星特有机制（Mode B 未实现、无链路仿真），且未讨论 UPF 层 RLNC 与 TCP 拥塞控制、与 DVB-S2X ACM 链路层 FEC 的相互作用。

**其他必须修复项**：Table 5 代码行数 5/9 个模块与 `wc -l` 偏差 18–87%（三位评审人独立核验）；运行时表更新无任何线程安全机制但论文未披露；Application B 实为管线外 harness，"两个耦合决策"的贡献表述需收敛；缺失 NetBricks/eBPF-XDP 等软件数据面文献与 Table 1 的 DPDK+ML 对照列；VoIP k=4 生成缓冲约 60–80 ms 与 EF/G.114 延迟预算冲突；`dnn.c` argmax 以 `best_v=0` 初始化（全负 logit 时恒输出类 0，代码级 bug）。

**DA 三项 CRITICAL 的裁决**：C2（99.1% 无基线不可解释）**成立**，已由 R3/R4/R5/R6 修订项承接；C1（编码决策是管线外脚手架）作为 Critical **不成立**（论文在 §1.4/§4.7.4/Algorithm 3 等处一贯披露 harness 边界），但 §1.3 "Application B 应用实例化"表述确实过度，降为 Major 处理；C3（12.3% 是算术恒等式）作为 Critical **不成立**（摘要含 "projected"、§5.10.4 已明示"确定性后果"），改写建议保留。

**修改期限建议**：6–8 周（2026-09-29 前）；修改后将再审。完整路线图见第 2 部分；五份独立评审报告全文见附录 A–E。

---

# Part 1: Editorial Decision Letter

Dear Author(s),

Thank you for submitting your manuscript titled "EdgeNet-CoM: Service-Aware In-Network Computing for Satellite User Plane Functions" to IEEE Transactions on Network and Service Management (simulated venue). Your manuscript has been reviewed by five independent reviewers: the Editor-in-Chief, a network-systems measurement methodologist, a programmable-data-plane domain expert, a satellite/NTN systems engineer, and a Devil's Advocate reviewer with code-audit access to your artifact repository.

## Decision: Major Revision

## Reviewer Summary

| Reviewer | Role | Recommendation | Confidence | Weighted Score |
|----------|------|---------------|------------|----------------|
| EIC | Senior Editor, IEEE TNSM (NFV/5G UPF background) | Major Revision | 4/5 | 65 |
| Reviewer 1 | Network systems measurement & ML-evaluation methodologist | Major Revision | 5/5 | 64 |
| Reviewer 2 | Programmable data planes / in-network ML / 3GPP UPF expert | Major Revision | 5/5 | 72 (Minor band; Major recommended for structural issues) |
| Reviewer 3 | Satellite NTN systems engineer (3GPP NTN, DVB-S2X, TCP-over-satellite) | Major Revision | 5/5 | 67 (borderline; Major recommended) |
| Devil's Advocate | Adversarial auditor (code-audit based) | Major Revision (advisory) | 4/5 | — |

## Consensus Analysis

### Points of Agreement

**Strengths (CONSENSUS-4, all four panel reviewers):**
1. Exemplary claim-scoping discipline and limitation disclosure (§1.1, §5.3, §6.1) — EIC S1/S4, R1 S4, R2 S5, R3 S1.
2. The artifact is real and faithfully implements the paper's algorithms: seed-42 generator, Table 6 ranges, Q16.16 forward pass, two-phase-commit VLIW, computed-goto dispatch, P4-subset compiler, bench_all.sh — EIC S2, R1 S1–S3/S6, R2 S1–S4/S6/S7, R3 S3.
3. Internal arithmetic of Tables 16/17 is exact; honest reporting of sub-linear scaling (Table 9) — EIC S5, R1 S5, DA Observations.

**Weaknesses (CONSENSUS-3):**
1. **[SC-1] Table 5 LOC claims are materially inaccurate** (EIC W1, R1 W3, R2 Minor Issues; R3 silent; DA M3 corroborates by audit). Five of nine modules deviate 18–87% from `wc -l`; the "~4,200 core LOC" claim does not match the actual sum (~3,518). Must fix.
2. **[SC-3] The classification evidence base is hollow: circular generator + zero baselines** (EIC W4, R1 W2, R3 Detailed Comments; R2 silent; DA C2/M6). 99.1% on deliberately separated class-conditional boxes, with no rule/classical/production baseline, no confusion matrix, no seed variance, is not interpretable as model quality. Must fix.

**Corroborated findings (2/4 reviewers):**
- **[SC-7] Satellite framing vs. evaluation disconnect** (EIC W2, R3 W7; DA M5): no experiment depends on any satellite property.
- **[SC-18] DSCP/QFI and GTP-U tunnel-context ambiguity** (R2 W8, R3 W6).

**Single-reviewer findings at Confidence 5 from the responsible domain owner** (carried with full weight per the confidence-weighting rule): SC-2 PGO claim (R1), SC-4 confusion matrix (EIC), SC-5 variance (R1), SC-6 unused calibrated-traffic results (EIC), SC-8 GTP-U (R3), SC-10 runtime-update thread safety (R2), SC-11 TCP/coding interaction (R3), SC-12 link-layer FEC interaction (R3), SC-13 missing literature (R2).

### Points of Disagreement

**Disagreement 1: Severity of the 12.3% repair-ratio framing**
- **EIC view**: Minor (W8) — the paper says "projected" and §5.10.4 fully discloses the arithmetic nature.
- **DA view**: CRITICAL (C3) — the abstract presents an arithmetic identity as a result.
- **Disagreement type**: Severity disagreement.
- **Editor's Resolution**: REJECTED as Critical; retained as a P2 reframing item (S2).
- **Resolution Rationale**: The manuscript itself contains the disclosure ("projected" in the abstract; §5.10.4 "deterministic consequence… it does not establish application quality or loss recovery"). A Critical requires the defect to invalidate a core claim *as written*; the text does not make the misrepresentation DA attributes to it. The residual improvement is presentational, hence P2.

**Disagreement 2: Severity of the five-challenge/five-pillar structure**
- **EIC view**: Minor padding (W5). **DA view**: Major post-hoc circularity (M4).
- **Disagreement type**: Severity disagreement.
- **Editor's Resolution**: P2 presentation item (S7).
- **Resolution Rationale**: Challenge→pillar 1:1 mapping is a conventional systems-paper device; DA's point that it reads as reverse-engineered has merit but is not decision-bearing given the paper's honest scoping elsewhere. Tightening is cheap and improves the paper.

**Disagreement 3: Scores in the Minor band vs. Major recommendations (R2: 72, R3: 67)**
- Both reviewers explain that structural/framing issues (SC-8/SC-9/SC-10 for R2; SC-7/SC-8 for R3) outweigh their dimension averages.
- **Editor's Resolution**: The decision matrix over reviewer recommendations governs: 5/5 Major Revision → **Major Revision**.

### Devil's Advocate CRITICAL Adjudications (mandatory visibility)

| ID | DA claim | Corroboration | EIC adjudication | Disposition |
|----|----------|---------------|------------------|-------------|
| C1 | Coding actuation is test scaffolding, yet framed as pipeline behavior | R2 W3 (Major) | **REJECTED as Critical**: §1.4 Contribution 3, §4.7.4, Algorithm 3 note, Figure 3, and the Table 16 caption all explicitly state the harness boundary — the abstract/conclusion do not claim pipeline-resident coding. **VALIDATED as Major** for the §1.3 "two representative application instantiations" framing, which implies co-equal integration | Carried as R9 |
| C2 | 99.1% is a tautological property of the generator; no baseline renders it uninterpretable | EIC W4, R1 W2, R3 Detailed Comments | **VALIDATED** — the strongest DA finding; fully corroborated by the panel | Carried as R3/R4/R5/R6; blocks Accept |
| C3 | 12.3% repair ratio is an arithmetic identity framed as a result | EIC W8 (as Minor) | **REJECTED as Critical**: disclosure present in abstract ("projected") and §5.10.4; reframing suggestion retained | Carried as S2 |

### Editorial continuity note (editorial context, not a panel finding)

This manuscript is the third iteration; the repository contains a Round-2 review report. Most Round-2 contradictions (CI claim vs. §6.1, "3.4×/near-linear" scaling, "entirely in P4", "first packet of a flow") have been resolved in this version. One item has **regressed**: Round 2 included a rule-based baseline (reported 94.2% vs. 99.1%); the current version removes it and §5.7 now states no baseline exists. Reviewers independently flagged the missing baseline (SC-3) without knowing this history; restoring and improving that baseline is the fastest way to close R3 below.

### Decision Rationale

The panel is unanimous on Major Revision, and the decision matrix maps a 5/5 Major panel to Major Revision. The manuscript has genuine strengths — an honestly scoped, artifact-verified software RMT pipeline with a correct Q16.16 inference engine and a well-engineered multi-queue core — but three issues currently block acceptance. First, the performance narrative rests on a PGO claim (6,300 ns → 933 ns, 6.8×) that contradicts the artifact's own Makefile (10–20% expected from PGO over `-O3 -flto`); until the baseline configuration is disclosed and the decomposition re-measured, the headline service time is not reproducible as described (SC-2, Critical, R1). Second, the classification evidence is circular and baseline-free; the panel consensus (SC-3) plus the DA's validated C2 make the 99.1% figure uninterpretable as model quality — particularly damaging because a rule baseline existed in the previous revision and was removed. Third, the "satellite UPF" framing collides with two architectural facts: real UPF traffic is GTP-U encapsulated, which the parser cannot process (SC-8, Critical, R3), and no satellite-specific mechanism is evaluated (SC-7). None of these is fatal: each has a concrete remediation path in the roadmap below (re-measurement, baselines + calibrated-distribution results already present in the repository, and either a GTP-aware parser or an explicit N6-placement repositioning). The weaknesses are therefore repairable rather than fundamental, which is precisely the Major Revision criterion. A Minor Revision is not defensible because R2–R8 require new experiments or structural reframing; Reject is not warranted because the architectural contribution is real, verified, and honestly presented.

### Top Blocking Issues

| Rank | Blocking issue | Source reviewer(s) | Evidence anchor | Resolving roadmap item |
|------|----------------|--------------------|-----------------|------------------------|
| 1 | PGO 6.8× claim contradicts Makefile ("10–20%"); baseline flags undisclosed | R1 (Critical) | `dataset: Makefile line 97 vs Table 8 rows 1–2` | R2 |
| 2 | Classification accuracy uninterpretable: circular generator, no baseline, no confusion matrix, no variance; rule baseline from prior revision removed | EIC + R1 (+DA C2) | `text: §5.7 "does not contain a reproducible classical or production-tool baseline"` | R3–R6 |
| 3 | GTP-U encapsulation gap: parser cannot see inner TLS at N3/N9; no satellite-specific mechanism evaluated | R3 (Critical) + EIC | `absence: src/parser.c, p4/*.p4 — expected GTP-U (UDP 2152) decapsulation; checked all .p4 and .c files` | R7, R8 |

---

# Part 2: Revision Roadmap

## Required Revisions (Must Fix)

| # | Revision Item | Sub-Claim(s) | Severity | Evidence Anchor | Confidence | Source | Priority | Estimated Effort |
|---|--------------|--------------|----------|-----------------|------------|--------|----------|-----------------|
| R1 | Re-measure all LOC with a documented method (e.g., `wc -l`); correct Table 5, abstract, §1.3, §5.1 | SC-1 | major | `dataset: Table 5 vs wc -l — p4c.c 933 vs ~1200; parser.c 187 vs ~350` | 5 — direct measurement | EIC/R1 (+DA) | P1 | 0.5 days |
| R2 | Disclose exact compiler flags for both Table 8 configurations; add an intermediate `-O3 -flto` row; rename rows if the "baseline" is not PGO-comparable | SC-2 | critical | `dataset: Makefile line 97 "10–20% additional speedup" vs Table 8 "~6,300 → 933 ns"` | 5 — core expertise: PGO benchmarking | R1 | P1 | 2–3 days |
| R3 | Implement ≥1 non-neural baseline (threshold-rule classifier on the same 4 features; optionally decision tree / k-NN) on the identical train/test split; report accuracy + latency | SC-3 | major | `text: §5.7 "does not contain a reproducible classical or production-tool baseline"` | 5 — ML evaluation | EIC/R1 (+DA C2) | P1 | 3–5 days |
| R4 | Record and report the full 4×4 confusion matrix; compute precision/macro-F1; analyze operational safety of dominant confusion pairs against Tables 3–4 | SC-4 | major | `text: §5.3(c) "does not support claims about which incorrect class was selected"` | 5 — classifier evaluation | EIC | P1 | 1–2 days |
| R5 | Report variance: repeat training/evaluation over 5–10 seeds (or k-fold); give mean ± std for overall and per-class accuracy | SC-5 | major | `absence: §5.3/§5.9/Table 12 — expected CIs or seed variance; checked §5.3, §5.9, §6.1` | 5 — ML evaluation | R1 | P1 | 2–3 days |
| R6 | Integrate `real_traffic_test_report.md` results (97.0% on Gaussian-calibrated distributions; Video 91.6%) as a new subsection, or explain the exclusion | SC-6 | major | `dataset: real_traffic_test_report.md — 97.0% accuracy, not discussed in §5/§6` | 5 — direct observation | EIC | P1 | 2–3 days |
| R7 | Reconcile satellite framing with evidence: either add one satellite-dependent experiment (e.g., netem GEO/LEO delay-loss trace over the pipeline/coding path) or restructure the paper as a programmable-data-plane framework with satellite as motivating scenario (incl. title adjustment) | SC-7 | major | `absence: §5 — expected any satellite-parameterized experiment; checked Tables 2/6/9 and all experimental parameters` | 5 — satellite systems / editorial | EIC/R3 (+DA M5) | P1 | 1–2 weeks |
| R8 | Close the GTP-U architectural gap: extend the parser for outer Eth/IP/UDP/GTP-U → inner IP/TCP/TLS and re-run key tests, OR explicitly reposition the classifier at the post-decapsulation N6 interface and discuss UPF placement consequences | SC-8 | critical | `absence: src/parser.c, p4/enc_classify.p4 — expected GTP-U parsing (UDP 2152, TEID, inner headers); checked all .p4/.c files` | 5 — 3GPP user plane | R3 | P1 | 1–2 weeks |
| R9 | Reframe Application B / Contribution 3 as "a connected harness validating policy coupling" (or integrate an NC extern stub writing generation metadata into PHV); align §1.3 "two representative applications" language | SC-9 | major | `text: §4.7.4 "not equivalent to a stateful NC extern in the packet pipeline"` | 4/5 — RMT extern design | R2 (+DA C1) | P1 | 2–4 days |
| R10 | Disclose runtime-update concurrency limits: state that `rmt_load_entries()`/`rmt_dnn_load()` are not safe concurrent with packet processing; specify the intended update model (stop-the-world / shadow-swap / RCU) in §6.1–6.2 | SC-10 | major | `absence: src/runtime.c, src/match.c, src/dnn.c — expected mutex/rwlock/RCU/atomic; checked via grep` | 5 — concurrent data structures | R2 (+DA M2) | P1 | 1 day |
| R11 | Add a TCP/coding-interaction discussion: loss hiding from congestion control, relation to Coded TCP [26], bounding redundancy; cite PEP literature (RFC 3135, ETSI TR 101 984) | SC-11 | major | `text: §4.7.1 + [26] — interaction never discussed` | 5 — TCP over satellite | R3 | P1 | 2–3 days |
| R12 | Position UPF-level RLNC relative to satellite link-layer protection (DVB-S2X ACM/LDPC); disaggregate the 1–10% loss range into operational regimes and justify Table 4 parameters for the targeted regime | SC-12 | major | `absence: §2/§4.7/§6 — expected DVB-S2X ACM discussion; checked all subsections` | 5 — satellite link layer | R3 | P1 | 2–3 days |
| R13 | Add missing software-data-plane literature (NetBricks, ClickOS, P4-DPDK targets, eBPF/XDP classification, Sonata, FlowRadar, Marple) and a "DPDK NFV + ML" column in Table 1 | SC-13 | major | `absence: §2.1/§2.6/Table 1 — expected NetBricks, eBPF/XDP, Sonata, FlowRadar; checked §2.1–2.6, Table 1` | 4 — NFV frameworks | R2 | P1 | 3–4 days |

### Required Item Details

**R1: Correct all LOC claims**
- **Problem**: Table 5 deviates from `wc -l` by 18–87% for 5 of 9 modules (parser.c 187 vs ~350; alu.c 398 vs ~280; p4c.c 933 vs ~1,200; dnn.c 163 vs ~200; pipeline.c 368 vs ~250); "≈4,200 core LOC" vs actual sum ≈3,518.
- **Source**: EIC W1, R1 W3, DA M3 (three independent audits).
- **Requirement**: Re-count every module with a single documented method; update Table 5, abstract, §1.3 Pillar 3, §5.1; state which files are included in the core total.
- **Acceptance criteria**: Every LOC number in the manuscript matches the documented recount within the stated rounding, and the counting method is stated in §5.1.

**R2: Repair the performance narrative (PGO claim)**
- **Problem**: A 6.8× single-thread speedup attributed to PGO contradicts the Makefile's own "10–20% additional speedup" over `-O3 -flto`; the baseline's compiler flags are undisclosed.
- **Source**: R1 W1 (Critical).
- **Requirement**: State exact flags for both configurations; if the baseline was a debug/-O0 build, rename the rows ("unoptimized vs. PGO-optimized") and add an `-O3 -flto` intermediate row so the PGO contribution is isolated; re-run and re-report Table 8 accordingly.
- **Acceptance criteria**: Table 8 rows are reproducible from the Makefile as documented, and the PGO-attributed delta is consistent with the artifact's build system.

**R3: Add a non-neural baseline**
- **Problem**: Accuracy is reported only for the MLP on data drawn from the same generator used for training; no baseline exists (§5.7), so the MLP's marginal value is unknowable.
- **Source**: EIC W4, R1 W2, DA C2/M6; editorial note: a rule baseline existed in the prior revision and was removed.
- **Requirement**: Implement a threshold-rule classifier (and optionally one classical ML model) on the identical split; report accuracy, per-class recall, and per-packet latency.
- **Acceptance criteria**: §5 reports baseline vs. MLP numbers on the same workload, with an explicit statement of the MLP's marginal contribution.

**R4: Confusion matrix and error-direction analysis**
- **Problem**: 36 misclassifications cannot be assessed for operational safety (e.g., VoIP→Bulk removes both EF marking and coding protection).
- **Source**: EIC W6.
- **Requirement**: Record a 4×4 matrix in the demo; report precision/macro-F1; discuss the dominant confusion pairs against the policies in Tables 3–4.
- **Acceptance criteria**: The matrix appears in §5.3 (or companion table) and the operational impact of the two worst confusion pairs is explicitly discussed.

**R5: Variance across seeds**
- **Problem**: 99.1% is a single-seed point estimate; Table 12's ">99%" acceptance criterion is statistically meaningless on one realization.
- **Source**: R1 W4.
- **Requirement**: 5–10 seeds (or k-fold); mean ± std for overall and per-class accuracy.
- **Acceptance criteria**: §5.3 reports variance and states whether the >99% criterion holds across seeds.

**R6: Include the calibrated-distribution results**
- **Problem**: `real_traffic_test_report.md` (97.0% overall; Video drops to 91.6%) directly addresses the paper's primary limitation but is absent from the manuscript.
- **Source**: EIC W3.
- **Requirement**: Add a subsection presenting these results and the 99.1→97.0 delta analysis, or state why they are excluded.
- **Acceptance criteria**: §5 contains the calibrated-distribution evaluation or a documented reason for exclusion.

**R7: Reconcile satellite framing with evidence**
- **Problem**: No experiment depends on any satellite property; replacing "satellite" with "edge gateway" would change nothing in §§4–5.
- **Source**: EIC W2, R3 W7, DA M5.
- **Requirement**: Either add one satellite-dependent experiment (recommended: netem-based GEO/LEO delay/loss profiles driving the pipeline and coding path) or restructure framing/title accordingly.
- **Acceptance criteria**: At least one reported experiment is parameterized by satellite-link characteristics, or the title/abstract/introduction no longer present satellite behavior as an evaluated contribution.

**R8: GTP-U architectural gap**
- **Problem**: At N3/N9, packets are GTP-U encapsulated; the parser stops at outer UDP 2152 and never reaches inner TLS — the classifier is inoperative at the deployment point the paper claims.
- **Source**: R3 W1 (Critical).
- **Requirement**: Implement GTP-U-aware parsing and re-run key tests, or explicitly place the classifier post-decapsulation (N6) and analyze the placement consequences for the satellite-gateway architecture.
- **Acceptance criteria**: The manuscript states precisely where the classifier operates in a real 5G user plane, and either demonstrates GTP-U handling or justifies the chosen placement.

**R9: Reframe Application B**
- **Problem**: Coding-profile selection and repair counting run in `nc_demo.c` after `rmt_process()` returns; no repair packet is constructed or emitted in the pipeline, yet §1.3 presents A and B as co-equal "application instantiations".
- **Source**: R2 W3, DA C1 (validated at Major level).
- **Requirement**: Rewrite the Application B framing as a connected validation harness (the paper's own §4.7.4 language is the model), or implement a minimal in-pipeline NC extern.
- **Acceptance criteria**: §1.3/§1.4 no longer imply pipeline-resident coding; Contribution 3 matches the implemented boundary.

**R10: Runtime-update concurrency disclosure**
- **Problem**: `rmt_load_entries()` and `rmt_dnn_load()` mutate shared trie/weight state with no synchronization while Challenge 3 claims runtime reconfigurability.
- **Source**: R2 W2, DA M2.
- **Requirement**: Add an explicit limitation paragraph and specify the production update model (stop-the-world, shadow-table swap, or RCU).
- **Acceptance criteria**: §6 states the concurrency limitation and the intended mechanism for production updates.

**R11: TCP/coding interaction**
- **Problem**: UPF-level systematic RLNC hides losses from TCP congestion control on shared satellite links without endpoint coordination; [26] is cited but never engaged.
- **Source**: R3 W2.
- **Requirement**: Discussion covering loss-hiding effects, relation to Coded TCP, redundancy bounding, and PEP literature.
- **Acceptance criteria**: §4.7 or §6 contains a dedicated interaction analysis with the cited mechanisms.

**R12: Link-layer FEC positioning**
- **Problem**: DVB-S2X ACM already provides adaptive link-layer FEC; UPF-level RLNC on top is either redundant or must target residual burst losses — the paper assumes neither regime explicitly.
- **Source**: R3 W3 (+W4 on loss-regime disaggregation).
- **Requirement**: Position Table 4's parameters relative to the link-layer stack; disaggregate the 1–10% loss claim by regime (clear-sky / rain fade / handover) with citations (3GPP TR 38.821, DVB-S2X).
- **Acceptance criteria**: The coding rationale names the targeted residual-loss regime and cites the link-layer standard.

**R13: Literature and Table 1 completeness**
- **Problem**: Software data-plane frameworks (NetBricks, ClickOS, P4-DPDK, eBPF/XDP) and in-network telemetry (Sonata, FlowRadar, Marple) are absent; Table 1 omits the most directly comparable category (DPDK NFV + ML).
- **Source**: R2 W6, W9.
- **Requirement**: Add the missing categories to §2 and a "DPDK NFV + ML" column to Table 1, stating EdgeNet-CoM's differentiator (P4-inspired programmability + integrated match-action) against it.
- **Acceptance criteria**: §2 covers software programmable data planes; Table 1 includes the new column.

## Suggested Revisions (Should Fix)

| # | Revision Item | Sub-Claim(s) | Severity | Evidence Anchor | Confidence | Source | Priority | Expected Improvement |
|---|--------------|--------------|----------|-----------------|------------|--------|----------|---------------------|
| S1 | VoIP coding-delay analysis: k=4 buffering ≈ 60–80 ms at 50 pps vs ITU-T G.114 150 ms budget + satellite propagation; consider k=2/3 with higher per-generation redundancy | SC-14 | major | `table: Table 4 — VoIP k=4, rationale "Max protection, low generation"` | 4 — VoIP delay budgets | R3 | P2 | Resolves EF-marking vs coding-delay contradiction |
| S2 | Reframe 12.3% as validation of the coupling mechanism ("classifier drives the configured profile; weighted-average repair ratio is the expected 12.3%"); echo in abstract | SC-15 | minor | `table: Table 16 — 1234/10000 = 12.3%` | 4/5 — coding evaluation | EIC + DA C3 | P2 | Prevents misreading arithmetic as measured benefit |
| S3 | Scaling presentation: cap headline scaling at 8 workers (= physical cores); move 16-worker rows to an oversubscription-sensitivity note; label throughput as "in-memory processing rate"; state explicitly that 2.75 Mpps ≈ 2.8 Gbps covers only the low end of the 1–10 Gbps motivating range | SC-16 | minor | `table: Table 9 — 16 workers on 8 vCPUs, 16% efficiency` | 5/4 — multicore benchmarking | R1 (+DA M1) | P2 | Removes implicit scalability promise |
| S4 | Technical corrections: Algorithm 1 line 9 vs dnn.c int64_t hidden storage; Definition 4 saturation bounds (±2,147,483,000 vs "integer limits"); Theorem 1 single-dispatcher assumption as a formal premise; fix dnn.c argmax `best_v = 0` initialization (all-negative logits → always class 0) | SC-17 | minor | `dataset: src/dnn.c line 134 "int64_t best_v = 0;"` | 5/4 — fixed-point arithmetic | R2 (+DA m3) | P2 | Aligns formal description with implementation; fixes latent bug |
| S5 | Clarify DSCP marking layer: prototype marks the parsed (inner) IPv4 header; a GTP-U deployment marks the outer tunnel header under SMF/QER policy; sketch the QFI mapping path | SC-18 | minor | `text: §6.2 "DSCP and QFI are distinct fields"` | 4/5 — 3GPP QoS | R2 + R3 | P2 | Removes 5G-reader ambiguity |
| S6 | Substantiate the "≈40% per-stage speedup" for computed goto with an A/B measurement, or soften to qualitative | SC-19 | minor | `text: §4.4 "reduces per-stage execution time by approximately 40%"` | 4 — compiler effects | R2 (+DA m2) | P2 | Evidence for a quantitative claim |
| S7 | Tighten the challenge/pillar structure: separate generic data-plane requirements from satellite-specific ones; avoid near-verbatim Challenge 5 ↔ Pillar 5 duplication | SC-20 | minor | `text: §1.2 Challenge 5 vs §1.3 Pillar 5` | 4 — structural analysis | EIC (+DA M4) | P2 | More honest contribution shape |
| S8 | Add inline qualifiers to abstract headline numbers ("synthetic class-conditional traffic", "in-memory, no NIC I/O") | SC-21 | minor | `text: Abstract — unqualified headline numbers` | 4 — reporting standards | EIC | P2 | Protects skimming readers |
| S9 | Separate RNG streams (or re-seed) between training and test generation | SC-22 | minor | `dataset: src/enc_classify_demo.c — train/test share one rand() stream` | 4 — reproducible ML evaluation | R1 | P3 | Methodological hygiene |
| S10 | Qualify constrained-hardware motivation (benchmarks ran on a 120 W Xeon); remove or soften space-grade implications absent energy measurements | SC-23 | minor | `text: §6.1 "No energy-efficiency or radiation-tolerance conclusion can be drawn"` | 4 — on-board constraints | R3 | P3 | Keeps motivation commensurate with evidence |

## Revision Checklist

### Priority 1 — Structural Revisions (Estimated total effort: 5–7 weeks)
- [ ] R1: Re-measure and correct all LOC claims (Table 5, abstract, §5.1)
- [ ] R2: Disclose baseline/PGO compiler flags; add intermediate build row; repair Table 8 narrative
- [ ] R3: Add threshold-rule (and optionally classical ML) baseline on the identical workload
- [ ] R4: Add confusion matrix + precision/macro-F1 + operational-safety analysis
- [ ] R5: Report 5–10 seed variance (mean ± std) for accuracy
- [ ] R6: Integrate real_traffic_test_report.md results (97.0%) or explain exclusion
- [ ] R7: Add one satellite-dependent experiment OR restructure the satellite framing/title
- [ ] R8: GTP-U-aware parser OR explicit N6 placement with architectural analysis
- [ ] R9: Reframe Application B / Contribution 3 as a connected validation harness
- [ ] R10: Disclose runtime-update thread-safety limits; specify production update model
- [ ] R11: TCP/coding interaction discussion (loss hiding, Coded TCP, PEP literature)
- [ ] R12: DVB-S2X ACM positioning + loss-regime disaggregation for Table 4
- [ ] R13: Add software-data-plane literature; add "DPDK NFV + ML" column to Table 1

### Priority 2 — Content Supplementation (Estimated total effort: 1–2 weeks)
- [ ] S1: VoIP generation-delay analysis vs G.114 budget
- [ ] S2: Reframe 12.3% repair-ratio result as mechanism validation
- [ ] S3: Scaling presentation fixes (8-worker headline, oversubscription note, in-memory label, range statement)
- [ ] S4: Algorithm 1 / Definition 4 / Theorem 1 corrections; fix dnn.c argmax initialization
- [ ] S5: DSCP inner/outer + QFI mapping clarification
- [ ] S6: Measured A/B for computed-goto claim or soften
- [ ] S7: Tighten challenge/pillar structure
- [ ] S8: Inline qualifiers in abstract

### Priority 3 — Text and Formatting (Estimated total effort: 2–3 days)
- [ ] Replace ASCII Figures 1–3 with vector graphics (EIC, R1, R2, R3 all noted)
- [ ] Reference fixes: [13] missing volume/pages; [3]/[38] add access dates; verify [37] workshop name
- [ ] Add 3GPP TS 29.281 (GTP-U), TS 29.244 (PFCP), TS 23.501, and DVB-S2X (ETSI EN 302 307-2) references where the revised text invokes them
- [ ] Table 6: note that tls.type=23 is a gating constant, not a discriminative feature
- [ ] Move/preview the T1–T7 test design (§5.9) before the results it categorizes
- [ ] Wording: "fundamentally differ" (§1.1), "aerial" (§1.1), w_d^(k) notation (§3.2)
- [ ] §3.2: mark the optimization objective prominently as not implemented by the prototype
- [ ] RNG stream separation (S9) and constrained-hardware wording (S10)

### Total Estimated Effort
- **Major Revision**: 6–8 weeks (dominated by R7/R8 if the experimental path is chosen; ~3–4 weeks if the reframing path is chosen for both)

## Revision Deadline

- **Recommended deadline**: 2026-09-29
- **Basis**: Major Revision — 6–8 weeks
- **Extension policy**: notify at least 1 week before the deadline

## Response Letter Instructions

Please respond to every item R1–R13 and S1–S10 point-by-point (R→A→C format: Response → Action → Change location), with all manuscript changes marked. The revised manuscript will undergo re-review.

## Closing

We encourage you to carefully consider the reviewers' comments and submit a substantially revised manuscript. The panel wishes to emphasize that this is a well-engineered, unusually honest feasibility study whose core architecture is sound and verified; the required revisions are about evidence (baselines, variance, measurement disclosure), framing (satellite claims, Application B, contribution shape), and two architectural preconditions (GTP-U, concurrency) — all of which are tractable within the revision window. Please note that the revised manuscript will undergo another round of review.

---

# Part 3: Reviewer Report Summaries

### EIC Report Summary
- Recommendation: Major Revision | Confidence: 4/5 | Weighted: 65
- Key point: legitimate TNSM-scoped contribution with exemplary scoping, but LOC inaccuracies, evaluationally empty satellite framing, an unused 97.0% calibrated-traffic result in the repo, no baseline, and a missing confusion matrix require substantial revision.

### Reviewer 1 (Methodology) Summary
- Recommendation: Major Revision | Confidence: 5/5 | Weighted: 64
- Key point: the PGO 6.8× claim contradicts the artifact's own Makefile (Critical); circular train/test generator with no baseline; single-split statistics; Table 5 LOC contradicted by wc -l; otherwise verified reproducibility (seed 42, ranges, agreement test).

### Reviewer 2 (Domain) Summary
- Recommendation: Major Revision | Confidence: 5/5 | Weighted: 72 (Major recommended despite Minor-band score)
- Key point: architectural claims largely artifact-verified (VLIW two-phase commit, computed goto, P4-subset compiler, Q16.16 math); but runtime updates lack thread safety, Application B is a harness not a pipeline extern, and software-data-plane literature (NetBricks/eBPF-XDP/telemetry) plus a Table 1 DPDK+ML column are missing.

### Reviewer 3 (Perspective) Summary
- Recommendation: Major Revision | Confidence: 5/5 | Weighted: 67 (Major recommended)
- Key point: the parser cannot see GTP-U-encapsulated traffic, so the classifier is inoperative at any real N3/N9 deployment point (Critical); TCP/coding and DVB-S2X ACM interactions unaddressed; VoIP k=4 buffering conflicts with the EF delay budget; no satellite-specific mechanism is evaluated.

### Devil's Advocate Summary
- Recommendation (advisory): Major Revision | Confidence: 4/5
- Key point: of the "two coupled decisions," only DSCP marking is pipeline-resident (C1 — adjudicated: disclosure exists; reframing still required); 99.1% is generator separability without any baseline (C2 — validated); 12.3% is an arithmetic identity (C3 — rejected as Critical, reframing kept). Code audit additionally found the argmax `best_v = 0` latent bug and confirmed all Table 16/17 arithmetic.

---

# Appendices: Full Independent Reviewer Reports

The five reports below were produced independently (no cross-referencing) with read-only access to the manuscript and this artifact repository.


---

# Peer Review Report

## Manuscript Information
- **Title**: EdgeNet-CoM: Service-Aware In-Network Computing for Satellite User Plane Functions
- **Manuscript ID**: TNSM-2026-XXXX (simulated)
- **Review Date**: 2026-08-04
- **Review Round**: Round 1

---

## Reviewer Information

### Reviewer Role *
Editor-in-Chief (EIC)

### Reviewer Identity *
Senior Editor, IEEE Transactions on Network and Service Management (TNSM). Background in NFV, 5G user-plane functions, and programmable network systems. 12 years of editorial experience. Two cited works ([6], [7]) appeared in TNSM.

### Review Focus *
Journal fit and significance for the TNSM readership; claim-evidence balance in abstract and introduction; whether headline numbers are honestly scoped relative to what Section 5 actually measures; positioning and framing coherence (especially the "satellite UPF" narrative); overall maturity, structure, and readability; adequacy of Section 6 limitations.

---

## Overall Assessment *

### Recommendation *
- [ ] **Accept** — Can be published directly, only minor formatting changes needed
- [ ] **Minor Revision** — Minor revisions needed, no re-review after revision
- [x] **Major Revision** — Substantial revisions needed, re-review required after revision
- [ ] **Reject** — Not suitable for publication in this journal

### Confidence Score *
**4** — Mostly within my area of expertise, high confidence. The paper's intersection of programmable data planes, NFV/UPF, and in-network ML inference falls squarely within my editorial domain. I am less confident in the satellite-link engineering aspects, but these are not central to the evaluation.

### Summary Assessment *
EdgeNet-CoM presents a software RMT pipeline that couples a Q16.16 fixed-point MLP classifier with downstream QoS marking, routing, and a coding-policy harness, targeting a prospective satellite UPF deployment. The core contribution—reusing one packet-level classification result for both route selection and coding-profile selection within a single software match-action pipeline—is a legitimate systems contribution that fits TNSM's scope on network and service management.

From an editorial standpoint, the paper exhibits an uncommon degree of honesty in scoping its claims: the abstract's final sentence, Section 1.3's caveats, and Section 6's limitations all explicitly bound what the evaluation demonstrates. The writing quality is strong, with precise terminology and careful qualification throughout. The artifact repository is well-structured and largely corroborates the paper's technical claims.

However, several issues prevent acceptance in the current form. First, Table 5's line-count claims are materially inaccurate when verified against the artifact—p4c.c is stated as ~1,200 LOC but is actually 933 lines, and parser.c is stated as ~350 but is actually 187 lines. Second, the satellite UPF framing, while motivationally useful, is not supported by any satellite-specific evaluation; no satellite link, 5G core integration, or even real NIC I/O is tested. Third, the repository contains a real-traffic classification test (real_traffic_test_report.md, 97.0% accuracy on Gaussian-calibrated distributions) that is not discussed in the paper—a missed opportunity that weakens the evaluation. Fourth, there is no comparison with any baseline system. These issues collectively require substantial revision.

I recommend Major Revision. The architectural contribution is sound and the engineering is solid, but the evaluation must be strengthened and factual inaccuracies corrected before the paper can meet TNSM's publication standards.

---

## Strengths *

### S1: Exemplary claim-scoping discipline
The paper consistently qualifies its headline results with explicit scope limitations. The abstract's final sentence states: "These results establish implementation feasibility; accuracy on operational traffic, integrated repair-packet emission, and performance with real NIC and 5G integration remain open validation tasks." Section 1.3 states: "The present evidence is limited to the synthetic data model in Section 5." Section 5.3 notes: "This behavior should not be extrapolated beyond the generator." This level of self-aware scoping is rare in systems papers and significantly reduces the risk of reader misinterpretation.
**Evidence Anchor**: `text: Abstract — "These results establish implementation feasibility; accuracy on operational traffic…remain open validation tasks"`

### S2: Verifiable, well-structured artifact repository
The artifact contains all core source modules (~7,800 LOC across 21 .c files), the P4 programs, table entries, benchmark scripts (bench_all.sh), and a comprehensive README. The DNN inference engine (dnn.c, 163 lines) faithfully implements Algorithm 1 with Q16.16 fixed-point arithmetic, 64-bit accumulators, and saturation guards. The network coding module (nc_demo.c, 849 lines) implements GF(2^8) with the AES polynomial and per-class coding parameters matching Table 4. The P4 program (enc_classify.p4) defines the four-stage pipeline described in the paper. The test suite (bench_all.sh) covers all seven test categories (T1–T7) listed in Table 12.
**Evidence Anchor**: `dataset: src/dnn.c — Q16.16 forward pass with int64_t accumulator matches Algorithm 1`

### S3: Coherent architectural contribution with clear design rationale
The unified parse-classify-act pipeline with a stateful DNN extern is a well-motivated architectural choice for software RMT platforms. The five-pillar design structure (unified pipeline, stateful extern, P4-inspired programmability, multi-queue scaling, service-aware actuation) maps cleanly to the four-layer architecture in Figure 1. The distinction between the implemented Mode A (DSCP marking + destination routing) and the proposed Mode B (class-aware multi-uplink mapping) is explicitly drawn in Section 4.6.2, preventing overclaiming.
**Evidence Anchor**: `text: §4.6.2 — "The design distinguishes the implemented mode from a proposed multi-uplink extension"`

### S4: Comprehensive and well-placed limitations section
Section 6.1 enumerates eight specific limitations (feature granularity, HTTP/2–3 multiplexing, synthetic evaluation scope, TCP-only coverage, adversarial robustness, first-packet behavior, energy characterization, no satellite integration), each with a clear explanation of why it matters. Section 6.2 covers deployment considerations (model lifecycle, graceful degradation, multi-tenancy, 5G UPF integration) that demonstrate awareness of production requirements. Section 6.3 addresses ethical and regulatory concerns around service-aware routing. This section is unusually thorough for a systems paper and well-positioned before the conclusion.
**Evidence Anchor**: `text: §6.1 — "No real satellite UPF integration validation…End-to-end experiments with a 5G core and representative links are required"`

### S5: Honest reporting of sub-linear scaling
Table 9 reports multi-core scaling efficiency declining from 100% (1 worker) to 16% (16 workers on 8 vCPUs), and the paper explicitly states: "Oversubscription, shared-cache traffic, queueing, and dispatcher overhead are plausible contributors, but the benchmark does not isolate them. Sixteen workers reach 2.75 Mpps, a 2.57x aggregate speedup over the single-worker baseline; no near-linear scaling claim is made." This is an honest presentation of results that many systems papers would be tempted to spin more favorably.
**Evidence Anchor**: `table: Table 9 — Efficiency drops from 100% to 16% at 16 workers; no near-linear scaling claim`

### S6: Correctness-first engineering with formal guarantee
Theorem 1 establishes per-flow ordering preservation under RSS hashing with SPSC queues, and the proof is straightforward and correct. The load-balancing results in Table 10 (Jain's Fairness Index 0.997) empirically validate the hash distribution quality. The end-to-end functional verification in Table 11 covers all eight scenarios including critical bypass cases (non-TLS, TLS handshake, unroutable destination). This attention to correctness is appropriate for a system targeting production network infrastructure.
**Evidence Anchor**: `table: Table 10 — Jain's Fairness Index 0.997, maximum deviation 1.3 percentage points`

---

## Weaknesses *

### W1: Material inaccuracies in Table 5 line-count claims
**Problem**: Table 5 reports approximate line counts for each source module that are materially incorrect when verified against the actual artifact. The most significant discrepancies are: p4c.c is stated as "~1,200" LOC but the actual file contains 933 lines (a 28.6% overstatement); parser.c is stated as "~350" LOC but actually contains 187 lines (an 87.2% overstatement); alu.c is stated as "~280" but actually contains 398 lines (a 42% understatement); pipeline.c is stated as "~250" but actually contains 368 lines (a 47.2% understatement). The paper's abstract and Section 5.1 claim "approximately 4,200 lines of core ISO C11," but the sum of the actual line counts for the nine modules in Table 5 is 3,518—an overstatement of approximately 19%.

**Evidence Anchor**: `dataset: Table 5 vs. wc -l of src/*.c — p4c.c stated ~1200, actual 933; parser.c stated ~350, actual 187`

**Why it matters**: LOC claims are verifiable factual assertions, not opinions. Inaccurate LOC counts undermine the reader's trust in other quantitative claims and raise questions about whether the reported numbers were actually measured or estimated. For a paper that positions its prototype's compactness as a contribution ("4,200-line ISO C11 prototype"), accurate module-level counts are essential.

**Suggestion**: Re-measure all module line counts using a consistent method (e.g., `wc -l` excluding blank lines and comments, or including them—state which). Update Table 5, the abstract, and Section 5.1 with corrected values. If the counting methodology matters (e.g., excluding comments), document it.

**Severity**: Major
**Confidence**: 5 — Direct measurement: `wc -l` on the source files is deterministic and unambiguous.

### W2: Satellite UPF framing is motivationally heavy but evaluationally empty
**Problem**: The paper invests substantial effort in Sections 1.1, 1.2, 2.5, 3.1, and 4.7.1 building a satellite-UPF-specific motivational case—discussing GEO/LEO/ISL path heterogeneity, satellite loss rates of 1–10%, constrained processing environments, and space-ground integrated networking. Yet the evaluation contains zero satellite-specific measurements: no satellite link emulation, no variable-delay path testing, no 5G core integration, no GTP-U encapsulation, no PFCP/N4 interface, and no real NIC I/O. The paper acknowledges this ("the prototype evaluated in this paper is a standalone data-plane implementation rather than a complete 3GPP-conformant UPF" in Section 1.1; "No real satellite UPF integration validation" in Section 6.1), but the sheer volume of satellite-specific motivational text creates an implicit promise that the evaluation does not fulfill.

**Evidence Anchor**: `absence: Section 5 — expected satellite-link-specific evaluation (path heterogeneity, variable delay, loss emulation); checked §5.1–5.12, all experiments use in-memory synthetic traffic on a single x86 host`

**Why it matters**: For TNSM readers—many of whom work on 5G core and NTN systems—the satellite UPF framing is the paper's primary differentiator from generic programmable-data-plane work. When the framing is not backed by domain-specific evaluation, it appears decorative rather than substantive. A reader who selects this paper based on its title and abstract may feel misled about the maturity of the satellite integration.

**Suggestion**: Either (a) add at least one satellite-link emulation experiment (e.g., netem-based delay/loss profiles for GEO/LEO paths, measuring classification accuracy and coding benefit under realistic loss), or (b) substantially reduce the satellite-specific motivational text and reposition the paper as a general-purpose in-network computing framework with satellite UPF as a motivating use case rather than a claimed contribution. Option (b) is more feasible for a revision.

**Severity**: Major
**Confidence**: 5 — Core expertise: NFV/5G UPF systems and evaluation methodology.

### W3: Unused real-traffic evaluation results in the artifact repository
**Problem**: The artifact repository contains `real_traffic_test_report.md`, which reports a classification test using Gaussian-calibrated distributions (rather than the uniform distributions in the paper) modeled after published real-traffic measurements (Aceto et al. TNSM 2019, FlowPic TNSM 2021). This test achieves 97.0% overall accuracy on 4,000 packets—2.1 percentage points below the synthetic result, as expected due to increased class-boundary overlap. The report also shows mean latency of 588 ns and throughput of 1.70 Mpps. These results directly address the paper's most significant acknowledged limitation ("Synthetic evaluation scope" in Section 6.1) but are not discussed anywhere in the manuscript.

**Evidence Anchor**: `dataset: real_traffic_test_report.md — 97.0% accuracy on Gaussian-calibrated distributions; not discussed in §5 or §6`

**Why it matters**: The paper's most vulnerable point is its reliance on deliberately separated uniform distributions, which makes the 99.1% accuracy claim easy to dismiss as an artifact of the data model. The existence of a more realistic evaluation in the repository that the authors chose not to include suggests either (a) the results were deemed insufficient, in which case the reasons should be discussed, or (b) they were overlooked during manuscript preparation. Either way, omitting these results weakens the paper's evidentiary basis and will likely be flagged by reviewers.

**Suggestion**: Include the real_traffic_test_report results in Section 5.3 or a new subsection (e.g., §5.3.1 "Evaluation with Calibrated Distributions"). Discuss the accuracy drop from 99.1% to 97.0%, analyze which classes are most affected (Video drops from 96.5% to 91.6%), and explain what this implies about the classifier's robustness. If the Gaussian distributions in gen_realistic_traffic.py are not sufficiently representative of operational traffic, state what additional validation is needed.

**Severity**: Major
**Confidence**: 5 — Direct observation: the file exists in the repository and is referenced by scripts/bench_real_traffic.sh.

### W4: No comparison with any baseline system
**Problem**: The paper explicitly declines to compare EdgeNet-CoM against any existing system. Section 5.7 states: "We do not report cross-paper accuracy or latency rankings because the studies use different traffic captures, label taxonomies, target hardware, and measurement boundaries." Table 1 compares "architectural scope rather than accuracy or latency." While the reasoning is partially valid (different datasets and hardware complicate direct comparison), the paper could have implemented at least one baseline on the same platform and workload—for example, a rule-based classifier using the same four features, or nDPI [38] running on the same synthetic traffic, or a floating-point MLP without quantization.

**Evidence Anchor**: `text: §5.7 — "The artifact compares fixed-point predictions with its floating-point model, but it does not contain a reproducible classical or production-tool baseline"`

**Why it matters**: Without any baseline comparison, the reader cannot assess whether EdgeNet-CoM's 99.1% accuracy and 933 ns latency are impressive, expected, or trivially achievable. A simple threshold-based classifier on the same well-separated uniform distributions might achieve comparable accuracy at a fraction of the computational cost, which would undermine the motivation for embedding a neural network. The absence of a baseline is the single largest gap in the paper's evidentiary structure.

**Suggestion**: Implement at least one comparison baseline on the same workload. The most informative would be: (a) a rule-based threshold classifier using the same four features, to establish whether the MLP adds value over simple heuristics on this data model; and (b) a flow-level classifier (e.g., first-N-packet majority vote) to quantify the benefit of per-packet vs. flow-level decision granularity. Report accuracy, latency, and throughput for each baseline alongside EdgeNet-CoM.

**Severity**: Major
**Confidence**: 4 — Core expertise: systems evaluation methodology; slight uncertainty about whether the authors considered and rejected specific baselines.

### W5: Five-challenge/five-pillar structure contains padding
**Problem**: The five-challenge structure in Section 1.2 is presented as satellite-UPF-specific requirements, but at least two challenges are generic data-plane requirements with no satellite-specific dimension. Challenge 1 ("Inline feature extraction under encryption") acknowledges this: "a constraint shared with terrestrial edge but more critical at the satellite UPF where DPI appliances are often impractical." Challenge 3 ("Framework programmability and runtime reconfigurability") is a standard requirement for any programmable data plane. The corresponding pillars (Pillar 1: Unified Pipeline, Pillar 3: P4-Inspired Programmability) describe well-known architectural patterns rather than novel contributions. This padding inflates the apparent scope of the contribution.

**Evidence Anchor**: `text: §1.2 Challenge 1 — "a constraint shared with terrestrial edge but more critical at the satellite UPF"`

**Why it matters**: A five-challenge/five-pillar structure implies five distinct contributions or design innovations. When some challenges are generic and their corresponding pillars describe established patterns, the structure overstates the paper's novelty. TNSM readers will recognize P4-inspired programmability and runtime table updates as standard features of programmable data planes, not novel contributions of this work.

**Suggestion**: Restructure to distinguish satellite-specific challenges from general data-plane requirements. One approach: present two general requirements (inline classification, programmability) as design context, then three satellite-specific challenges (heterogeneous path selection under encryption, elevated-loss-aware coding, constrained-environment deployment) as the actual contribution drivers. This would give a more honest three-pillar contribution structure.

**Severity**: Minor
**Confidence**: 4 — Core expertise: programmable data plane architectures and contribution assessment.

### W6: Missing confusion matrix prevents error analysis
**Problem**: Section 5.3 reports per-class recall (correct/generated) but does not record or report a confusion matrix. The paper acknowledges this: "The demo does not record a confusion matrix, so precision and macro-averaged F1 are not reported" (Table 7 caption) and "the reported run does not support claims about which incorrect class was selected or whether errors are operationally safe" (Section 5.3(c)). Without a confusion matrix, the reader cannot determine whether the 36 misclassified packets are benign (e.g., Video misclassified as Web, both receiving reasonable treatment) or dangerous (e.g., VoIP misclassified as Bulk, receiving no coding protection and no priority routing).

**Evidence Anchor**: `text: §5.3(c) — "the reported run does not support claims about which incorrect class was selected or whether errors are operationally safe"`

**Why it matters**: In a system that couples classification to routing and coding decisions, the cost of misclassification depends entirely on which classes are confused. Misclassifying VoIP as Bulk means latency-sensitive traffic gets no priority and no coding protection—a potentially service-breaking error. Misclassifying Bulk as Web is comparatively benign. Without error-direction data, the operational safety of the classifier cannot be assessed, which is critical for a system targeting production UPF deployment.

**Suggestion**: Add confusion matrix recording to the demo (a 4×4 integer matrix incremented at each classification decision) and report the full matrix in Table 7 or a companion table. Compute precision and macro-averaged F1. Analyze the operational impact of the most common confusion pairs given the routing and coding policies in Tables 3 and 4.

**Severity**: Major
**Confidence**: 5 — Core expertise: classification system evaluation and operational safety analysis.

### W7: Abstract headline numbers lack inline qualification
**Problem**: While the abstract's final sentence provides an overall scope limitation, the middle of the abstract presents headline numbers without inline qualification: "the fixed-point model attains 99.1% accuracy on one 4,000-sample split," "933 ns single-thread service time," "2.75 Mpps with 16 workers, a 2.57x speedup," "projected 12.3% repair ratio versus 25%." A reader skimming the abstract will absorb these numbers without the scoping context that appears only in the final sentence. The 99.1% figure is particularly prominent and potentially misleading, as it reflects deliberately separated uniform distributions rather than operational traffic.

**Evidence Anchor**: `text: Abstract — "the fixed-point model attains 99.1% accuracy on one 4,000-sample split"`

**Why it matters**: Abstracts are the primary discovery mechanism for papers. Many readers will encounter only the abstract (via search results, conference proceedings, or citation managers). Presenting unqualified headline numbers in the abstract, even with a scoping sentence at the end, risks creating an inaccurate impression of the paper's validated performance. For TNSM, where practitioners may cite these numbers in proposals or design documents, this risk is non-trivial.

**Suggestion**: Add brief inline qualifiers to the headline numbers in the abstract. For example: "attains 99.1% accuracy on one 4,000-sample split of class-conditional synthetic traffic" and "933 ns single-thread service time (in-memory, no NIC I/O)." The full qualifications already exist in the body text; they need only be echoed in the abstract.

**Severity**: Minor
**Confidence**: 4 — Adjacent field: applying general standards for abstract reporting in systems venues.

### W8: The "12.3% repair ratio" is a policy accounting result, not a measured coding benefit
**Problem**: The abstract states: "the configured policy yields a projected 12.3% repair ratio versus 25% for the selected uniform baseline." Table 16 and Section 5.10.4 clarify that this is a deterministic consequence of the configured class profiles and the inferred class distribution: VoIP gets 25% redundancy, Video gets 12.5%, Web gets 12.5%, and Bulk gets 0%. The 12.3% figure is the weighted average given the test's class mix. It does not measure actual loss recovery, bandwidth savings on a real link, or any comparison with adaptive coding. The word "projected" in the abstract is accurate but easy to miss.

**Evidence Anchor**: `text: §5.10.4 — "The 12.3% mixed repair ratio is a deterministic consequence of the configured class profiles and inferred class mix; it does not establish application quality or loss recovery on a live path"`

**Why it matters**: The 12.3% vs. 25% comparison is one of the paper's four headline results, featured in the abstract and conclusion. While the paper is honest about its nature (the word "projected" appears, and Section 5.10.4 provides full context), the prominence of this number in the abstract may lead readers to interpret it as a measured benefit rather than a policy-accounting calculation. Any class-differentiated coding policy would produce a similar weighted average; the result demonstrates that the classifier can drive the policy, not that the policy is optimal.

**Suggestion**: Reframe the 12.3% result more precisely as "the classifier correctly drives the configured coding profile for each packet, yielding the expected weighted-average repair ratio of 12.3%." In the abstract, consider replacing "projected 12.3% repair ratio versus 25%" with language that emphasizes the coupling mechanism rather than the arithmetic outcome.

**Severity**: Minor
**Confidence**: 4 — Core expertise: network coding evaluation methodology.

---

## Detailed Comments *

### Title & Abstract
- The title accurately identifies the system (EdgeNet-CoM), the approach (service-aware in-network computing), and the target application (satellite UPF). However, given that no satellite-specific evaluation is presented, "for Satellite User Plane Functions" may set expectations that the paper cannot meet. Consider "toward Satellite User Plane Functions" or "with Application to Satellite User Plane Functions."
- The abstract is well-structured with clear progression from problem to approach to results to limitations. The final scoping sentence is commendable. The headline numbers should carry brief inline qualifiers (see W7).
- The 99.1% accuracy figure dominates the abstract but is measured on deliberately separated synthetic data. This needs more prominent qualification.

### Introduction
- Section 1.1 provides strong motivation for the satellite UPF deployment context, with specific technical details about GEO/LEO/ISL path characteristics. However, this detail creates an expectation of satellite-specific evaluation that is not fulfilled (see W2).
- The five-challenge structure (Section 1.2) is logically organized but includes padding (see W5). Challenges 1 and 3 are generic data-plane requirements.
- Section 1.3's overview is clear and well-illustrated. The distinction between Application A (service-aware forwarding) and Application B (service-differentiated coding) is effective.
- The contributions list (Section 1.4) is honest and well-scoped. Contribution 4 explicitly states: "The results demonstrate feasibility on controlled synthetic traffic; they do not establish classification accuracy on operational traffic or full 5G UPF conformance."

### Literature Review / Theoretical Framework
- Section 2 provides comprehensive coverage of in-network computing, encrypted traffic classification, network coding, and satellite edge computing. The 38 references are predominantly peer-reviewed and include key works from TNSM ([6], [7]), SIGCOMM, and IEEE Transactions.
- Table 1 (architectural positioning) is a useful contribution but explicitly compares scope rather than performance. This is honest but limits the paper's ability to demonstrate advancement over the state of the art.
- The discussion of nDPI [38] in Section 2.2 is appropriate, and the paper correctly declines to claim a direct comparison. However, the absence of any empirical comparison (even on the same synthetic workload) weakens the positioning.

### Methodology / Research Design
- The system model (Section 3) is formally defined with six definitions and a joint optimization objective. The formalization is clear and provides a useful reference framework, though the optimization objective (Section 3.2) defines "a policy target rather than an optimizer implemented by the prototype."
- The synthetic feature model (Section 3.4, Table 2) is transparent about its limitations: "These ranges define the synthetic generator used in the prototype study; they should not be interpreted as universal application invariants." This is commendable.
- The evaluation methodology (Section 5.2) specifies hardware, software, dataset split, and metrics. However, the synthetic generator uses `rand()` with seed 42 and discrete uniform distributions—this is a very simple data model that does not capture temporal correlations, protocol diversity, or multiplexing effects.

### Results / Findings
- Table 7 (classification accuracy) reports 99.1% overall with per-class recall. The results are clearly presented, and the paper correctly identifies that VoIP and Bulk achieve perfect recall due to well-separated ranges. The absence of a confusion matrix is a significant gap (see W6).
- Table 8 (processing cost) provides a clear progression from unoptimized (6,300 ns) to PGO-optimized (933 ns). The benchmark methodology is described but does not decompose per-stage costs.
- Table 9 (multi-core scaling) honestly reports sub-linear scaling. The 2.57x at 16 workers on 8 vCPUs is a real result, and the paper does not overclaim.
- Tables 13–17 (network coding evaluation) verify GF(2^8) arithmetic, encode/decode correctness, coding throughput, and joint decision-path behavior. The coding evaluation is thorough within its scope (standalone module, in-memory).
- Table 18 (consolidated results) provides a useful summary with explicit scope notes for each metric.

### Discussion
- Section 6 (Limitations and Deployment Considerations) is one of the paper's strongest sections. It is unusually comprehensive and honest. The eight limitations in Section 6.1 are specific and actionable. The deployment considerations in Section 6.2 demonstrate awareness of production requirements.
- Section 6.3 (Ethical and Regulatory Considerations) appropriately addresses net neutrality and fairness concerns.
- Section 6.4 (Future Work) identifies six directions, all of which are well-motivated by the limitations discussed above.

### Conclusion
- Section 7 accurately summarizes the paper's contributions and limitations. The final sentence correctly identifies the next validation step: "a common captured-traffic benchmark followed by integration with a real packet-I/O path and 5G core policy interface."
- The conclusion does not over-infer beyond the evidence presented.

### References
- 38 references, predominantly peer-reviewed venues (IEEE Transactions, ACM SIGCOMM, USENIX NSDI). Key works in programmable data planes ([4], [10]), encrypted traffic classification ([5]–[9]), network coding ([23]–[27]), and satellite NTN ([28]–[33]) are covered.
- Two references ([6], [7]) are from TNSM, supporting the paper's venue fit.
- Reference [13] (IIsy) is cited as "IEEE Trans. on Networking, 2024" without volume or page numbers—this may need updating if the paper has since been published with full bibliographic details.
- The reference to Google Transparency Report [3] is appropriate for the encryption-prevalence claim.

---

## Questions for Authors *

1. **Line-count discrepancies in Table 5**: The actual source file line counts differ materially from Table 5's reported values (e.g., p4c.c: 933 vs. ~1,200; parser.c: 187 vs. ~350). What counting methodology was used? Were blank lines, comments, or preprocessor directives included or excluded? Can the authors provide a reproducible counting method (e.g., `cloc` or `sloccount` with specific options) and update all LOC claims accordingly?

2. **Unused real-traffic evaluation**: The repository contains `real_traffic_test_report.md` showing 97.0% accuracy on Gaussian-calibrated distributions, with per-class results (Web 96.3%, Video 91.6%, VoIP 100%, Bulk 100%). Why were these results not included in the paper? Are there concerns about the realism of the Gaussian calibration in `gen_realistic_traffic.py`, or was this simply an oversight? How do the authors interpret the 2.1pp accuracy drop and the Video class's decline from 96.5% to 91.6%?

3. **Baseline comparison feasibility**: The paper declines cross-system comparison due to different datasets and hardware. However, the same artifact could run a simple rule-based classifier (e.g., threshold rules on ipv4.len and tcp.window) on the identical synthetic workload. Would the authors be willing to implement and report at least one such baseline in the revision, to help readers contextualize the MLP's accuracy and latency relative to simpler approaches?

4. **Operational impact of misclassification**: Given that the classifier drives both DSCP marking and coding-profile selection, what is the operational consequence of the 36 misclassifications in the 4,000-sample test? Specifically, how many VoIP packets (which receive EF marking and 25% RLNC redundancy) were misclassified as Bulk (which receives AF11 and no coding)? A confusion matrix would directly answer this question and is essential for assessing operational safety.

---

## Minor Issues

### Language / Grammar
- Section 1.1, paragraph 2: "Satellite UPF edge nodes differ fundamentally from terrestrial edge nodes in three dimensions." The word "fundamentally" is slightly strong given that the three dimensions listed (heterogeneous connectivity, elevated loss, constrained processing) also apply to some terrestrial edge scenarios (e.g., multi-homing, wireless loss, embedded processors). Consider "differ significantly" or "exhibit pronounced differences."
- Section 3.2: The utility function U(c_k, p_m, γ_k) uses w_d^(k), w_b^(k), w_l^(k) notation. The superscript (k) is clear from context but could be confused with exponentiation in some renderings. Consider using subscript notation w_{d,k} or a parenthetical clarification.

### Citation Format
- Reference [13] (IIsy): Missing volume, issue, and page numbers. Verify whether the final published version is available.
- Reference [37] (Zhang et al., P4-based satellite routing): Listed as "ACM SIGCOMM Workshop on Satellite and Space Communications (SatCom), 2024." Verify the workshop name—ACM SIGCOMM 2024 workshops should be cross-checked for exact naming.

### Figures and Tables
- Figure 1 is rendered as ASCII art. For the camera-ready version, a proper diagram (TikZ, draw.io, or similar) would significantly improve readability and professional appearance.
- Figure 2 (parser state transition graph) is also ASCII art. Same recommendation.
- Figure 3 (classification-coding integration) is ASCII art. Same recommendation.
- Table 6 could benefit from a note explaining that tls.type = 23 is constant across all classes, making it a gating condition rather than a discriminative feature.

### Layout
- Section 5.9 (Test Scenario Design, Table 12) introduces the T1–T7 test categories after Sections 5.3–5.8 have already presented results organized differently. This creates a mild structural confusion: the reader encounters results before the test-design framework that categorizes them. Consider moving the test-scenario overview to Section 5.2 (Experimental Setup) or restructuring Section 5 to follow the T1–T7 organization.

---

## Dimension Scores *

| Dimension | Score (0-100) | Descriptor | Notes |
|-----------|--------------|------------|-------|
| Originality (20%) | 68 | Adequate | The unified parse-classify-act pipeline with fixed-point DNN and dual downstream action (routing + coding) is a real contribution, but the individual components (RMT pipeline, quantized MLP, DSCP marking, RLNC coding) are well-known. The novelty lies in their integration, which is incremental. The satellite UPF framing adds motivational context but not evaluated novelty. |
| Methodological Rigor (25%) | 55 | Weak | The evaluation is transparent and reproducible within its scope, but the scope is narrow: synthetic-only traffic, no comparison baseline, no NIC I/O, no satellite-specific testing, missing confusion matrix, no confidence intervals. The LOC inaccuracies in Table 5 are a factual rigor issue. The paper's honest scoping partially mitigates these gaps but does not eliminate them. |
| Evidence Sufficiency (25%) | 58 | Weak | 38 references with good coverage of relevant literature. Claims are supported within their stated scope. However, the evaluation evidence is narrow (one synthetic generator, one hardware platform, no baseline comparison). The unused real_traffic_test_report.md represents available but unreported evidence. The architectural positioning in Table 1 is qualitative rather than quantitative. |
| Argument Coherence (15%) | 72 | Adequate | The paper follows a clear logical structure: motivation → challenges → design → evaluation → limitations. The five-pillar mapping is coherent. However, the satellite UPF framing creates a disconnect between motivational promises and evaluative delivery. The five-challenge structure contains padding (Challenges 1 and 3 are generic). The overall argument is persuasive but could be tightened. |
| Writing Quality (15%) | 82 | Strong | Professional academic prose with precise terminology and careful qualification. Honest scoping language throughout. Well-organized sections with clear transitions. Minor issues with ASCII-art figures and occasional strong claims ("fundamentally differ"). The writing quality is a genuine strength of this paper. |
| Literature Integration (optional) | 72 | Adequate | Good coverage of programmable data planes, encrypted traffic classification, network coding, and satellite NTN. Table 1 provides useful architectural positioning. However, the explicit decision not to compare quantitatively with any cited system limits the integration depth. |
| Significance & Impact (optional) | 60 | Adequate | The paper addresses a relevant problem (service-aware forwarding at edge nodes) with a plausible approach. The satellite UPF context is timely given 3GPP NTN standardization. However, the standalone prototype with synthetic-only evaluation limits immediate practical impact. The contribution is more architectural than operational. |
| **Weighted Average** | **65** | **Major Revision** | Weighted: 68×0.20 + 55×0.25 + 58×0.25 + 72×0.15 + 82×0.15 = 13.6 + 13.75 + 14.5 + 10.8 + 12.3 = **64.95** → rounded to 65, borderline Major/Minor. Given the significance of evaluation gaps (W2–W4, W6), the editorial judgment tilts toward Major Revision. |

---

# Peer Review Report

## Manuscript Information
- **Title**: EdgeNet-CoM: Service-Aware In-Network Computing for Satellite User Plane Functions
- **Manuscript ID**: (not assigned)
- **Review Date**: 2026-08-04
- **Review Round**: Round 1

---

## Reviewer Information

### Reviewer Role *
Peer Reviewer 1 (Methodology)

### Reviewer Identity *
Expert in network systems measurement and experimental methodology: software packet-processing performance engineering (DPDK-style run-to-completion pipelines, SPSC rings, core pinning, PGO) and evaluation methodology for ML-based traffic classification. Author of benchmarking best-practice studies; particularly sensitive to unsound measurement, circular evaluation, and unsupported numeric claims.

### Review Focus *
This review scrutinizes the experimental methodology: classification evaluation design (synthetic generator circularity, single-split reporting, absence of baselines), statistical reporting quality, performance measurement soundness (PGO plausibility, oversubscription, throughput arithmetic), internal numeric consistency across tables and against the artifact, and reproducibility of the coding-module evaluation.

---

## Overall Assessment *

### Recommendation *
- [x] **Major Revision** — Substantial revisions needed, re-review required after revision

### Confidence Score *
**5** — The manuscript's methodology is squarely within my core expertise (network systems benchmarking, ML evaluation for traffic classification, fixed-point inference measurement). I have high confidence in every finding below, each grounded in direct artifact inspection.

### Summary Assessment *
EdgeNet-CoM presents a software RMT pipeline that couples a Q16.16 MLP classifier with QoS marking, routing, and a network-coding harness, evaluated entirely on synthetic class-conditional traffic. The prototype is a substantial engineering artifact, and the paper is commendably transparent about its synthetic-data scope and the absence of real-traffic validation. From a methodology standpoint, however, several findings materially weaken confidence in the reported numbers. The classification evaluation is circular (the same generator trains and tests the model, with no external baseline), and accuracy is reported as a single point estimate without variance. The PGO speedup claim (6.8×) is not credible as stated: the Makefile documents PGO as yielding "10–20% additional speedup" over the already -O3 -flto baseline, yet the paper attributes a ~6.3 µs to 933 ns improvement to PGO without disclosing what the two configurations actually differ in. Table 5 LOC claims are materially inaccurate for several modules (parser.c overstated by ~87%, p4c.c overstated by ~29%). The multi-worker scaling experiments oversubscribe an 8-vCPU host with up to 16 workers without adequate framing. On the positive side, the seed-42 generator, train/test sizes, quantization agreement test, and bench_all.sh script are all verified as real and reproducible, and the paper's limitation disclosures are unusually thorough. I recommend major revision to correct the PGO claim, fix the LOC table, add variance estimates and at least one non-neural baseline, and reframe the oversubscription results.

---

## Strengths *

### S1: Seed-42 deterministic reproducibility verified in artifact
The synthetic generator, training loop, and test evaluation are all seeded from a single `srand(42)` call at line 261 of `enc_classify_demo.c`. The model is retrained at demo startup rather than distributed as a pre-trained weight file, ensuring that any reviewer who runs the artifact obtains the same numerical results. The `bench_all.sh` script orchestrates the complete build-test-report pipeline.
**Evidence Anchor**: [`dataset: src/enc_classify_demo.c line 261, "srand(42)"`]

### S2: Generator ranges in Table 6 exactly match the implementation
Every range stated in Table 6 (e.g., Web: U[200,1460], U[16384,45000], U[100,1400]; VoIP: U[60,200], U[4096,16384], U[20,160]) is reproduced verbatim in `gen_sample()` at lines 62-94 of `enc_classify_demo.c`. The paper's description and the code are in exact correspondence.
**Evidence Anchor**: [`dataset: src/enc_classify_demo.c lines 62-94 vs Table 6`]

### S3: Quantization agreement test is genuine and correctly scoped
The 2,000-sample float-vs-Q16.16 agreement test (lines 289-303) runs the same input through both `forwardf()` (float) and `rmt_dnn_infer()` (fixed-point) and counts exact prediction matches. The threshold check (`agree >= nq * 95 / 100`) is a meaningful guard. The paper explicitly disclaims this as a general zero-loss guarantee.
**Evidence Anchor**: [`dataset: src/enc_classify_demo.c lines 289-303`]

### S4: Unusually thorough limitation disclosures
Section 6.1 enumerates nine distinct limitations including synthetic evaluation scope, absence of confidence intervals, no classical baseline, TCP-only coverage, adversarial fragility, HTTP/2 and QUIC exclusion, energy unmeasured, no UPF integration. Each is stated precisely without hedging. This level of self-criticism is rare in systems papers and significantly aids the reader's calibration of the results.
**Evidence Anchor**: [`text: §6.1 "No confidence intervals, significance tests, or reproducible non-neural baseline are reported"`]

### S5: Table 16 repair-decision arithmetic is internally consistent
The four repair counts (Web 312, Video 296, VoIP 626, Bulk 0) sum to exactly 1,234, matching the stated 12.3%. The per-class percentages (12.5%, 12.5%, 25.0%, 0%) are arithmetically correct against the inferred-class distribution (2,499 + 2,369 + 2,505 + 2,627 = 10,000). The floor(10000/k) generation counts in Table 17 are also exact.
**Evidence Anchor**: [`table: Table 16 — repair decision sum 312+296+626+0 = 1234`]

### S6: bench_all.sh faithfully implements the T1-T7 test categories
The script builds with `-O3 -flto -march=native`, runs `rmt_tests` for functional correctness (T1), `enc_classify_demo` for classification (T2), bulk and batch latency (T3), parallel and multi-queue scaling (T4-T5), `nc_demo` for coding (T6), and extracts joint-path metrics (T7). The mapping from script to paper tables is traceable.
**Evidence Anchor**: [`dataset: scripts/bench_all.sh lines 1-241`]

---

## Weaknesses *

### W1: PGO speedup claim of 6.8× is not credible and not adequately explained
**Problem**: Table 8 attributes a reduction from ~6,300 ns/pkt ("baseline") to 933 ns/pkt ("optimized PGO") to Profile-Guided Optimization. This 6.8× improvement is far outside the typical PGO benefit range (10-30% on compute-bound code already compiled with -O3 -flto). The repository's own Makefile (line 97) documents PGO as providing "10-20% additional speedup" over the default -O3 -flto build. The paper does not disclose the compiler flags used for the "baseline" configuration. If the baseline is -O0 or -O1 without LTO, the speedup is plausible but is not a PGO effect — it is an optimization-level effect. If the baseline is the default -O3 -flto build, the 6.8× claim is not credible.
**Evidence Anchor**: [`dataset: Makefile line 97 "10-20% additional speedup" vs Table 8 "~6,300 ns to 933 ns"`]
**Why it matters**: The single-thread service time of 933 ns is the foundation for the "sub-microsecond" claim and the throughput calculations throughout Tables 8-9. If the baseline is mislabeled, the entire performance narrative is distorted, and the "effective rate" claims in Table 8 cannot be reproduced by a reader.
**Suggestion**: Specify the exact compiler flags for both "baseline" and "PGO" configurations. If the baseline is -O0 or a debug build, state this explicitly and rename the comparison to "unoptimized vs. PGO-optimized" rather than implying PGO alone produced the 6.8× gain. Include an intermediate row (e.g., `-O3 -flto` without PGO) to decompose the optimization-level and PGO contributions separately.
**Severity**: Critical — this single defect, if uncorrected, invalidates the core performance claim and makes acceptance impossible.
**Confidence**: 5 — core expertise: performance engineering and PGO benchmarking

### W2: Classification evaluation is circular — same generator trains and tests the model
**Problem**: The classifier is trained on 6,000 samples from `gen_sample()` and tested on 4,000 newly drawn samples from the same `gen_sample()` with the same class-conditional uniform distributions. The model never sees data from a different distribution. Since the generator produces well-separated classes by design (Table 2 shows minimal range overlap), 99.1% accuracy is largely a property of the generator rather than evidence of learning. There is no comparison to any classical rule, decision tree, or production tool on the same data, and the paper acknowledges this in §5.7 but offers no substitute.
**Evidence Anchor**: [`text: §5.7 "does not contain a reproducible classical or production-tool baseline on the same workload"`]
**Why it matters**: Without a non-neural baseline (e.g., a simple rule-based classifier using the same four features), the reader cannot determine whether the MLP adds value over a trivial threshold test. Given the strong range separation (VoIP ipv4.len in [60,200] vs. Bulk in [1400,1500]), a three-rule system would likely achieve >95% accuracy on the same generator. The 99.1% figure is therefore uninformative about the classifier's representational contribution.
**Suggestion**: Implement and evaluate at least one non-neural baseline (e.g., a k-NN, a decision tree, or a hand-crafted rule set) on the same train/test split. Report its accuracy alongside the MLP to quantify the marginal benefit of the neural approach.
**Severity**: Major — materially weakens confidence in the claim that the MLP classifier contributes discriminative power beyond trivial feature thresholds.
**Confidence**: 5 — core expertise: ML evaluation methodology for traffic classification

### W3: Table 5 LOC counts are materially inaccurate for multiple modules
**Problem**: I counted lines in the artifact files and compared to Table 5's claims:
- `parser.c`: claimed ~350, actual 187 (overstated by 87%)
- `p4c.c`: claimed ~1,200, actual 933 (overstated by 29%)
- `dnn.c`: claimed ~200, actual 163 (overstated by 23%)
- `alu.c`: claimed ~280, actual 398 (understated by 30%)
- `pipeline.c`: claimed ~250, actual 368 (understated by 32%)

The sum of Table 5's claimed values is ~3,745, yet the text claims "approximately 4,200 lines of core ISO C11." The actual sum of the listed modules is ~3,518.
**Evidence Anchor**: [`dataset: wc -l src/parser.c(187), src/p4c.c(933), src/dnn.c(163), src/alu.c(398), src/pipeline.c(368) vs Table 5`]
**Why it matters**: LOC is a proxy for system complexity and engineering effort. Inflated counts for some modules and deflated counts for others undermine the credibility of the implementation description. The discrepancy between the Table 5 sum (~3,745 claimed) and the text's "~4,200" suggests either missing modules or arithmetic error.
**Suggestion**: Re-count lines using `wc -l` on each file and update Table 5 and the text with verified values. If additional files contribute to the ~4,200 figure, list them explicitly.
**Severity**: Major — inaccurate implementation metrics undermine the prototype description.
**Confidence**: 5 — direct measurement via wc -l

### W4: Single-split accuracy reporting without variance, confidence intervals, or cross-validation
**Problem**: The 99.1% accuracy is a point estimate from one train/test split (one call to `srand(42)`, one training run, one test set). No standard deviation, confidence interval, bootstrap, or repeated-run variance is reported. The acceptance criterion in Table 12 states ">99% overall" and ">95% per-class," but these thresholds are evaluated against a single realization. The paper acknowledges this in §5.9 and §6.1 but does not address whether the result is stable across seeds.
**Evidence Anchor**: [`absence: §5.3 — expected confidence intervals or repeated-seed variance; checked §5.3, §5.9, §6.1, Table 12`]
**Why it matters**: A 99.1% point estimate from a single seed could vary by several percentage points across seeds, especially given the stochastic SGD training with `frand()` initialization. Without variance, the ">99%" acceptance criterion is not statistically meaningful.
**Suggestion**: Run the experiment with at least 5-10 different seeds and report mean ± standard deviation for overall and per-class accuracy. Alternatively, use k-fold cross-validation on the generated dataset.
**Severity**: Major — the core accuracy claim lacks statistical support.
**Confidence**: 5 — core expertise: ML evaluation methodology

### W5: 16-worker configuration oversubscribes 8-vCPU host, misleadingly presented
**Problem**: The test host has 8 vCPUs, yet Table 9 reports results for 16 workers (2× oversubscription). While the paper acknowledges this ("The 16-worker configuration oversubscribes the 8-vCPU test environment"), the result is still presented as a scaling data point and used to claim "2.57× speedup at 16 workers." On an 8-vCPU host, the maximum meaningful worker count is 8; beyond that, throughput gains come from I/O wait overlap (which this in-memory benchmark has none of) or are artifacts of OS scheduling. The 16% efficiency at 16 workers (Table 9) is consistent with pure contention overhead.
**Evidence Anchor**: [`table: Table 9 — 16 workers on 8-vCPU host, efficiency 16%`]
**Why it matters**: Presenting oversubscribed configurations as scaling results is a well-known anti-pattern in systems benchmarking. The 2.57× speedup claim is technically correct but practically misleading — a reader skimming Table 9 may conclude the framework scales to 16 cores.
**Suggestion**: Cap the primary scaling analysis at 8 workers (matching the physical core count). Present 16-worker results in a separate "oversubscription sensitivity" sub-section with explicit framing that no additional physical resources are available.
**Severity**: Minor — the paper acknowledges oversubscription; the finding is about presentation framing.
**Confidence**: 5 — core expertise: multicore benchmarking methodology

### W6: Performance measurement uses wall-clock time without NIC I/O and with 126-byte "effective rate" arithmetic
**Problem**: The throughput benchmark processes packets entirely in memory (no NIC receive or transmit), yet reports "Effective Rate" in Gbps using 126-byte payload multiplication. This conflates in-memory processing throughput with network throughput. At 933 ns/pkt, the "effective rate" of ~1.1 Gbps assumes a steady stream of 126-byte payloads arriving at line rate, but no actual packet I/O is exercised. The per-packet cost includes `clock_gettime` overhead, which at sub-microsecond granularity can be significant (typically 20-40 ns on Linux with vDSO).
**Evidence Anchor**: [`text: Table 8 caption "Effective rate is payload-size multiplication and excludes Ethernet framing and interpacket overhead"`]
**Why it matters**: Readers may interpret "1.1 Gbps" as a network-line-rate capability, when in fact it is an in-memory processing rate. Without NIC I/O, the benchmark does not capture DMA overhead, descriptor ring management, or interrupt coalescing effects that dominate real packet-processing pipelines.
**Suggestion**: Clearly label the throughput as "in-memory processing rate" rather than "Effective Rate." If possible, add a loopback NIC benchmark (e.g., DPDK testpmd with the RMT pipeline as a processing stage) to ground the throughput claims.
**Severity**: Minor — the caption acknowledges the limitation; the concern is about framing.
**Confidence**: 4 — adjacent field: DPDK benchmarking practices

### W7: Training loop uses rand() for both label selection and feature generation, creating implicit correlation with evaluation
**Problem**: The training function (`train()`, lines 142-207) calls `rand()` for label selection (`rand() % NOUT`) and `gen_sample()` also calls `rand()` internally for feature values. The test loop (lines 195-206) then continues drawing from the same `rand()` sequence. Because `rand()` is a linear congruential generator with known periodicity and correlation properties, the training and test samples are drawn from a single deterministic pseudorandom stream without re-seeding. While this ensures reproducibility, it means the train/test boundary is arbitrary — both sets are from the same stream and distribution.
**Evidence Anchor**: [`dataset: src/enc_classify_demo.c lines 158-206 — train and test share single rand() stream`]
**Why it matters**: The absence of an explicit re-seed or stream separation between training and test data means the test set is not an independent realization of the data-generating process. A reviewer cannot determine whether the 99.1% accuracy would hold on an independently seeded test set.
**Suggestion**: Re-seed the RNG before the test phase (e.g., `srand(12345)`) or use a separate PRNG instance for test data generation to ensure independence.
**Severity**: Minor — the practical impact is likely small given the uniform distributions, but the practice is methodologically unsound.
**Confidence**: 4 — core expertise: reproducible ML evaluation

---

## Coverage Receipt (conditional *)

Not applicable — both Strengths and Weaknesses lists are populated.

---

## Detailed Comments *

### Methodology / Research Design

**Classification evaluation (§5.3).** The synthetic generator defines class-conditional uniform ranges (Table 6) that are deliberately well-separated. The MLP is then trained and tested on samples from this same generator. This design is appropriate for a mechanism-feasibility study but insufficient for a claim of classification accuracy. The 99.1% result is a near-deterministic consequence of the range separation (VoIP max ipv4.len = 200, Bulk min = 1400, a 7× gap), not evidence of the MLP's discriminative capacity. The absence of any non-neural baseline (§5.7) prevents the reader from assessing whether the MLP adds value over a trivial rule set.

**Statistical reporting (§5.3, §5.9, Table 12).** All reported metrics are point estimates from a single seed-42 run. The acceptance criteria in Table 12 (">99% overall," ">95% per-class") are evaluated once and declared met. No variance, confidence interval, or repeated-measures analysis is provided. The paper's acknowledgment of this gap in §6.1 is appreciated but does not remedy it.

**Performance measurement (§5.4).** The ~6.3 µs to 933 ns PGO claim is the most problematic measurement in the paper. The Makefile's PGO target explicitly documents "10-20% additional speedup" (line 97), which is consistent with published PGO literature for -O3 code. A 6.8× improvement implies the "baseline" is compiled at a lower optimization level (likely -O0 or -O1), but the paper does not disclose this. The 126-byte "effective rate" calculation is a standard practice in DPDK-style benchmarking but is misleading without NIC I/O context.

**Multi-core scaling (§5.5).** The sub-linear scaling and declining efficiency are honestly reported. However, running 16 workers on 8 vCPUs without I/O wait is pure CPU contention, and the result should be framed as an oversubscription sensitivity test rather than a scaling result. The 2.57× speedup at 8 workers (matching core count) is the more meaningful data point.

**Network coding evaluation (§5.10-§5.11).** The GF(2^8) arithmetic verification (exhaustive over all 255 non-zero elements) and single-loss recovery tests are sound functional tests. The stress test (40,000 packets, 5,000 generations) confirms generation accounting. However, the coding evaluation is self-referential: the same `nc_demo.c` that implements the coding also tests it, without an independent decoder implementation. The amortized-cost methodology (Table 15) is valid for isolating encoder computation, and the disclaimer about excluding buffer copies and I/O is appropriate.

**Internal numeric consistency.** Table 8 vs. Table 9 are consistent (16-worker = 2.75 Mpps in both). Table 16 repair arithmetic is exact (1,234 / 10,000 = 12.34%). Table 17 generation counts match floor(10000/k) for each class. The main inconsistency is Table 5 LOC counts vs. actual file sizes, and the text's "~4,200" vs. the Table 5 sum of ~3,745.

### Results / Findings

The results are presented clearly and the tables are well-structured. The consolidated summary in Table 18 is useful. However, the "Design Target" column in Table 18 reads as post-hoc goal-setting: the targets appear to have been set to match the observed results (e.g., "<1,000 ns" target vs. 933 ns result; ">2.5× speedup" target vs. 2.57× result). This reduces the evidential value of the target-met framing.

### Discussion

The limitations section (§6.1) is one of the strongest parts of the paper. Nine distinct limitations are enumerated with precision, including the synthetic evaluation scope, absence of statistical rigor, TCP-only coverage, adversarial fragility, and lack of UPF integration. This section should be preserved and expanded in the revision.

---

## Questions for Authors *

1. **What compiler flags were used for the "Single-thread, baseline" configuration in Table 8?** The Makefile shows the default build already uses `-O3 -flto -march=native -funroll-loops`, and the PGO target documents "10-20% additional speedup." Can you clarify whether the 6,300 ns baseline was compiled at -O0, -O1, or some other level? If the baseline is the default -O3 build, how do you reconcile the 6.8× speedup with the Makefile's 10-20% PGO estimate?

2. **Have you evaluated a non-neural baseline (e.g., a rule-based classifier or decision tree) on the same synthetic workload?** Given the strong range separation in Table 6, a simple threshold-based classifier might achieve comparable accuracy. What is the marginal discriminative contribution of the MLP over such a baseline?

3. **Can you report accuracy variance across multiple random seeds?** The current 99.1% is from a single seed-42 run. Running the experiment with 5-10 different seeds would establish whether the result is stable or sensitive to the particular training trajectory and test-sample realization.

4. **Does the `gen_realistic_traffic.py` script (which uses Gaussian distributions rather than uniform ranges) produce different accuracy results when fed to the trained model?** The script exists in the repository but its results (in `real_traffic_test_report.md`) show 97.0% overall accuracy — lower than the 99.1% on uniform data. Is this difference discussed anywhere in the manuscript, and does it inform the synthetic-vs-realistic gap?

---

## Minor Issues

### Language / Grammar
- §1.3, Pillar 3: "approximately 1,200 LOC" for p4c.c should match the actual 933 lines.
- §5.4, Table 8 caption: "Effective rate is payload-size multiplication" — consider rephrasing as "Projected throughput assuming continuous 126-byte payload delivery, excluding framing."

### Figures and Tables
- Table 5: Update all LOC values to match `wc -l` output. Consider adding a "Verified LOC" column or footnote.
- Table 9: Add a row for 8 workers highlighted as "matches physical core count" and move 16 workers to a separate "oversubscription" sub-table.
- Figure 1: The ASCII diagram is functional but could benefit from a proper figure with labeled data-flow arrows and component boundaries.

### Layout
- The paper uses ASCII art for Figures 1-3. For a camera-ready version, these should be replaced with vector graphics.

---

## Dimension Scores *

| Dimension | Score (0-100) | Descriptor | Notes |
|-----------|--------------|------------|-------|
| Originality (20%) | 72 | Adequate | Novel integration of MLP inference within an RMT pipeline for satellite UPF, but the contribution is primarily architectural rather than methodological. The synthetic-data scope limits originality of the evaluation findings. |
| Methodological Rigor (25%) | 48 | Weak | Circular evaluation design (same generator for train/test), no non-neural baseline, single-split accuracy without variance, PGO claim not credible as stated, LOC table inaccurate. The limitations section partially compensates but does not remedy the design gaps. |
| Evidence Sufficiency (25%) | 55 | Weak | The artifact is available and partially verifiable, but the core accuracy claim rests on a single synthetic split without statistical support. Performance claims lack adequate configuration disclosure. No real-traffic evidence. |
| Argument Coherence (15%) | 78 | Strong | The paper's logical flow from problem statement through design to evaluation is clear. Limitations are well-integrated. The main coherence gap is the PGO narrative, which is internally inconsistent with the Makefile. |
| Writing Quality (15%) | 82 | Strong | Clear, precise technical prose. Good use of definitions, algorithms, and tables. Minor verbosity in some sections but generally well-structured. |
| **Weighted Average** | **64** | **Major Revision** | Weighted: 72×0.20 + 48×0.25 + 55×0.25 + 78×0.15 + 82×0.15 = 14.4 + 12.0 + 13.75 + 11.7 + 12.3 = **64.15** |

---

## Artifact Verification Summary

| Claim | Verified? | Details |
|-------|-----------|---------|
| Seed 42 | ✓ Confirmed | `srand(42)` at enc_classify_demo.c:261 |
| Generator ranges (Table 6) | ✓ Confirmed | Exact match in gen_sample(), lines 62-94 |
| Train size 6,000 / Test size 4,000 | ✓ Confirmed | `train(6000, 40, 0.05f)`, ntest=4000 |
| 40 epochs, lr 0.05 | ✓ Confirmed | Arguments to train() |
| Q16.16 agreement test (2,000 samples) | ✓ Confirmed | Lines 289-303, nq=2000 |
| bench_all.sh runs T1-T7 | ✓ Confirmed | Script covers all categories |
| Table 5 LOC for parser.c (~350) | ✗ Contradicted | Actual: 187 lines |
| Table 5 LOC for p4c.c (~1,200) | ✗ Contradicted | Actual: 933 lines |
| Table 5 LOC for dnn.c (~200) | ✗ Contradicted | Actual: 163 lines |
| Table 5 LOC for alu.c (~280) | ✗ Contradicted | Actual: 398 lines |
| Table 5 LOC for pipeline.c (~250) | ✗ Contradicted | Actual: 368 lines |
| "~4,200 lines of core C11" | ✗ Contradicted | Table 5 claimed sum: ~3,745; actual sum: ~3,518 |
| PGO yields 6.8× speedup | ✗ Not credible | Makefile says "10-20% additional speedup"; baseline flags undisclosed |
| Table 16 repair sum = 1,234 | ✓ Confirmed | 312+296+626+0 = 1,234 |
| Table 17 generation counts = floor(10000/k) | ✓ Confirmed | Exact for all classes |
| Table 8 vs Table 9 consistency | ✓ Confirmed | 16-worker = 2.75 Mpps in both |

---

# Peer Review Report

## Manuscript Information
- **Title**: EdgeNet-CoM: Service-Aware In-Network Computing for Satellite User Plane Functions
- **Manuscript ID**: N/A
- **Review Date**: 2026-08-04
- **Review Round**: Round 1

---

## Reviewer Information

### Reviewer Role *
Peer Reviewer 2 (Domain Expert)

### Reviewer Identity *
Programmable-data-plane researcher with expertise in P4-based in-network intelligence, match-action pipeline compilation (RMT lineage), Tofino-class hardware constraints, in-network ML systems (Xiong & Zilberman, N3IC, IIsy, Packet Transactions), 3GPP 5G core UPF architecture (N4/PFCP, GTP-U, QFI/5QI), and software data-plane frameworks (DPDK, ClickOS, NetBricks, eBPF/XDP).

### Review Focus *
Domain accuracy of the RMT pipeline architecture claims, P4 subset compiler characterization, Q16.16 fixed-point arithmetic, VLIW execution model, Theorem 1 soundness, 5G UPF terminology, literature coverage, and novelty positioning relative to prior programmable data planes and in-network ML systems.

---

## Overall Assessment *

### Recommendation *
- [x] **Major Revision** — Substantial revisions needed, re-review required after revision

### Confidence Score *
5 — Completely within my area of expertise; I have published in programmable data planes, match-action compilation, and in-network ML, and am conversant with 3GPP UPF architecture.

### Summary Assessment *
This paper proposes EdgeNet-CoM, a software RMT pipeline integrating a Q16.16 MLP extern for per-packet encrypted traffic classification, coupled with DSCP marking, LPM routing, and a connected network-coding harness. The artifact-verified claims are largely accurate: the 16-wide VLIW executor with genuine two-phase commit, the computed-goto dispatch, the P4-inspired subset compiler producing parser states/tables/ALU microcode, and the bit-trie LPM engine are all implemented as described. The honest scoping language—disclaiming full P4_16 conformance, NIC integration, and real-traffic validation—is exemplary for a feasibility prototype.

However, several domain-accuracy issues weaken the paper. Algorithm 1's accumulator description has a subtle inconsistency between the stated Q16.16 to int32_t truncation at the hidden layer and the implementation's int64_t storage, which matters for formal correctness claims. The runtime table-update claim (Challenge 3) is API-level correct but lacks thread-safety guarantees for concurrent packet processing—a gap the paper does not disclose. Application B is more accurately a connected test harness than a pipeline-resident application, yet the paper frames it as a co-equal "application instantiation." Key literature is missing (NetBricks, eBPF/XDP classification, Sonata/FlowRadar telemetry), and the 5G terminology is mostly correct but the DSCP/QFI distinction could be more precisely located in the packet processing context. Theorem 1's proof is sound but omits the single-dispatcher precondition. These issues are individually addressable but collectively require substantial revision to the technical narrative and scoping language.

---

## Strengths *

### S1: Artifact-verified VLIW two-phase commit execution
The paper's claim of 16-wide VLIW execution with two-phase commit is verified in the artifact. In `alu.c`, each VLIW word allocates a `wb_t wb[RMT_NUM_ALUS]` write-back buffer (line 67). Phase 1 (lines 71-384) reads all source operands from PHV before computing results; Phase 2 (lines 387-396) commits all writes simultaneously. This is a faithful software emulation of the hardware ALU latch-and-commit semantics from the RMT architecture.
**Evidence Anchor**: `dataset: src/alu.c lines 66-397, two-phase read/commit loop with wb[] write-back buffer`

### S2: Genuine P4-inspired subset compiler with realistic scoping
The `p4c.c` module (933 LOC) is a legitimate compiler for a P4-14-flavored subset, producing parser state machines, match-action table configurations, stage assignments, and VLIW microcode with dependency-aware list scheduling. The paper's characterization as "not a complete P4_16 compiler" (Section 1.3) is accurate: the compiler supports header/metadata declarations, parser FSMs with select/goto, actions with arithmetic/extern primitives, tables with exact/LPM keys, and linear control flow with valid() gating—but lacks P4_16 structs, loops, counters, meters, registers, and type checking. This honesty about scope is commendable.
**Evidence Anchor**: `dataset: src/p4c.c 933 LOC compiler with lexer, parser, action scheduler, and control-flow stage mapper`

### S3: Computed-goto dispatch and per-stage table indexing verified
The computed-goto dispatch table in `alu.c` (lines 51-63) uses GCC &&label and goto *dispatch[in->op] as claimed in Section 4.4. The per-stage table index stage_tbl[stage][idx] is built in rmt_finalize() (pipeline.c lines 101-106) and used in the fast path (pipeline.c line 218) to iterate only over relevant tables. Both optimizations are implemented as described.
**Evidence Anchor**: `dataset: src/alu.c lines 51-63 (dispatch table) and src/pipeline.c lines 101-106 (stage_tbl construction)`

### S4: Q16.16 quantization scheme is correctly implemented
The quant() function in dnn.c (lines 73-79) correctly scales by 2^16, rounds half away from zero, and saturates. The forward pass in rmt_dnn_infer() (lines 108-150) uses int64_t accumulators for Q32.32 intermediate products and shifts back to Q16.16 after each layer, matching the algorithmic description in Section 4.3.2.
**Evidence Anchor**: `dataset: src/dnn.c lines 73-150 quantization and integer-only forward pass`

### S5: Exemplary honest scoping and limitation disclosure
The paper consistently qualifies claims with explicit boundaries: "standalone data-plane implementation rather than a complete 3GPP-conformant UPF" (Section 1.1), "results reflect generator separation" (Section 5.3b), "the demo records per-label correct counts but not the complete confusion matrix" (Section 5.3c), and a dedicated Section 6.1 enumerating seven specific limitations. This level of intellectual honesty is unusual and valuable for a systems paper.
**Evidence Anchor**: `text: Section 1.1 "standalone data-plane implementation rather than a complete 3GPP-conformant UPF"`

### S6: Theorem 1 proof is logically sound under stated assumptions
The per-flow ordering theorem correctly identifies that (a) identical 5-tuples produce a constant hash, (b) constant hash maps to a constant queue, and (c) the SPSC ring preserves FIFO insertion order. The artifact's rmt_rss_flow_hash() in mq.c (lines 308-389) implements FNV-1a with murmur3 fmix32 finalization, parsing Ethernet/IPv4/IPv6 down to TCP/UDP 5-tuples as described.
**Evidence Anchor**: `dataset: src/mq.c lines 308-389, flow hash implementation matching the theorem premises`

### S7: Parser FSM implementation matches paper description
The parser in parser.c implements a table-driven FSM with parse_state_t structures containing extraction operations, select-field-based transitions, and default next-states. The enc_classify.p4 program (lines 85-112) compiles to the exact 5-state parser described in Figure 2 (start to parse_ipv4 to parse_tcp to parse_tls to accept), with conditional transitions on ethertype, protocol, and destination port.
**Evidence Anchor**: `dataset: p4/enc_classify.p4 lines 85-112 and src/parser.c FSM implementation matches Figure 2`

---

## Weaknesses *

### W1: Algorithm 1 accumulator description inconsistent with implementation at hidden layer output
**Problem**: Algorithm 1 line 9 states h[j] = max(0, (int32_t)(acc >> 16)), implying the hidden-layer activation is truncated to 32 bits before being stored. The implementation in dnn.c (line 124) stores h[j] as int64_t, and line 129 computes h[j] = acc > 0 ? (acc >> Q) : 0 without the int32_t cast. In the output layer (Algorithm 1 line 14), the multiplication (int64_t)W2[k][j] * h[j] then operates on a 64-bit h[j] rather than a 32-bit value. For the evaluated network dimensions (4 inputs, 24 hidden units), the accumulated value after shift fits within int32_t range, so no overflow occurs in practice. However, the algorithm's formal description promises a 32-bit truncation that the implementation does not perform. If the network were wider or weights larger, the discrepancy could produce silently different results between the algorithm and the code.
**Evidence Anchor**: `text: Algorithm 1 line 9 "h[j] <- max(0, (int32_t)(acc >> 16))" vs src/dnn.c line 124 "int64_t h[]"`
**Why it matters**: The paper makes a formal-correctness argument around the Q16.16 arithmetic (Section 4.3.2, Algorithm 1). A mismatch between the pseudocode and implementation undermines confidence in the precision claims, especially since Section 5.3 reports 100% float/fixed-point agreement on 2,000 samples—this agreement is validated against the implementation, not the algorithm as written.
**Suggestion**: Either (a) revise Algorithm 1 line 9 to h[j] = max(0, acc >> 16) (removing the int32_t cast) and explain that the implementation uses 64-bit storage for hidden activations with a correctness argument for the given network dimensions, or (b) add the explicit int32_t cast in the implementation to match the algorithm and verify that accuracy is preserved.
**Severity**: Minor
**Confidence**: 5 — core expertise: fixed-point arithmetic and ML inference implementations

### W2: Runtime table update claim lacks thread-safety analysis
**Problem**: Challenge 3 (Section 1.2) requires "runtime reconfiguration of classification models, QoS policies, and forwarding logic without data-plane restart." The paper states that "Models, table entries, and action parameters can be updated at runtime" (Section 1.3, Pillar 3). The artifact provides rmt_load_entries() (runtime.c) for adding table entries and rmt_dnn_load() (dnn.c) for installing model weights. However, the trie-based match engine in match.c has no read-write locking: rmt_table_insert() modifies trie nodes (lines 33-77) while rmt_table_lookup() traverses them (lines 82-135). If a worker thread is mid-lookup when the control plane inserts a new entry, the lookup may observe a partially-constructed trie path. Similarly, rmt_dnn_load() overwrites weight arrays while rmt_dnn_infer() may be reading them. The paper does not acknowledge this concurrent-safety gap.
**Evidence Anchor**: `absence: Section 4.4, 6.1, 6.2 — expected thread-safety analysis for runtime updates during concurrent packet processing; checked Section 4.4, Section 6.1, Section 6.2`
**Why it matters**: Runtime reconfigurability is one of the five core challenges and a pillar of the framework. A production UPF must update policies while processing packets. The absence of any synchronization mechanism means the claim holds only for single-threaded or stop-the-world scenarios, which is not disclosed.
**Suggestion**: Add a paragraph in Section 6.1 or 6.2 explicitly stating that the current prototype does not provide atomic table updates under concurrent access, and that a production deployment would require either (a) read-copy-update (RCU) for the trie, (b) a shadow-table swap mechanism, or (c) a stop-the-world pause during updates.
**Severity**: Major
**Confidence**: 5 — core expertise: concurrent data structures in packet processing systems

### W3: Application B is a connected test harness, not a pipeline-resident application
**Problem**: The paper frames Application B (Service-Aware Network Coding) as a co-equal "application instantiation" alongside Application A (Section 1.3, Section 1.4 contribution 3). However, Application A executes entirely within the RMT pipeline's four match-action stages (classification, DSCP marking, LPM routing, MAC rewrite), while Application B is a standalone C harness (nc_demo.c, 849 LOC) that reads the pipeline's output and performs coding operations externally. The paper acknowledges this boundary (Section 4.7.4, Figure 3) but the framing as "two representative satellite UPF applications" implies equal integration. The coding module does not inject repair packets into the deparser, does not maintain per-flow generation buffers within the pipeline's PHV, and does not participate in the match-action abstraction.
**Evidence Anchor**: `text: Section 4.7.4 "This split validates the policy coupling and the coding algorithms, but it is not equivalent to a stateful NC extern in the packet pipeline"`
**Why it matters**: Contribution 3 claims "Two coupled service-aware decisions" as a core contribution. The coupling for Application A is genuine (classification result drives DSCP and routing within the pipeline). For Application B, the coupling is an output-file or function-call boundary. Readers expecting a stateful NC extern integrated into the RMT stages will be disappointed.
**Suggestion**: Reframe Application B as "a connected evaluation harness demonstrating reuse of classification output for coding-policy selection" rather than an "application instantiation." Alternatively, implement at least a stub NC extern opcode in the ALU that writes generation metadata into PHV fields, demonstrating how the coding would integrate if fully implemented.
**Severity**: Major
**Confidence**: 4 — core expertise: RMT pipeline architecture and extern design

### W4: Definition 4 saturation bounds differ between specification and implementation
**Problem**: Definition 4 (Section 3.1) states that Q16.16 values are "saturated at the integer limits," implying +/-2,147,483,647 (INT32_MAX). The implementation in dnn.c (lines 76-77) saturates at +/-2,147,483,000—slightly below INT32_MAX. This is a defensive choice to prevent overflow in subsequent arithmetic, but it means the representable range stated in Definition 4 is slightly narrower in practice.
**Evidence Anchor**: `text: Section 3.1 Definition 4 "saturating at the integer limits" vs src/dnn.c lines 76-77 saturation at +/-2147483000`
**Why it matters**: Definition 4 is the formal basis for the fixed-point arithmetic used throughout the paper. An inaccurate definition could mislead implementers who rely on the stated range for overflow analysis.
**Suggestion**: Revise Definition 4 to state the actual saturation bounds used in the implementation, or use INT32_MAX in the implementation and handle overflow in the multiply-accumulate step.
**Severity**: Minor
**Confidence**: 5 — core expertise: fixed-point arithmetic specifications

### W5: Theorem 1 omits the single-dispatcher precondition
**Problem**: Theorem 1 proves per-flow ordering under RSS hashing with SPSC FIFO queues. The proof correctly identifies that identical 5-tuples produce a constant hash and that the SPSC ring preserves FIFO order. However, the proof's third sentence embeds a critical precondition ("Since the single dispatcher inserts packets in wire-arrival order") without elevating it to a formal assumption. In a multi-queue NIC environment with multiple RX queues feeding multiple dispatchers, arrival order between dispatchers is not guaranteed.
**Evidence Anchor**: `text: Section 4.5 Theorem 1 proof "Since the single dispatcher inserts packets in wire-arrival order"`
**Why it matters**: The theorem is presented as a general correctness guarantee. Without the single-dispatcher assumption, a reader implementing a multi-dispatcher variant could incorrectly assume ordering is preserved.
**Suggestion**: Add an explicit assumption to Theorem 1: "Assumption: a single dispatcher thread processes all incoming packets in wire-arrival order before RSS hash-based queue assignment."
**Severity**: Minor
**Confidence**: 5 — core expertise: packet processing correctness under parallel dispatch

### W6: Missing literature on software data-plane frameworks and eBPF/XDP classification
**Problem**: The related work section (Section 2) covers hardware in-network ML and encrypted traffic classification but omits several important categories of software programmable data planes directly relevant to EdgeNet-CoM's positioning: (a) NetBricks (Panda et al., OSDI 2016) provides a software NFV framework with per-core run-to-completion processing and flow-affine scheduling—the same scaling model EdgeNet-CoM uses; (b) eBPF/XDP-based traffic classification represents a growing class of kernel-bypass classifiers operating at per-packet granularity without payload inspection; (c) Sonata (Krishnamoorthy et al., NSDI 2018) and FlowRadar (Kim et al., NSDI 2016) demonstrate in-network telemetry in programmable pipelines; (d) Marple (Zhu et al., SIGCOMM 2017) performs in-network performance analysis.
**Evidence Anchor**: `absence: Section 2.1, 2.2, 2.6 — expected discussion of NetBricks, eBPF/XDP classification, Sonata, FlowRadar, Marple; checked Section 2.1-2.6, Table 1`
**Why it matters**: EdgeNet-CoM's novelty claim rests on being a "software RMT pipeline" (Section 2.6). Without positioning against other software data-plane frameworks that also achieve per-packet processing with flow-affine parallelism, the paper overstates its uniqueness.
**Suggestion**: Add coverage of software programmable data planes (NetBricks, ClickOS, P4-DPDK targets, eBPF/XDP classifiers) in Section 2.1. Table 1 should include a "Software NFV/DPDK" column.
**Severity**: Major
**Confidence**: 4 — adjacent field: NFV frameworks and kernel-bypass packet processing

### W7: Computed-goto speedup claim lacks measurement methodology
**Problem**: Section 4.4 states that computed-goto dispatch "reduces per-stage execution time by approximately 40%." This is a specific quantitative claim, but the paper provides no measurement methodology, no comparison baseline, and no reference to supporting data. Table 8 reports end-to-end pipeline timing but does not decompose it into dispatch vs. computation overhead.
**Evidence Anchor**: `text: Section 4.4 "this optimization reduces per-stage execution time by approximately 40%"`
**Why it matters**: Quantitative performance claims require measurement evidence. A 40% improvement from a single compiler optimization is unusually large and warrants a controlled A/B measurement.
**Suggestion**: Either provide a measured comparison (build with and without computed goto, measure single-thread ns/pkt for both), or soften the claim to a qualitative statement about eliminating branch-prediction overhead.
**Severity**: Minor
**Confidence**: 4 — core expertise: compiler optimization effects on packet processing

### W8: DSCP marking layer ambiguity in 5G GTP-U tunnel context
**Problem**: Section 6.2 correctly notes that "DSCP and QFI are distinct fields governed by different policies." However, the paper does not explain where in the packet the DSCP value lives relative to the 5G QoS architecture. In a 5G UPF, the UPF operates on GTP-U tunnels: the DSCP field is in the outer IP header of the GTP-U tunnel (N3/N9 interface), while QFI is in the GTP-U extension header. The current prototype writes DSCP to the parsed ipv4.tos field, which in a GTP-U-encapsulated packet would be the inner IP header.
**Evidence Anchor**: `absence: Section 4.6.1, 6.2 — expected clarification of which IP header (inner vs outer GTP-U) receives the DSCP mark; checked Section 4.6.1, Section 6.2`
**Why it matters**: For 5G readers, the DSCP marking description is ambiguous about the tunnel context. A real UPF would need to mark the outer tunnel header for downstream DiffServ treatment.
**Suggestion**: Add a sentence clarifying that the prototype writes DSCP to the parsed IPv4 header (which in a GTP-U deployment would be the inner header), and that a production UPF would need to parse the GTP-U outer header and apply SMF-configured QER rules.
**Severity**: Minor
**Confidence**: 4 — adjacent field: 3GPP UPF user-plane protocol stack

### W9: Table 1 missing comparison with DPDK-based ML middleboxes
**Problem**: Table 1 positions EdgeNet-CoM against Flow-Level ML, In-Network ML, SDN/SD-WAN, and Network Coding. However, the most direct comparison is DPDK-based NFV middleboxes that perform ML classification (e.g., a DPDK application calling TensorFlow Lite for per-packet classification). These systems achieve per-packet processing at software-pipeline speeds, can embed arbitrary ML models, and use flow-affine scheduling—the same architecture as EdgeNet-CoM minus the RMT match-action abstraction.
**Evidence Anchor**: `absence: Table 1 — expected a "DPDK-based NFV ML" column; checked Table 1, Section 2.1, Section 2.6`
**Why it matters**: Table 1 is the paper's primary positioning tool. Without the most directly comparable system category, the table's conclusion about EdgeNet-CoM's differentiation is less persuasive.
**Suggestion**: Add a "DPDK NFV + ML" column to Table 1. The differentiator would then be the P4-inspired programmability and integrated match-action pipeline, which is genuinely absent from ad-hoc DPDK applications.
**Severity**: Major
**Confidence**: 4 — core expertise: software data-plane architectures and NFV frameworks

---

## Detailed Comments *

### Title & Abstract
- The title "EdgeNet-CoM" is descriptive and the acronym is well-formed. "Service-Aware In-Network Computing" accurately captures the scope.
- The abstract is well-structured, covering motivation, approach, results, and limitations. However, "satellite user-plane functions" in the abstract could mislead 5G readers into expecting a 3GPP-integrated system. The qualifier "standalone data-plane implementation" from Section 1.1 should appear in the abstract.

### Introduction
- Section 1.1 provides strong motivation for the satellite UPF context with concrete path characteristics (GEO/LEO/ISL/TB). The three differentiators (heterogeneous connectivity, elevated loss, constrained processing) are well-argued.
- Section 1.2's five challenges are clearly articulated and map well to the design pillars. Challenge 3 (runtime reconfigurability) is the weakest in terms of implementation evidence.
- Section 1.3's "five core design pillars" are well-organized with appropriate honest qualification.

### Literature Review / Theoretical Framework
- Section 2.1 covers the RMT/P4 lineage correctly ([4], [10]) and the in-network ML work ([11]-[13]) accurately. The characterization of Xiong & Zilberman [11] as "binary classification using match-action table entries as decision tree nodes" is correct.
- Section 2.2's taxonomy of encrypted traffic classification (flow-level, byte-sequence, per-packet header-only) is sound and well-organized.
- Section 2.4's network coding coverage ([23]-[27]) is correct: RLNC [24], COPE [25], Coded TCP [26], Fulcrum [27] are all characterized accurately.
- Gap: Missing software data-plane frameworks (see W6) and the DPDK+ML middlebox comparison (see W9).

### Methodology / Research Design
- The system model (Section 3.1) is well-formalized with six definitions. Definition 4 has a minor saturation-bound inaccuracy (see W4).
- Algorithm 1 is clearly presented but has the int32_t/int64_t discrepancy noted in W1.
- The optimization objective (Section 3.2) is well-formulated with appropriate caveats.

### Results / Findings
- Section 5.3's classification results (99.1% accuracy) are appropriately qualified.
- Section 5.4's timing results (933 ns PGO, 2.75 Mpps at 16 workers) are plausible for the described platform. The sub-linear scaling is honestly reported.
- Section 5.10's network coding evaluation is thorough with GF(2^8) arithmetic verification, encode/decode correctness, and generation stress testing.

### Discussion
- Section 6.1 (Limitations) is exemplary with seven specific limitations. The HTTP/2 multiplexing discussion is particularly insightful.
- Section 6.2 (Deployment Considerations) correctly identifies model lifecycle, graceful degradation, multi-tenancy, and 5G integration gaps.

### Conclusion
- The conclusion (Section 7) is well-balanced and does not over-infer.

### References
- 38 references total; core references are accurate.
- Missing references: see W6.

---

## Questions for Authors *

1. **Runtime table update thread safety**: The runtime.c rmt_load_entries() function inserts entries into the trie-based match engine without any synchronization mechanism. If a worker thread is executing rmt_table_lookup() concurrently, could it observe a partially-constructed trie path? Have you tested concurrent table updates during packet processing, and if so, what mechanisms prevent data races? If not, could you clarify the intended update model?

2. **Application B integration boundary**: The paper describes Application B as a "service-differentiated network coding" instantiation, but the coding module (nc_demo.c) operates as a standalone harness after rmt_process() returns. Could you clarify what specific pipeline-level integration would be needed to make Application B a true RMT extern (e.g., an OP_NC_ENCODE ALU opcode, per-flow generation buffers in PHV metadata, repair-packet injection in the deparser)?

3. **Comparison with DPDK+ML middleboxes**: A DPDK application using TensorFlow Lite or ONNX Runtime could also achieve per-packet ML classification with flow-affine scheduling, without the RMT match-action overhead. What specific advantages does the RMT pipeline abstraction provide over such an ad-hoc approach, beyond the intellectual elegance of the P4-inspired front end?

4. **Generalization beyond synthetic data**: The 99.1% accuracy is achieved on deliberately separated class-conditional uniform distributions (Table 2). In operational traffic, VoIP often uses RTP/UDP (not TLS/TCP), HTTP/2 multiplexes multiple services in one TCP connection, and QUIC is increasingly prevalent. Could you provide a preliminary analysis of how the four-feature model would perform on a captured traffic dataset?

---

## Minor Issues

### Language / Grammar
- Section 1.3 Pillar 3 and Table 5: "approximately 1,200 LOC" for p4c.c — the actual file is 933 lines (verified by wc -l). Consider correcting.
- Section 3.1 Definition 2: "tls.length(x)" in LaTeX math font may render ambiguously; consider \text{tls.length} for consistency.

### Figures and Tables
- Figure 1 is ASCII art; a proper block diagram would improve readability for camera-ready.
- Table 1 would benefit from the additional "DPDK NFV + ML" column suggested in W9.
- Table 8's "Effective Rate" column uses "~" prefix inconsistently.

### Layout
- The paper's structure is logical and well-organized. The five-pillar structure provides a coherent narrative.

---

## Dimension Scores *

| Dimension | Score (0-100) | Descriptor | Notes |
|-----------|--------------|------------|-------|
| Originality (20%) | 65 | Adequate | Software RMT + MLP extern is a genuine but incremental combination; individual components exist in prior work. Missing comparison with DPDK+ML middleboxes weakens novelty argument. |
| Methodological Rigor (25%) | 72 | Adequate | Well-formalized system model; Algorithm 1 has minor accumulator inconsistency; synthetic evaluation is transparent but limited; runtime-update thread-safety gap is a concern. |
| Evidence Sufficiency (25%) | 68 | Adequate | 38 references, mostly peer-reviewed; key claims artifact-verified; but missing literature on software NFV frameworks and the 40% computed-goto claim lacks evidence. |
| Argument Coherence (15%) | 78 | Strong | Clear logical flow from motivation through design to evaluation and limitations. Application B framing slightly overstates integration level. |
| Writing Quality (15%) | 82 | Strong | Professional prose with precise terminology. Honest scoping language is particularly well-executed. Minor LOC count discrepancies. |
| Literature Integration (R2 focus) | 62 | Adequate | Core RMT/P4 and in-network ML references are accurate. Significant gaps in software data-plane frameworks and telemetry systems. |
| **Weighted Average** | **72.0** | **Minor Revision band** | Weighted: 65*0.20 + 72*0.25 + 68*0.25 + 78*0.15 + 82*0.15 = 72.0 |

*Note: The weighted average of 72.0 falls in the Minor Revision range (65-79), but I recommend Major Revision because the thread-safety gap (W2), Application B framing (W3), and missing literature (W6, W9) collectively require structural changes to the narrative and scoping language that warrant re-review.*

---

# Peer Review Report

## Manuscript Information
- **Title**: EdgeNet-CoM: Service-Aware In-Network Computing for Satellite User Plane Functions
- **Manuscript ID**: (not assigned)
- **Review Date**: 2026-08-04
- **Review Round**: Round 1

---

## Reviewer Information

### Reviewer Role *
Peer Reviewer 3 (Cross-disciplinary / Practical Perspective)

### Reviewer Identity *
Satellite communications systems engineer working on 3GPP NTN (Release 17/18) network integration, gateway and on-board packet processing, transport-layer behavior over lossy high-RTT satellite paths, link-layer protection standards (DVB-S2X ACM), and 5G core deployment practice (GTP-U tunnels, PFCP sessions, QFI marking).

### Review Focus *
Cross-disciplinary and practical assessment: whether the satellite premise (§1.1) is technically credible, whether the claimed UPF deployment path survives contact with a real 5G user plane (GTP-U encapsulation, N4/PFCP, QFI), whether RLNC/XOR coding at the UPF layer is compatible with TCP congestion control and satellite link-layer protection, and whether the interdisciplinarity is genuine or juxtaposed.

---

## Overall Assessment *

### Recommendation *
- [ ] **Accept** — Can be published directly, only minor formatting changes needed
- [ ] **Minor Revision** — Minor revisions needed, no re-review after revision
- [x] **Major Revision** — Substantial revisions needed, re-review required after revision
- [ ] **Reject** — Not suitable for publication in this journal

### Confidence Score *
5 — Core expertise: satellite NTN gateway design, 3GPP UPF data-plane integration, and transport behavior over satellite paths.

### Summary Assessment *
The paper presents EdgeNet-CoM, a software RMT-based data-plane pipeline that integrates a Q16.16 fixed-point MLP classifier, DSCP marking, destination-based routing, and a connected network-coding harness, evaluated on synthetic traffic. The programmable-data-plane contribution is genuinely interesting: the prototype is well-engineered, the Q16.16 quantization is sound, and the 933 ns per-packet service time with PGO is credible for an in-memory software pipeline. However, from a satellite systems perspective the paper's framing substantially overreaches its implementation. The codebase contains zero GTP-U encapsulation, zero PFCP/N4 integration, zero satellite channel modeling, and zero link-layer interaction. The parser assumes bare Ethernet/IPv4/TCP/TLS headers; at a real satellite UPF the user-plane packets arrive inside GTP-U tunnels (UDP port 2152), so the parser's outer-header view would not expose the inner TLS record that drives classification. RLNC coding at the UPF, applied systematically before the satellite link's own DVB-S2X ACM layer, risks hiding losses from TCP congestion control without coordination. The paper is honest about many of these gaps in §6, but it presents them as incremental deployment steps rather than as fundamental architectural disconnects. The recommendation is Major Revision because the satellite framing needs to be either substantially narrowed (presenting this as a programmable data-plane study with satellite as a motivating scenario) or backed by at least a GTP-U-aware parser, a basic satellite channel-loss experiment, and a discussion of TCP/coding/link-layer interaction.

---

## Strengths *

### S1: Honest and extensive limitation disclosure
The paper explicitly states in §6.1 that the prototype has "no SMF/N4 implementation, GTP-U integration, PFCP rule handling, real NIC data path, or satellite protocol-stack validation." This level of candor is unusual and appreciated; it means the reader can trust what the paper *does* claim, even if the headline framing oversells.
**Evidence Anchor**: `text: §6.1 "no SMF/N4 implementation, GTP-U integration, PFCP rule handling"`

### S2: Sound fixed-point quantization with verified agreement
The Q16.16 MLP quantization produces identical predictions to the floating-point model on all 2,000 samples in the agreement test. For constrained satellite gateway or on-board processors where an FPU may be absent or power-costly, demonstrating that a 4-24-4 MLP can run purely in integer arithmetic at sub-microsecond latency is a legitimate and relevant result.
**Evidence Anchor**: `table: Table 7 — 99.1% accuracy, Float/fixed 100% agreement on 2,000 samples`

### S3: Credible software engineering of the RMT pipeline
The 4,200-line ISO C11 prototype with approximately 1,800 lines of tests, eight working P4 data planes, two-phase VLIW commit semantics, and a multi-queue parallel engine is substantial and verifiable. The repository is open and the tests are reproducible. This is not a toy prototype.
**Evidence Anchor**: `dataset: Repository — 4,200 LOC core, 8 P4 programs, ~1,800 LOC tests, all end-to-end verified`

### S4: Useful architectural positioning against related work
Table 1 carefully compares EdgeNet-CoM's architectural scope against flow-level ML, in-network ML, SDN/SD-WAN, and network coding categories without claiming accuracy or latency superiority, which is methodologically appropriate. The paper avoids the common pitfall of benchmarking against systems with different evaluation setups.
**Evidence Anchor**: `table: Table 1 — architectural positioning across 7 dimensions, no quantitative superiority claims`

### S5: Per-flow ordering preservation under multi-core scaling
Theorem 1 and its implementation (RSS 5-tuple hashing to SPSC rings) correctly guarantee that TCP packet ordering is maintained across workers. This is a non-trivial and important correctness property for any data-plane system that processes TCP traffic, and the proof is clean.
**Evidence Anchor**: `text: §4.5 Theorem 1 "per-flow ordering preservation under RSS 5-tuple hashing"`

---

## Weaknesses *

### W1: Parser assumes bare IP/TCP/TLS — real satellite UPF traffic arrives GTP-U encapsulated
**Problem**: The parser in p4/enc_classify.p4 traverses Ethernet → IPv4 → TCP → TLS (dport 443). In a real 3GPP 5G user plane, traffic between the gNB and UPF (N3 interface) and between UPFs (N9 interface) is carried inside GTP-U tunnels (UDP port 2152). The outer packet is Ethernet/IPv4/UDP/GTP-U, and the inner packet (which contains the actual TLS record) is only visible after GTP-U decapsulation. The parser as written would see the outer UDP header and stop — the tcp.dport == 443 condition would never trigger, and the TLS header would never be extracted.
**Evidence Anchor**: `absence: src/parser.c and p4/enc_classify.p4 — expected GTP-U header parsing (UDP dport 2152, TEID extraction, inner-header parsing); checked all .p4 files, all .c source files, include/rmt.h`
**Why it matters**: This is the single most critical deployment gap. Without GTP-U awareness, the classifier cannot see the inner TLS record in any real 5G deployment. The entire classification pipeline is inoperative at the N3/N9 interface where a satellite UPF would actually sit. The paper presents this as a future integration step (§6.2), but it is a fundamental architectural precondition.
**Suggestion**: At minimum, extend the P4 program and parser to handle GTP-U decapsulation (outer Eth/IP/UDP/GTP-U → inner IP/TCP/TLS) and re-run the accuracy and throughput tests. Alternatively, acknowledge that the classifier must sit at the SGi/N6 interface (post-decapsulation) and discuss the implications for UPF placement in the satellite gateway architecture.
**Severity**: Critical
**Confidence**: 5 — core expertise: 3GPP user-plane protocol stack

### W2: Network coding at UPF layer interacts with TCP congestion control — paper cites [26] but does not discuss the interaction
**Problem**: The paper applies systematic RLNC (25% redundancy for VoIP, 12.5% for Video) at the UPF, which hides packet losses from the end-to-end TCP connection. Coded TCP [26] was designed for this, but EdgeNet-CoM does not implement Coded TCP — it applies standard RLNC coding transparently to regular TCP flows. When a satellite link drops a packet that the RLNC repair packet recovers at the receiver, TCP never sees the loss and does not reduce its congestion window. Over a shared satellite link with limited capacity, this means TCP flows protected by UPF-level coding will not back off under congestion, potentially causing buffer overruns at the satellite gateway and starving unprotected flows.
**Evidence Anchor**: `text: §4.7.1 "proactive coding can trade bandwidth for recovery" and §2.4 citation of [26] Coded TCP`
**Why it matters**: The satellite community has extensively studied the TCP-spoofing/PEP problem. Hiding losses from TCP over satellite links is a known double-edged sword: it improves per-flow throughput but can cause congestion collapse on shared bottleneck links. The paper cites Coded TCP [26] as related work but does not discuss that its coding is applied at the UPF (a network-interior node) without TCP endpoint coordination, which is the exact scenario where TCP starvation occurs.
**Suggestion**: Add a discussion of TCP/coding interaction covering at least: (a) whether the receiver-side decoder signals losses to TCP or hides them, (b) how coding redundancy is bounded to prevent congestion masking, (c) whether Coded TCP's TCP-aware coding is more appropriate than transparent UPF-level coding. Consider referencing the satellite PEP literature (e.g., ETSI TR 101 984, RFC 3135).
**Severity**: Major
**Confidence**: 5 — core expertise: TCP over satellite, PEP design

### W3: No satellite link-layer interaction — coding may be redundant with DVB-S2X ACM
**Problem**: Modern satellite links (GEO and LEO) use DVB-S2X with Adaptive Coding and Modulation (ACM), which already provides link-layer FEC that adapts to instantaneous channel conditions. DVB-S2X MODCODs can achieve near-Shannon-limit performance with LDPC codes at frame error rates well below the 1-10% packet loss rates cited in §1.1. Applying RLNC at the UPF (a network-layer function) on top of DVB-S2X ACM (a link-layer function) creates two independent FEC layers operating on different timescales with no coordination. The paper does not discuss this interaction.
**Evidence Anchor**: `absence: §2 Related Work, §4.7 Coding, §6 Limitations — expected discussion of satellite link-layer FEC (DVB-S2X ACM, LDPC) and its interaction with UPF-level RLNC; checked all subsections of §2, §4.7, and §6.1`
**Why it matters**: If the satellite link's ACM already recovers most physical-layer losses, the UPF-level RLNC sees only the residual loss pattern (typically burst losses during deep fades or handovers), which has different statistics than the 1-10% i.i.d. loss model implicitly assumed. The redundancy ratios in Table 4 (25% for VoIP, 12.5% for Video) may be grossly over-provisioned for residual losses, wasting the very satellite bandwidth the system aims to protect. Alternatively, if the satellite link uses a lower MODCOD with higher residual loss, the coding parameters need to be jointly optimized with the ACM feedback — which the paper does not implement.
**Suggestion**: Discuss the satellite link-layer protection stack (DVB-S2X ACM + possible erasure coding at link layer) and position UPF-level RLNC relative to it. If the RLNC is intended to protect against residual post-ACM losses, characterize those losses (burst statistics, not i.i.d. rates) and justify the coding parameters accordingly. If it is intended for end-to-end protection independent of the link layer, state this explicitly and discuss the implications.
**Severity**: Major
**Confidence**: 5 — core expertise: DVB-S2X ACM, satellite link-layer design

### W4: Loss and delay figures in §1.1 are plausible but not sourced to measurements
**Problem**: The paper states satellite UPF paths exhibit "packet loss rates of 1-10%" (citing [33]) and specific delay ranges (GEO ~500 ms, LEO ~20-50 ms, ISL ~5-30 ms, TB ~5-50 ms). The delay figures are approximately correct for the stated path types, but the 1-10% loss range spans a very wide operational envelope. Modern DVB-S2X GEO links in clear-sky conditions achieve BER < 10^-8 after FEC, translating to packet loss rates well below 1%. The 1-10% figure is more representative of handover events, rain fade, or LEO beam transitions, which are bursty and time-varying — not the steady-state condition the evaluation models.
**Evidence Anchor**: `text: §1.1 "packet loss rates of 1-10% due to atmospheric attenuation, handover events, and RF impairments"`
**Why it matters**: The coding parameter selection in Table 4 (25% redundancy for VoIP) implies a steady-state loss environment at the high end of the stated range. If actual steady-state loss is <0.1% (clear-sky DVB-S2X), the coding overhead is wasteful. If loss is bursty at 1-10% during handovers, the generation size k=4 for VoIP may be too small to span a loss burst, and the coding would fail to recover. The paper needs to specify which loss regime it targets and justify the coding parameters for that regime.
**Suggestion**: Disaggregate the 1-10% range into operational regimes (clear-sky, rain fade, handover, beam transition) with cited measurements or 3GPP TR 38.821 channel models. Justify Table 4's coding parameters for the specific regime targeted.
**Severity**: Minor
**Confidence**: 4 — adjacent field: satellite channel characterization is well-established but the exact figures depend on frequency band, orbit, and ground-station location

### W5: Generation buffering (k=4 for VoIP) adds delay incompatible with EF treatment
**Problem**: VoIP is assigned DSCP 46 (EF, Expedited Forwarding) in Table 3 and the smallest generation size k=4 in Table 4. However, buffering 4 VoIP packets before emitting a repair packet introduces a generation-completion delay. At typical VoIP codec rates (e.g., G.711 at 50 packets/sec, one 20 ms frame per packet), accumulating 4 packets takes approximately 60-80 ms before the repair packet is emitted. ITU-T G.114 recommends one-way delay of 150 ms or less for acceptable conversational quality. The generation buffering alone consumes 40-53% of the ITU budget before any propagation, queuing, or processing delay is added.
**Evidence Anchor**: `table: Table 4 — VoIP k=4, n=5, with rationale "Max protection, low generation"`
**Why it matters**: The paper assigns VoIP the highest-priority DSCP marking (EF) while simultaneously introducing a coding delay that may push the total one-way latency beyond the conversational quality threshold. The trade-off between loss protection and added delay is not analyzed. For a latency-sensitive service, the coding decision must account for the delay budget, not just the redundancy ratio.
**Suggestion**: Add a delay analysis for the VoIP coding profile: generation-completion time at typical codec rates, total one-way delay budget including satellite propagation, and the resulting end-to-end conversational quality. Consider whether k=2 or k=3 with higher per-generation redundancy would better serve the latency constraint.
**Severity**: Major
**Confidence**: 4 — core expertise: VoIP delay budgets; minor uncertainty on exact codec rates assumed

### W6: DSCP vs. QFI mismatch in a real 5G UPF — the paper conflates two distinct QoS mechanisms
**Problem**: The prototype writes DSCP values (0, 32, 46, 10) to the IPv4 ToS byte. In a real 3GPP 5G UPF, the QoS mechanism is the QoS Flow Identifier (QFI), carried in the GTP-U header extension, not DSCP. DSCP is used for DiffServ treatment on the transport network between UPF and DN, but the QFI governs radio-resource scheduling and 5G QoS enforcement. The paper acknowledges this in §6.2 ("DSCP and QFI are distinct fields governed by different policies") but does not discuss how the classifier's output would map to QFI, how QFI is authorized by the SMF via PFCP, or how a locally inferred class interacts with SMF-authorized QoS policy.
**Evidence Anchor**: `text: §6.2 "DSCP and QFI are distinct fields governed by different policies"`
**Why it matters**: A satellite UPF cannot unilaterally assign QoS treatment based on local classification. The SMF authorizes QoS flows and their parameters (5QI, ARP, session-AMBR) via PFCP sessions. If the classifier infers "VoIP" and marks DSCP EF, but the subscriber's authorized QoS profile does not include a VoIP-dedicated flow, the UPF must either map to an existing authorized flow or trigger a PFCP session modification — neither of which the prototype supports.
**Suggestion**: Expand §6.2 to describe the specific integration path: how classifier output maps to QFI, how unauthorized QoS upgrades are handled, and how the local inference interacts with SMF policy. This need not be implemented, but the architectural gap should be precisely characterized.
**Severity**: Minor
**Confidence**: 5 — core expertise: 3GPP QoS architecture

### W7: No satellite-specific mechanism is evaluated — Mode B routing unimplemented, no link feedback, no channel model
**Problem**: The paper's satellite-specific contributions reduce to: (a) a motivating scenario (§1.1), (b) a path taxonomy with QoS profiles (§3.1), (c) a proposed but unimplemented Mode B class-aware multi-uplink routing (§4.6.2), and (d) a network-coding harness that operates in memory without a satellite channel model. None of the satellite-specific mechanisms are implemented or evaluated. The evaluation measures only the data-plane pipeline on synthetic traffic in memory.
**Evidence Anchor**: `text: §4.6.2 "Mode B (proposed: class-aware multi-uplink mapping) would extend the route key… Neither the composite route key nor dynamic link monitoring is implemented"`
**Why it matters**: The paper's title and abstract promise "service-aware in-network computing for satellite user plane functions," but the evaluation validates only a generic programmable data plane with classification and coding. The satellite framing is a narrative overlay, not an evaluated contribution. A reader from the satellite community would find no actionable result about satellite system performance.
**Suggestion**: Either (a) implement and evaluate at least one satellite-specific mechanism (e.g., Mode B routing with a simple two-path model using a basic loss/latency simulator, or a comparison of coding policy under a DVB-S2X residual-loss trace), or (b) restructure the paper to present the satellite scenario as a motivating use case while positioning the contribution as a programmable data-plane framework with a satellite-inspired evaluation.
**Severity**: Major
**Confidence**: 5 — core expertise: satellite system evaluation

### W8: On-board deployment claims unsupported by any energy, power, or radiation measurement
**Problem**: The paper repeatedly motivates the design for "constrained processing environments" with "power, thermal, and radiation constraints" (§1.1, §4.3.2) and states that Q16.16 integer arithmetic is "suitable for constrained satellite gateway processors" (§3.2). However, §6.1 admits "the evaluation does not measure energy per packet or execute on a satellite-grade processor. No energy-efficiency or radiation-tolerance conclusion can be drawn from the use of integer arithmetic alone." The benchmark runs on an Intel Xeon E5-2680 v4 — a 120 W server processor — not a space-grade or even radiation-tolerant SoC.
**Evidence Anchor**: `text: §6.1 "No energy-efficiency or radiation-tolerance conclusion can be drawn"`
**Why it matters**: The claim that integer-only inference is beneficial for constrained satellite hardware is intuitively plausible but entirely unvalidated. Modern space-grade processors (e.g., Cobham Gaisler GR740, BAE RAD750) run at 100-400 MHz with very different memory hierarchies than a Xeon. The 933 ns service time measured on the Xeon does not translate to any space-grade platform without characterization.
**Suggestion**: Remove or substantially qualify claims about constrained processing environments. If the claim is retained, provide at minimum a power-per-inference measurement on a representative embedded platform (even an ARM Cortex-A72 would be more relevant than a Xeon).
**Severity**: Minor
**Confidence**: 4 — adjacent field: on-board processing constraints are well-known but platform-specific

---

## Detailed Comments *

### Title & Abstract
- The title "Service-Aware In-Network Computing for Satellite User Plane Functions" accurately describes the *aspiration* but overstates the *demonstration*. The prototype is a generic software RMT pipeline with no satellite-specific or UPF-specific functionality. A more accurate title would reference programmable data planes with classification and coding for edge network applications.
- The abstract is well-structured and honest about scope ("establish implementation feasibility; accuracy on operational traffic, integrated repair-packet emission, and performance with real NIC and 5G integration remain open validation tasks"). This honesty partially mitigates the title's overreach.

### Introduction
- §1.1's three differentiators (heterogeneous paths, elevated loss, constrained processing) are plausible for satellite UPFs but are presented as established facts without operational measurement references. The 1-10% loss range in particular needs disaggregation by operational regime.
- The "architectural tension" framing (§1.1) between forwarding and intelligence planes is well-articulated and genuine. The paper correctly identifies a real gap in how programmable data planes could be applied to edge functions.
- §1.2's five challenges are reasonable design requirements, though Challenge 5 (service-aware actuation across heterogeneous paths) is only partially addressed (Mode A destination routing, not class-aware path selection).

### Methodology / Research Design
- The system model (§3.1) defines four path types with QoS profiles but does not implement any of them in the prototype. The path taxonomy is purely motivational.
- The synthetic feature model (§3.4, Table 2) uses deliberately separated uniform distributions that make classification nearly trivial (VoIP and Bulk are perfectly separable). The 99.1% accuracy must be read against this generator, not as a measure of classifier quality.
- The optimization objective (§3.2) is well-formulated but explicitly acknowledged as "a policy target rather than an optimizer implemented by the prototype." This is honest but raises the question of why it is included if it is not evaluated.

### Results / Findings
- The classification results (Table 7) are valid for the synthetic generator but do not generalize. The paper correctly notes this.
- The throughput results (Table 8) are solid for an in-memory software pipeline. The 933 ns PGO result and 2.75 Mpps at 16 workers are credible.
- The multi-core scaling (Table 9) shows sub-linear scaling with declining efficiency (16% at 16 workers on 8 vCPUs), which is expected for oversubscription. The paper correctly does not claim near-linear scaling.
- The network coding evaluation (§5.10) validates arithmetic correctness and encode/decode functionality but does not evaluate coding performance under any loss model — satellite or otherwise.

### Discussion
- §6.1's limitations section is one of the strongest parts of the paper. It is unusually candid and comprehensive.
- §6.2's deployment considerations correctly identify the GTP-U/PFCP/N4 gap but understate its severity — this is not a "practical deployment consideration" but a fundamental architectural precondition for any 5G UPF.
- §6.3's ethics paragraph on net neutrality is appropriate but brief. For service-aware forwarding that could preferentially route traffic, additional stakeholder perspectives (ground-station operators, spectrum regulators, end users) would strengthen the discussion. The paragraph does not address the regulatory status of traffic classification at a satellite gateway, which may be subject to national telecommunications regulations.

### Conclusion
- The conclusion (§7) is well-calibrated: "These measurements support the feasibility of coupling inference and forwarding logic in a compact software pipeline. They do not establish accuracy on operational traffic, NIC-level throughput, energy efficiency, radiation tolerance, or 3GPP UPF conformance." This is accurate.

### References
- The 3GPP references ([28], [29]) are appropriate (TR 38.821 Release 17, TR 38.822 Release 18).
- The satellite network coding references ([34], [35]) are relevant.
- Missing: any reference to 3GPP TS 29.281 (GTP-U protocol specification), TS 29.244 (PFCP specification), or TS 23.501 (5G system architecture), which are the foundational standards for any UPF implementation.
- Missing: any reference to DVB-S2X (ETSI EN 302 307-2) or satellite link-layer ACM.

---

## Questions for Authors *

1. **GTP-U encapsulation**: At a real 5G satellite UPF's N3 interface, user-plane packets arrive as Ethernet/IP/UDP/GTP-U/inner-IP/TCP/TLS. The current parser stops at the outer UDP (port 2152) and never reaches the inner TLS record. How would the classification pipeline operate on real UPF traffic? Would GTP-U decapsulation be added to the parser, or would the classifier need to be placed at the N6/SGi interface post-decapsulation?

2. **TCP/coding interaction**: When UPF-level RLNC recovers a lost packet at the receiver, the TCP sender never observes the loss and does not reduce its congestion window. Over a capacity-constrained satellite link shared with unprotected flows, could this cause congestion collapse? How does the paper's coding design relate to Coded TCP [26], which was specifically designed to make TCP aware of network coding?

3. **Link-layer redundancy**: Modern satellite links use DVB-S2X ACM with LDPC codes that achieve near-Shannon-limit performance. If the link layer already provides FEC, what is the residual loss pattern that UPF-level RLNC is designed to protect against? Is it steady-state random loss (likely <0.1%) or burst loss during handovers/fades?

4. **VoIP coding delay**: With k=4 generation size and typical VoIP codec rates (~50 pps), the generation-completion delay is ~60-80 ms. Given ITU-T G.114's 150 ms one-way delay budget for conversational quality and the additional satellite propagation delay (~250 ms GEO, ~30 ms LEO), does the coding delay budget leave sufficient margin for acceptable VoIP quality?

---

## Minor Issues

### Language / Grammar
- §1.1: "satellite-aerial-terrestrial integrated links" — the term "aerial" is unusual in the NTN context; consider "satellite-terrestrial" or cite a specific source that uses this terminology.
- §4.7.1: "The cost of recovery depends on both path RTT and application requirements" — consider "round-trip time" spelled out on first use in this section.

### Citation Format
- Reference [3] (Google Transparency Report) is a web source without an access date; consider adding one for reproducibility.
- Reference [38] (nDPI) is similarly a web source without access date.

### Figures and Tables
- Figure 1's ASCII diagram is functional but would benefit from a proper figure in the camera-ready version, particularly to show the boundary between the implemented and proposed (dashed) components more clearly.
- Table 4 could add a column showing the generation-completion delay at typical codec rates, to make the latency trade-off explicit.

### Layout
- §3.2's optimization objective is mathematically well-presented but could be more clearly marked as "not implemented" in the section heading or an upfront note, to prevent readers from assuming it describes the prototype's behavior.

---

## Dimension Scores *

| Dimension | Score (0-100) | Descriptor | Notes |
|-----------|--------------|------------|-------|
| Originality (20%) | 72 | Adequate | The integration of in-pipeline MLP classification with routing and coding is novel for programmable data planes. The satellite framing adds motivational novelty but not architectural novelty, as no satellite-specific mechanism is implemented. |
| Methodological Rigor (25%) | 62 | Adequate | The synthetic evaluation is methodologically sound for what it tests (pipeline mechanism under controlled conditions). However, the absence of any satellite channel model, GTP-U handling, or real traffic evaluation means the methodology does not address the paper's stated domain. No confidence intervals or significance tests. |
| Evidence Sufficiency (25%) | 58 | Weak | The evidence supports the data-plane pipeline claims but not the satellite UPF claims. All results are on synthetic traffic in memory. No satellite link simulation, no GTP-U traffic, no real captures, no link-layer interaction. The 38 references are adequate in count but missing key 3GPP protocol specifications (TS 29.281, TS 29.244). |
| Argument Coherence (15%) | 74 | Adequate | The paper's internal logic is coherent: it identifies a gap, proposes a framework, implements a prototype, and evaluates it. The coherence breaks at the boundary between the satellite framing (which promises heterogeneous path selection and satellite-aware coding) and the evaluation (which tests none of these). |
| Writing Quality (15%) | 82 | Strong | The prose is clear, precise, and well-structured. The honest limitation disclosure is commendable. Minor terminology issues ("aerial" in satellite context). |
| Significance & Impact (optional) | 55 | Some implications but narrowly scoped | The programmable data-plane contribution has moderate impact for the networking community. The satellite impact is near-zero because no satellite-specific result is demonstrated. The interdisciplinarity is juxtaposition (satellite terminology wrapping a generic data-plane paper) rather than genuine integration. |
| **Weighted Average** | **67** | **Minor Revision (borderline Major)** | The weighted score of 67 maps to Minor Revision by the rubric, but the satellite framing gap is substantial enough that Major Revision is the appropriate recommendation. The score reflects strong data-plane engineering partially offset by the domain mismatch. |

---

## Devil's Advocate Review

### Strongest Counter-Argument

The paper's central claim — that EdgeNet-CoM demonstrates "two coupled service-aware decisions" driven by a single classification result — overstates what the artifact actually implements. Of the two decisions the paper attributes to the classifier, only one (DSCP marking) executes inside the RMT pipeline. The second decision, coding-profile selection, runs in an external C harness (`nc_demo.c`) that invokes `rmt_process()` and then, *after the pipeline returns*, inspects the written DSCP byte to select a coding profile and increment a generation counter. No repair packet is ever constructed within the pipeline, no coding header is written, and no repair packet is injected into the deparser output. The 12.3% "projected repair ratio" (Table 16, abstracted into the Abstract) is a deterministic arithmetic consequence of dividing `floor(classified_count / k)` for each inferred class by 10,000 — it measures bookkeeping, not loss recovery. The comparison "51% lower than uniform 25%" compares two author-chosen configurations, not the system against an adaptive coding baseline or any measured channel loss.

Simultaneously, the 99.1% accuracy that anchors the classifier's credibility is an artifact of generator design, not model quality. Table 6 defines four near-disjoint boxes in 4-D feature space: VoIP occupies ipv4.len in [60,200] and tcp.window in [4096,16384]; Bulk occupies [1400,1500] x [60000,65535]. Three of the four dimensions have ranges that are either non-overlapping or only marginally overlapping between classes. A trivial rule-based classifier (e.g., `if ipv4.len < 200 and tcp.window < 16384 then VoIP`) would achieve comparable accuracy on this generator without any training. The paper acknowledges "deliberately separated ranges" (Section 5.2.1) and reports no baseline (Section 5.7), but the Abstract frames 99.1% as a result rather than as a tautological property of the data model.

Finally, the throughput ceiling of 2.75 Mpps at 16 workers (2.77 Gbps at 126 B) reaches only the low end of the paper's own motivating range of "1-10 Gbps" (Section 2.1). At 16% scaling efficiency on an oversubscribed 8-vCPU host, the curve is flattening sharply; extrapolating linearly, reaching 10 Gbps would require approximately 58 workers at the same diminishing marginal returns, on a host with enough physical cores to avoid the oversubscription penalty that already dominates at 16. The Abstract's framing of 2.75 Mpps alongside the 1-10 Gbps motivational range creates an implicit scalability promise the data does not support.

### Issue List

#### CRITICAL

| # | Dimension | Issue Description | Evidence Anchor | Confidence | Field-Norm Boundary | Evidence-Crossing Rationale |
|---|-----------|-------------------|-----------------|------------|---------------------|-----------------------------|
| C1 | Core Thesis Challenge | Of the "two coupled service-aware decisions" (Contribution 3, Abstract), only DSCP marking executes inside the pipeline. Coding-profile selection and repair-decision counting occur in nc_demo.c *after* rmt_process() returns — the harness reads out.data[15] (the DSCP byte), looks up coding_table[cls], and increments a counter. No repair packet is constructed in the pipeline path, no coding header is inserted, and no packet is emitted from the deparser. The Abstract and Conclusion frame this as "a single classification result drives QoS marking, routing, AND coding," but the third actuation is test scaffolding, not pipeline behavior. | text: "a single classification result controls DSCP marking in the RMT pipeline and coding-profile selection in the evaluation harness" — Section 1.4 Contribution 3; absence: src/nc_demo.c — expected repair-packet construction or deparser injection within the RMT pipeline; checked rlnc_encode, xor_encode, test_joint_pipeline | 5/5, code-audit basis | Systems-paper norm: a claimed data-plane actuation must execute within the data-plane fast path, not in post-hoc test code. Standard for SIGCOMM/NSDI/P4 workshop papers. | The paper explicitly states "Repair decisions are counters" (Table 16 caption) and "the joint benchmark does not inject the optional repair packet into the RMT output path" (Algorithm 3 note). |
| C2 | Confirmation Bias Detection | The 99.1% accuracy is measured on data drawn from the same class-conditional uniform distributions the authors designed, with "deliberately separated ranges" (Section 5.2.1). The VoIP box (ipv4.len in [60,200], tcp.window in [4096,16384], tls.length in [20,160]) is disjoint from Bulk ([1400,1500], [60000,65535], [14000,16384]) in three dimensions simultaneously. A hand-written if-else rule would achieve similar accuracy without training. No baseline of any kind is reported (Section 5.7). The 99.1% is evidence of generator separability, not classifier quality. | text: "deliberately separated ranges make this a controlled feasibility dataset" — Section 5.2.1; table: Table 6, VoIP row vs Bulk row — three of four feature ranges are non-overlapping | 5/5, ML evaluation basis | ML evaluation norm: reporting accuracy without any baseline (trivial rule, classical ML, or production tool) on the same data renders the number uninterpretable as evidence of model quality. | The paper acknowledges no baseline exists and frames 99.1% in the Abstract as a result. |
| C3 | Logic Chain Validation | The Abstract states the configured policy yields a projected 12.3% repair ratio versus 25% for the selected uniform baseline. This is not a measured outcome — it is floor(2499/8) + floor(2369/8) + floor(2505/4) = 312 + 296 + 626 = 1234 divided by 10,000, which equals 12.34% by construction given Table 4's profiles and Table 16's inferred class counts. The "51% lower" comparison divides (25 - 12.3)/25. No packet loss is simulated, no repair packet is transmitted, and no receiver attempts recovery. The Abstract frames this arithmetic identity as if it were an experimental result. | table: Table 16 — "Projected repair overhead 1,234 / 10,000 = 12.3%"; text: "the configured policy yields a projected repair ratio versus 25% for the selected uniform baseline" — Abstract | 5/5, arithmetic basis | Systems evaluation norm: a "repair ratio" presented as a result must correspond to measured loss recovery or at minimum simulated loss injection. | The 12.3% is fully determined before any experiment runs. The single run producing class distribution 2499/2369/2505/2627 does not test the policy — it applies it. |

#### MAJOR

| # | Dimension | Issue Description | Evidence Anchor | Confidence | Field-Norm Boundary | Evidence-Crossing Rationale |
|---|-----------|-------------------|-----------------|------------|---------------------|-----------------------------|
| M1 | Overgeneralization Check | The throughput ceiling of 2.75 Mpps at 16 workers yields approximately 2.77 Gbps (2.75e6 x 126 x 8). The paper motivates the design for "1-10 Gbps" satellite links (Section 2.1) and sets T_budget < 1 us "for Gbps-rate forwarding" (Section 3.2). The measured peak reaches only the low end of this range. At 16% scaling efficiency with 16 workers on 8 vCPUs (oversubscribed), the speedup curve is sharply concave: 1->2 workers yields 1.48x, 8->16 yields only 1.26x marginal. | table: Table 9 — 16 workers, 2.57x speedup, 16% efficiency; text: "link rates are typically 1-10 Gbps" — Section 2.1 | 4/5, performance analysis basis | Systems performance norm: when a paper motivates a design for a stated throughput range, the evaluation must demonstrate reachability of at least the upper bound or explicitly bound the claim. | The Abstract juxtaposes "2.75 Mpps" with the 1-10 Gbps motivation without noting 2.75 Mpps at 126 B covers only ~2.8 Gbps. |
| M2 | Core Thesis Challenge | The "runtime table updates without data-plane restart" claim (Section 1.3, 1.4) has no thread-safety mechanism. rmt_load_entries() in runtime.c directly modifies trie structures via rmt_table_insert() without any mutex, rwlock, RCU, or atomic operations. rmt_dnn_load() in dnn.c overwrites weight arrays non-atomically. Calling either function while workers are executing rmt_process() would produce a data race. | absence: src/runtime.c, src/pipeline.c, src/match.c — expected mutex, rwlock, RCU, or atomic synchronization for concurrent table modification; checked grep for mutex, rwlock, rcu, atomic, lock in runtime.c and pipeline.c | 4/5, code-audit basis | Systems concurrency norm: a "runtime update" claim for a multi-threaded data plane requires demonstration of concurrent safety. | The paper states "Models, table entries, and action parameters can be updated at runtime" (Section 1.3 Pillar 3) and "runtime reconfiguration without data-plane restart" (Challenge 3). |
| M3 | Cherry-Picking Detection | Table 5 LOC counts are materially inaccurate for several modules. Audited with wc -l: parser.c = 187 (claimed ~350, 47% understated); alu.c = 398 (claimed ~280, 42% overstated); p4c.c = 933 (claimed ~1200, 22% overstated); dnn.c = 163 (claimed ~200, 18% overstated); pipeline.c = 368 (claimed ~250, 47% overstated). The aggregate "approximately 4,200 lines" (Abstract, Section 5.1) versus the Table 5 sum of ~3,745 and the actual nine-file total of 3,518 represents a ~19% inflation. | table: Table 5 vs wc -l audit — parser.c 187 vs ~350, alu.c 398 vs ~280, p4c.c 933 vs ~1200, pipeline.c 368 vs ~250 | 5/5, direct measurement basis | Artifact accuracy norm: reported LOC in a systems paper must match wc -l within the stated approximation tolerance. | |
| M4 | Logic Chain Validation | The five challenges (Section 1.2) and five pillars (Section 1.3) exhibit a 1:1 structural circularity: each challenge is defined to be answered by exactly one pillar. Challenge 5 ("Service-aware actuation across heterogeneous paths") and Pillar 5 ("Service-Aware Actuation Layer") share nearly identical wording. This is not an independent requirements derivation — it is reverse-engineering of the system's existing features into challenge language. | text: "Challenge 5: Service-aware actuation across heterogeneous paths" — Section 1.2; text: "Pillar 5: Service-Aware Actuation Layer" — Section 1.3 | 4/5, structural analysis basis | Research methodology norm: when challenges and solutions exhibit 1:1 structural isomorphism, the derivation is likely post-hoc. | |
| M5 | Overgeneralization Check | The paper is framed as a satellite UPF system, but no experiment depends on satellite-specific characteristics. The synthetic generator uses no satellite traffic model; the routing is destination-based LPM with no satellite path selection (Mode B is explicitly not implemented, Section 4.6.2); the coding harness does not simulate satellite loss profiles; no RTT, handover, beam, or constellation parameter appears in any experiment. Replacing "satellite" with "edge gateway" throughout Sections 4-5 would not change a single result. | absence: Sections 4-5 — expected any experiment parameterized by satellite link characteristics (RTT, loss rate, beam geometry, handover); checked Table 2, Table 6, Table 9, all experimental parameters | 5/5, content analysis basis | Systems-paper norm: when a paper's title and motivation center on a specific deployment context, the evaluation must include at least one experiment that depends on properties of that context. | The title, Abstract, and Section 1.1 all invoke satellite-specific language, but the prototype is a generic in-memory packet processor. |
| M6 | Alternative Paths Analysis | Section 5.7 explicitly states "the artifact does not contain a reproducible classical or production-tool baseline on the same workload." No comparison is made against nDPI [38], a simple rule-based classifier on the same generator, or any published encrypted-traffic classifier on a shared dataset. Without at least a trivial baseline on the same data, the 99.1% accuracy and 933 ns latency have no reference point. | text: "the artifact does not contain a reproducible classical or production-tool baseline on the same workload" — Section 5.7 | 5/5, evaluation completeness basis | ML-in-systems evaluation norm: even feasibility studies must report at least a trivial baseline on the same data to contextualize accuracy claims. | |

#### MINOR

| # | Dimension | Issue Description | Evidence Anchor | Confidence |
|---|-----------|-------------------|-----------------|------------|
| m1 | Logic Chain Validation | The P4 front end (p4c.c, 933 LOC) handles a "P4-14 flavored subset" supporting only header, metadata, parser, action, table, and control ingress — no extern functions, registers, counters, meters, or action profiles. The "P4-inspired" label could mislead readers into assuming P4_16 compatibility. | text: "P4-14 flavored subset" — p4c.c line 4 | 4/5, code-audit basis |
| m2 | Overgeneralization Check | The "approximately 40% per-stage execution time reduction" from computed-goto dispatch (Section 4.4) is stated without a measurement reference. No before/after comparison or instrumentation methodology is described. | text: "this optimization reduces per-stage execution time by approximately 40%" — Section 4.4 | 3/5, claim verification basis |
| m3 | Logic Chain Validation | Algorithm 1's fixed-point arithmetic is internally consistent (verified: Q16.16 bias << 16 = Q32.32 accumulator, Q16.16 x Q16.16 = Q32.32 product, >> 16 returns Q16.16). However, dnn.c initializes best_v = 0 (line 134) rather than negative infinity for argmax, meaning if all output logits are negative, class 0 is always selected regardless of which logit is largest. This is a correctness bug for edge cases. | text: "int64_t best_v = 0;" — src/dnn.c line 134 | 4/5, code-audit basis |
| m4 | "So What?" Test | The 220-parameter, 4-class MLP with 4 inputs trained on synthetic data has limited practical significance. Modern encrypted traffic classification targets 10-100+ application classes with features including inter-arrival time, direction, and burst patterns. | text: "cannot distinguish sub-application types within a class" — Section 6.1 | 3/5, scope assessment basis |

### Ignored Alternative Explanations/Paths

1. **Trivial rule-based classifier as an equally valid solution.** Given the deliberately separated feature ranges (Table 6), a hand-written decision tree on ipv4.len and tcp.window would achieve comparable accuracy with zero training cost, zero quantization risk, and far simpler runtime verification. The paper never tests this alternative, which would undermine the motivation for embedding an MLP in the data plane.

2. **Flow-level classification as a more practical alternative.** The paper dismisses flow-level approaches because they "delay a decision until enough packets have arrived" (Section 2.2), but its own classifier only fires on TLS application-data records (tls.type == 23) on port 443, which already requires the handshake to have completed. A flow-level approach observing the first few packets could extract richer features with minimal additional delay and substantially better accuracy on real traffic.

3. **Adaptive link-aware coding as a more effective loss-recovery strategy.** The paper motivates network coding for satellite links based on loss rates (Section 4.7.1) but then selects coding profiles based solely on the inferred service class, ignoring measured link state. Existing adaptive coding schemes [34], [35] adjust redundancy based on real-time channel estimation, which would likely outperform a static class-to-profile mapping.

### Missing Stakeholder Perspectives

- **Satellite network operators** who would deploy a UPF: the paper does not address operational concerns such as integration with existing satellite ground-station infrastructure, interaction with link-layer FEC already present in DVB-S2X or similar standards, or the cost-benefit of application-layer coding when lower-layer protection exists.
- **End users** whose traffic is classified: the paper notes adversarial robustness (Section 6.1) and net neutrality (Section 6.3) but does not discuss what happens when misclassification assigns a latency-sensitive flow to a Bulk profile (no coding, AF11 DSCP).
- **3GPP standards bodies** whose specifications govern UPF behavior: the paper mentions N4/PFCP integration as future work but does not discuss whether per-packet inference at the UPF is compatible with 3GPP's session-level QoS model.

### Unexamined Premise

The paper assumes that placing intelligence at the packet-processing granularity within the data plane is architecturally superior to a flow-level classifier that communicates results to the data plane via a table update. This assumption is never tested: no comparison is made between (a) per-packet MLP inference at 933 ns/pkt and (b) flow-level classification with a table-installation delay of, say, 100 us for the first packet, after which all subsequent packets in the flow are forwarded at the baseline LPM cost. The "in-pipeline" architecture is presented as the only option, but the trade-off between first-packet latency and steady-state throughput is never quantified. For long-lived flows (video streaming, bulk transfer), the flow-level approach would amortize its setup cost over thousands of packets and could use richer features, potentially achieving both higher accuracy and higher aggregate throughput.

### Observations (Non-Defects)

- The paper is unusually transparent about its limitations. Section 6.1 explicitly names synthetic evaluation scope, TCP-only coverage, adversarial robustness, and the absence of satellite integration — more so than is typical for systems papers in this space.
- The Q16.16 fixed-point implementation and its exact agreement with the float32 model on 2,000 samples is a genuine implementation achievement, even if the model itself is trained on synthetic data.
- The multi-queue engine's load-balancing quality (Jain index 0.997, Table 10) and the per-flow ordering guarantee (Theorem 1) are well-engineered framework properties that would be valuable regardless of the classification application.
- The GF(2^8) arithmetic verification (Table 13) and RLNC/XOR encode-decode tests (Table 14) demonstrate coding correctness rigorously, even though the coding is not integrated into the packet pipeline.
- Algorithm 1's fixed-point arithmetic is internally consistent upon manual verification of the Q-format propagation through accumulator initialization, multiply-accumulate, and shift-back operations.
- The internal arithmetic of Table 16 is correct: 312/2499 = 12.49%, 296/2369 = 12.49%, 626/2505 = 24.99%, total 1234/10000 = 12.34%.

---

**Overall Recommendation (adversarial-perspective input):** Major Revision

**Confidence:** 4/5 — based on direct code audit of the artifact repository against manuscript claims, with all LOC counts, coding harness behavior, and synchronization mechanisms verified against source files.

**Rationale:** The paper's three quantitative headline claims — 99.1% accuracy, 12.3% repair ratio, and 2.75 Mpps throughput — are internally consistent and arithmetically correct, but each is substantially weaker than its Abstract framing suggests. The accuracy is a property of the generator, not the model; the repair ratio is a deterministic function of author-chosen parameters; and the throughput reaches only the low end of the motivating range with sharply declining scaling efficiency. The most critical structural issue is that the "two coupled service-aware decisions" contribution (Contribution 3) consists of one real pipeline action (DSCP marking) and one external-harness counter increment — yet the Abstract and Conclusion frame both as pipeline-driven actuation. Addressing this requires either (a) integrating the coding module into the RMT deparser so that repair packets are actually constructed and emitted within the pipeline, or (b) reframing Contribution 3 to accurately describe the coding harness as a validation of policy coupling rather than a demonstrated pipeline behavior. The paper also needs at minimum a trivial rule-based baseline on the same generator to contextualize the 99.1% figure, and accurate LOC counts from wc -l rather than approximate values that deviate by 22-47% on individual files.
