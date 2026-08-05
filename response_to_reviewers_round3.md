# Response to Reviewers — Round 3 Revision

**Manuscript:** EdgeNet-CoM: Service-Aware In-Network Computing toward Satellite User Plane Functions
**Basis:** `review_round3_report.md` (5-reviewer panel, 2026-08-04)
**Revision date:** 2026-08-04

All changes below were executed in this revision. New experimental numbers were measured on the revision artifact (i5-13420H, gcc 13.3, Linux 7.0.0-28) with the median-of-runs protocol stated in §5.2; code changes are in the repository and pass the full regression suite (`scripts/bench_all.sh`, 0 failures).

---

## Required Revisions (R1–R13)

### R1: Correct all LOC claims — **DONE**
- **Action**: Re-measured every module with `wc -l` (physical lines, documented in §5.1). Corrected Table 5, abstract, §1.3, §5.1.
- **Changes**: Table 5 now lists 11 core modules totaling **3,510** lines (previously claimed ~4,200); application modules 1,493 (`enc_classify_demo.c` 644, `nc_demo.c` 849); tests 1,886. `p4c.c` corrected to 933 (was ~1,200), `parser.c` to 187 (was ~350), `alu.c` to 398, `pipeline.c` to 368, `dnn.c` to 168; `runtime.c` (212), `phv.c` (21), `main.c` (182) added as explicit rows.

### R2: Repair the performance narrative (PGO claim) — **DONE**
- **Action**: Withdrew the unreproducible "~6,300 ns baseline" row. Table 8 now reports the classification pipeline under the two Makefile-defined builds, each as median (range) of five runs: `-O3 -flto` single 600 ns (597–636), batch 611 ns (594–765); `+PGO` single 592 ns (475–614), batch 571 ns (478–594).
- **Changes**: §5.4 states the isolated PGO contribution on this workload (1.3% single, 6.5% batch), notes the Makefile's "10–20%" refers to the SRv6 profile-collection workload, and explains the withdrawal. The Makefile's `pgo` target was also fixed to build `enc_classify_demo` and `nc_demo` (previously omitted), making the PGO configuration reproducible.

### R3: Add a non-neural baseline — **DONE**
- **Action**: Implemented `rule_classify()` in `src/enc_classify_demo.c`: a four-rule threshold cascade derived from the configured ranges of Table 6 (documented verbatim in §5.3.3), evaluated on the identical 4,000 Protocol A feature vectors.
- **Result**: rule baseline 3,907/4,000 = **97.68%** (web recall 91.0%, video 99.5%, voip 100%, bulk 100%); MLP advantage **1.42 pp**, concentrated in the web class; rule decision cost 1.7 ns vs ~592 ns pipeline cost. §5.3.3 and §5.7 now frame the MLP as an accuracy-per-cost choice. (Note: a rule baseline existed in the Round-1 artifact and had been removed; it is restored and documented.)

### R4: Confusion matrix and error-direction analysis — **DONE**
- **Action**: `enc_classify_demo.c` now records and prints the 4×4 confusion matrix and per-class precision during the Protocol A batch.
- **Result** (§5.3.2): 34 video→bulk errors, 1 video→web, 1 web→video; **no VoIP errors in either direction**. Operational analysis: the dominant direction demotes video (CS4→AF11, coding removed); the worst direction (VoIP→bulk) was not observed; bulk precision 96.8%.

### R5: Variance across seeds — **DONE**
- **Action**: Implemented Protocol B in `enc_classify_demo.c`: five seeds {42,…,46}, each retrained with `srand(seed)` and evaluated on 4,000 packets from an independent stream `srand(seed+7919)`.
- **Result** (§5.3.4, Table 7b): per-seed 97.92–99.03%; mean **98.53% ± 0.39%** (population std). Table 12's T2 criterion restated accordingly (Protocol A ≥ 99%; Protocol B mean ≥ 95%, std ≤ 1 pp). Both hold.

### R6: Integrate the calibrated-distribution results — **DONE**
- **Action**: Re-ran `scripts/bench_real_traffic.sh` (verified reproducible) and added §5.3.5 with Table 7c.
- **Result**: overall **97.0%** (3,879/4,000); web 96.3%, video 91.6%, voip 100%, bulk 100%; mean latency 560 ns; 1.79 Mpps. The section states precisely that the workload is Gaussian-calibrated synthetic traffic and that `pcap_test` uses a sibling trainer (6,000 samples, 50 epochs, lr 0.01, best-training-accuracy selection).

### R7: Reconcile satellite framing with evidence — **DONE (reframing path)**
- **Action**: Title changed to "… **toward** Satellite User Plane Functions". Abstract, §1.1, §1.3, and §5.2 state that the satellite UPF is a motivating scenario and that no satellite-parameterized experiment is included. §1.1 loss figures disaggregated by regime (clear-sky residual < 10⁻³ post-FEC; 1–10% bursts during rain fade/beam transition/handover). Future-work item 2 commits to satellite-trace evaluation (netem GEO/LEO profiles or TR 38.821 channel models).

### R8: GTP-U architectural gap — **DONE (N6 placement path)**
- **Action**: §1.1 adds a deployment-point note (N3/N9 carry GTP-U on UDP 2152; inner headers visible only after decapsulation). §4.2 adds an "Encapsulation boundary" paragraph: the parser stops at the outer UDP header on N3/N9; the evaluated prototype targets the post-decapsulation **N6 reference point**; a GTP-U decap stage (outer IPv4/UDP parse, TEID extraction, inner re-parse) is specified as future work (§6.4 item 2). §6.2 states the same precondition for production placement. 3GPP TS 29.281 / TS 29.244 / TS 23.501 added as references [45]–[47].

### R9: Reframe Application B — **DONE**
- **Action**: §1.3 presents "two components of differing integration depth": Application A (pipeline-resident) and Application B (**connected validation harness**). Contribution 3 (§1.4) now reads "one in-pipeline and one harness-level decision". §4.7.4 states the boundary explicitly; Tables 16/18 retain "repair decisions are counters" disclaimers.

### R10: Runtime-update concurrency disclosure — **DONE**
- **Action**: §6.1 adds "Runtime update concurrency": `rmt_load_entries()`/`rmt_dnn_load()` mutate trie nodes and weight arrays without synchronization; current semantics are stop-the-world reload; RCU/shadow-swap/double-buffered weights listed as unimplemented production mechanisms. Table 1's runtime-update row reads "stop-the-world reload"; §6.2 repeats the requirement.

### R11: TCP/coding interaction — **DONE**
- **Action**: §4.7.1 adds "Interaction with TCP congestion control": systematic UPF coding hides losses from the sender's congestion window; on shared capacity this can starve uncoded flows; relation to Coded TCP [26] and RFC 3135 PEPs [49] stated; deployment precondition added to §6.2 ("bound redundancy so loss hiding does not distort congestion control").

### R12: Link-layer FEC positioning — **DONE**
- **Action**: §4.7.1 adds "Relation to satellite link-layer FEC": DVB-S2X ACM (ETSI EN 302 307-2, ref [48]) leaves residual burst loss; UPF-level RLNC protects that residual pattern, not a steady-state i.i.d. process; parameter selection requires measured burst statistics. §1.1 loss regime disaggregation cross-references this.

### R13: Literature and Table 1 completeness — **DONE**
- **Action**: §2.1 adds a software-data-plane paragraph covering NetBricks [39], ClickOS [40], eBPF/XDP [41], P4-DPDK targets, and telemetry systems Sonata [42], FlowRadar [43], Marple [44] (references [39]–[44] added). Table 1 gains a "Software NFV + ML [39-41]" column across all seven dimensions.

---

## Suggested Revisions (S1–S10)

| # | Item | Disposition | Where |
|---|------|-------------|-------|
| S1 | VoIP generation-assembly delay | **Done**: Table 4 gains an assembly-delay column; §4.7.2 computes 80 ms for k=4 at 20 ms frames vs G.114's 150 ms budget (53%), recommends k=2–3 or timeout early-emit for LEO deployments (ITU-T G.114 added as ref [50]) | §4.7.2, Table 4 |
| S2 | Reframe 12.3% as mechanism validation | **Done**: "the deterministic expected value of Table 4's profiles applied to this run's inferred class mix"; abstract says "expected weighted average"; §5.10.4(a) states it measures policy overhead accounting, not delivered recovery | Abstract, §5.10.4, Table 18 |
| S3 | Scaling presentation | **Done**: Table 9 reports medians with ranges, marks 16 workers as oversubscribed (†), states the SRv6-pipeline workload distinction from Table 8 explicitly; Table 8 labeled "in-memory rate" | §5.4, §5.5 |
| S4 | Algorithm 1 / Definition 4 / Theorem 1 / argmax | **Done**: Algorithm 1 rewritten to implementation form (int64 hidden storage, INT64_MIN argmax init); Definition 4 saturation bound corrected to ±2,147,483,000; Theorem 1 carries the single-dispatcher assumption as an explicit premise; `dnn.c` argmax rewritten to `best_v = INT64_MIN` (behavior-identical, self-evidently correct — the previous `k == 0 \|\|` guard was already correct, which the response to DA-m3 notes) | §3.1, §4.3.2, §4.5, src/dnn.c |
| S5 | DSCP inner/outer + QFI mapping | **Done**: §6.2 states the prototype marks the parsed (inner) header; tunnel-terminating deployments mark the outer transport header; classifier output must map to SMF-authorized QoS flows via PFCP/N4, not override policy | §6.2 |
| S6 | Computed-goto 40% claim | **Done**: softened — "the per-stage effect of this substitution is not separately instrumented in this paper; end-to-end service times are reported in §5.4" | §4.4 |
| S7 | Challenge/pillar structure | **Done**: §1.2 restructured into two general data-plane requirements (R1–R2) and three satellite-sharpened requirements (R3–R5), with an explicit statement of which are evaluated; near-verbatim Challenge 5 ↔ Pillar 5 duplication removed | §1.2 |
| S8 | Abstract qualifiers | **Done**: inline qualifiers added ("median", "in-memory", "oversubscribed", "one 4,000-sample split", "expected weighted average", "motivating scenario") | Abstract |
| S9 | RNG stream separation | **Done**: Protocol B draws test samples from `srand(seed+7919)`, independent of the training stream; Protocol A retained (and labeled) for continuity with prior splits | §5.2, src/enc_classify_demo.c |
| S10 | Constrained-hardware claims | **Done**: §1.1 states benchmarks ran on a commodity x86 platform with no space-grade measurements; §6.1 retains the energy limitation; §6.4 item 6 commits to embedded-platform measurements | §1.1, §6.1, §6.4 |

---

## Devil's Advocate items (adjudicated in the decision letter)

| ID | Adjudication | Revision action |
|----|--------------|-----------------|
| C1 (coding actuation is scaffolding) | Rejected as Critical; validated as Major framing issue | R9 reframing (above) |
| C2 (99.1% uninterpretable without baseline) | Validated | R3 (rule baseline), R4 (confusion matrix), R5 (seeds), R6 (calibrated workload) |
| C3 (12.3% arithmetic identity) | Rejected as Critical; reframing retained | S2 (above) |
| M1 (throughput vs 1–10 Gbps range) | Noted | §5.4–5.5 report in-memory rates with explicit labels; no line-rate claim is made |
| M2 (runtime update races) | Validated | R10 |
| M3 (LOC counts) | Validated | R1 |
| M4 (challenge/pillar circularity) | Noted | S7 |
| M5 (no satellite-parameterized experiment) | Validated | R7 + future-work item 2 |
| M6 (no baseline) | Validated | R3 |
| m3 (argmax best_v=0 bug) | **Partially incorrect as reported**: the previous code's `if (k == 0 \|\| acc > best_v)` guard unconditionally accepts the first logit, so all-negative logits were already handled correctly. The loop was nonetheless rewritten to the `best_v = INT64_MIN` form, which is correct by inspection (S4) |

---

## Editorial continuity item

**Regression from Round 2 (rule baseline removal)**: the Round-2 artifact's rule baseline (94.2%) had been removed in the Round-3 draft. A stronger, fully documented threshold cascade is now implemented and reported (§5.3.3, R3).

## Artifact changes summary (for re-review)

- `src/enc_classify_demo.c` (428 → 644 lines): confusion matrix recording, rule baseline + decision-cost benchmark, Protocol B multi-seed harness, 126-byte latency benchmark (single + batch, 200K-packet loops).
- `src/dnn.c` (163 → 168 lines): argmax rewrite with comment; hidden-storage comment.
- `src/tests.c` (894 → 899 lines): optional packet-count argument for `mq`/`tcpmq` modes (1M-packet scaling runs).
- `Makefile`: `pgo` target now builds `enc_classify_demo` and `nc_demo` under profile feedback.
- Headline Protocol A results are bit-identical to the previous draft (981/982, 951/986, 1013/1013, 1019/1019; 99.1%; 2000/2000 quantization agreement), verified on rebuild.
