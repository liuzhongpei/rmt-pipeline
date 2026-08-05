# Second-Round Peer Review Report

**Paper:** Service-Aware Edge Computing-Networking Collaboration Model for Satellite Network UPF: A Programmable RMT-Based In-Network Computing Framework

**Review Type:** Second Round (Re-Review after Major Revision)

**Reviewer Role:** Strict Reviewer — Devil's Advocate

---

## Summary (250 words)

This paper proposes EdgeNet-CoM, a programmable RMT-based in-network computing framework for satellite UPF that integrates per-packet traffic classification (via a quantized MLP) with service-aware routing and network coding decisions within a unified 5-stage pipeline. The revision has made meaningful progress on several first-round issues: the "P4-inspired" framing is more honest, synthetic data limitations are now extensively acknowledged, a rule-based baseline has been introduced, the "first-packet" claim has been partially clarified, net neutrality is discussed, and LOC counts are more consistent.

However, the revision has introduced **new internal contradictions** that are arguably worse than the original issues. Most critically: (1) Section 5.9 (line 781) claims "confidence intervals well below 1%" while Section 6.1 admits "no confidence intervals were computed"—a direct self-contradiction; (2) the abstract claims "3.4× with 16 workers" and "near-linear scaling" while Table 7 shows 2.57× speedup at 16% efficiency; (3) Section 1.4 claims "The architecture is expressed entirely in P4" while Section 6.1 uses "P4-inspired"; (4) Sections 2.5 and 5.7 continue to claim "the very first packet of a new flow" is classified despite the Section 6.1 clarification that it is actually the 5th–6th packet; (5) Section 3.4 claims feature patterns are "validated against real-world encrypted traffic traces" but no such validation results are ever presented. The evaluation remains entirely on synthetic data with no real traffic, no confidence intervals, no variance bars, and no cross-validation. These contradictions suggest the revision was applied unevenly across sections, undermining confidence in the paper's internal consistency. The paper shows genuine engineering merit but falls short of top-venue standards in evaluation rigor and self-consistency.

---

## Resolution of First-Round Issues

| # | First-Round Issue | Severity | Resolution Status | Assessment |
|---|---|---|---|---|
| 1 | All synthetic data, no real traffic validation | CRITICAL | △ Partially Resolved | Limitations honestly acknowledged (Section 6.1, 7), BUT misleading claim "validated against real-world encrypted traffic traces" (Section 3.4, line 257) remains with no supporting data. No real traffic experiments added. |
| 2 | Unfair comparison with existing work | MAJOR | △ Partially Resolved | Table 9 revised with more structured comparison. Rule-based baseline added (94.2%). BUT comparison numbers for other systems remain approximate ("~95%", "~93%") without clear sourcing. Different datasets, different class definitions. |
| 3 | P4 programmability vs. C implementation contradiction | MAJOR | △ Partially Resolved | "P4-inspired" framing introduced in Section 1.3 and 6.1 — an improvement. BUT Sections 1.4 (line 77: "expressed entirely in P4"), abstract (line 9: "P4-programmable"), and Table 1 still claim "P4 programmable" — creating a three-way inconsistency. |
| 4 | QUIC support missing | MAJOR | ✓ Resolved | Explicitly acknowledged as limitation (Section 6.1, TCP-only). Listed as future work (Section 6.4, item 4). Honest and adequate. |
| 5 | No baseline comparison (rule vs. DNN) | MAJOR | △ Partially Resolved | Threshold-based rule baseline added (94.2% vs. 99.1%). BUT: only on synthetic data, no per-class breakdown for rules, rule description is informal, and the 5-point gap may simply reflect overfitting to the synthetic distribution. |
| 6 | No statistical analysis (CI, significance tests) | MAJOR | ✗ Not Resolved | Authors admit no CIs computed (Section 6.1). BUT Section 5.9 (line 781) still states "confidence intervals well below 1% for all reported metrics" — a direct, unresolved contradiction. No error bars, no variance, no repeated runs. |
| 7 | First-packet claim misleading | MAJOR | ✗ Not Resolved | Section 6.1 adds a clarification (first *app_data* packet, not literally first). BUT Sections 2.5 (line 141) and 5.7 (line 736) STILL claim "the very first packet of a new flow" without cross-referencing the limitation. The misleading claim remains uncorrected in the most visible parts. |
| 8 | Citation errors | MAJOR | △ Uncertain | Cannot fully verify without access to all sources. Some references appear misaligned (e.g., [1] Shi 2016 for "3GPP NTN specifications"; [14] RFC 2475 cited where RFC 2474 is mentioned; [16] Sailfish cited for SD-WAN). Authors should verify each citation maps correctly. |
| 9 | LOC inconsistency | MINOR | ✓ Resolved | 4,200 core + 1,800 test = 6,000 total. Table 4 modules sum to ~4,365 (within rounding). Consistent across Abstract, Section 1.4, Section 5.1. |
| 10 | Net neutrality not discussed | MINOR | ✓ Resolved | Substantial discussion added in Section 6.3. Distinguishes engineering QoS from content discrimination. Notes regulatory considerations and audit points. Well done. |
| 11 | Excessive speculative content | MINOR | ✓ Resolved | Speculative applications moved to Discussion (Section 6.3–6.4). Main body is more focused. The five-pillar structure helps organize claims. |

**Summary:** 3 fully resolved, 5 partially resolved, 1 not resolved, 1 uncertain, 1 resolved.

---

## Dimension Scores (1–10)

### 1. Problem Definition Clarity — 8/10

**Strengths:** The problem of service-aware forwarding at satellite UPF under encryption is well-motivated and timely. The five sub-challenges (Section 1.2) are clearly decomposed. The satellite UPF context (heterogeneous paths, elevated loss, constrained processing) adds specificity beyond generic edge computing.

**Weaknesses:** The scope of "in-network computing framework" is very broad—encompassing classification, routing, coding, and programmability—which risks appearing as a collection of loosely connected contributions rather than a unified framework. The optimization objective (Section 3.2) is formalized with an unspecified utility function $U(\cdot)$, weakening the theoretical grounding. The four-class taxonomy is motivated more by engineering convenience than theoretical necessity.

### 2. Core Contribution Novelty — 7/10

**Strengths:** The integration of Q16.16 fixed-point MLP inference into a software RMT pipeline is a genuine systems contribution. The "compute once, act twice" (route + code) paradigm is a clean insight. The zero-dependency single-binary deployment model is practical and unusual.

**Weaknesses:** The individual components (programmable parser, fixed-point MLP, RSS-based scaling, network coding) are individually well-known. The contribution is primarily architectural integration rather than fundamental innovation. The concept of running a small MLP in the data plane has precedents in SwitchML, NetSage, and other in-network ML work—running it in software on commodity CPUs is a design choice, not a breakthrough.

### 3. Technical Rigor — 5/10

**Strengths:** Definition 1–6 provide a reasonable formalization scaffold. Algorithm 1 (Q16.16 forward pass) is correct and clearly specified. Theorem 1 (per-flow ordering) is trivially true but correctly stated.

**Weaknesses:**
- **Definition 2** claims features occupy "fixed positions in the packet header stack"—this is false for packets with IP options, TCP options, or after middlebox modifications. No caveat is provided.
- **The optimization objective** uses an undefined utility function $U(\cdot)$, making it non-operational.
- **Table 5 internal inconsistency check:** The per-class totals (982+986+1013+1019 = 4000) are consistent with 4000 test packets. BUT the table shows 981/982 for Web (1 misclassified AWAY from Web) and 951/986 for Video (35 misclassified AWAY from Video). If 36 total misclassified, and accuracy is 99.1%, then 0.9% × 4000 = 36 — this checks out. However, 981+951+1013+1019 = 3964 correct, and 3964/4000 = 99.1% — consistent.
- **Contradiction in Section 5.9 vs. 6.1** (see below, New Issues #1) is a serious rigor failure.
- **Section 3.4 claims "validated against real-world encrypted traffic traces"** but no validation data is presented (see New Issues #3).

### 4. Evaluation Sufficiency — 3/10

**Strengths:** Comprehensive test suite design (T1–T7). Multi-dimensional evaluation (accuracy, latency, scaling, load balance, coding correctness). End-to-end functional verification (Table 10).

**Weaknesses:**
- **100% synthetic data evaluation.** The CRITICAL first-round issue is not resolved.
- **No confidence intervals, no error bars, no repeated runs.** Latency measurements are single-configuration point estimates.
- **Unfair comparison.** Table 9 compares against systems evaluated on different datasets, different class taxonomies, different hardware. The "~95%", "~93%", "~92%" figures have no clear provenance.
- **Rule baseline is under-specified.** The threshold rules are described informally in one paragraph. No per-class breakdown, no precision/recall, no confusion matrix.
- **No end-to-end network evaluation.** The system processes synthetic packets in memory—no real NIC, no real satellite protocol stack, no real 5G UPF integration.
- **The 99.1% accuracy on well-separated synthetic distributions is unsurprising and essentially meaningless for real-world claims.** The authors acknowledge this but continue to headline the number.

### 5. Literature Coverage Completeness — 6/10

**Strengths:** Covers the major areas: RMT/P4, in-network ML, encrypted traffic classification, network coding, satellite edge computing. 37 references provide reasonable breadth.

**Weaknesses:**
- **Missing key works:** No mention of Katzev et al.'s work on per-packet encrypted traffic classification; no reference to hardware ML accelerators for programmable switches beyond N3IC; no discussion of recent work on QUIC traffic classification; no reference to 3GPP TS 29.244 (UPF specification).
- **Some citations appear misaligned** (e.g., [1] Shi 2016 is about generic edge computing, cited for 3GPP NTN; [14] RFC 2475 cited where RFC 2474 is contextually appropriate).
- **No comparison with classical traffic classification approaches** like nDPI, which is the industrial standard.

### 6. Writing Clarity — 6/10

**Strengths:** The five-pillar structure provides clear organization. Mathematical notation is consistent. ASCII diagrams, while not publication-quality, convey the architecture adequately.

**Weaknesses:**
- **Internal contradictions across sections** (see New Issues #1–5) create confusion about what the paper actually claims.
- **Repetitive.** The same five pillars, same four service classes, same four features are re-described in every section with minor rewording. The paper could be 30% shorter.
- **Overclaiming in visible locations.** The abstract, introduction, and evaluation sections use stronger language than the limitations section permits.
- **Some grammatical issues.** "Do switches dream of machine learning?" (ref [11]) is the title of a cited paper, not a sentence in this paper—but the citation format should be verified.

### 7. Practicality / Impact — 6/10

**Strengths:** The zero-dependency deployment model is genuinely appealing for edge scenarios. The satellite UPF motivation is timely given 3GPP NTN standardization. The network coding differentiation idea has practical value.

**Weaknesses:**
- **No real traffic validation** means the practical value is unproven.
- **No integration with real satellite infrastructure** (no 5G core, no SMF, no N4 interface, no real NIC).
- **The P4 compiler is a toy implementation** (~1,200 LOC) that likely supports a tiny subset of P4₁₆. Claims of "P4 programmability" overstate this significantly.
- **The system has not been tested under real network conditions** (packet reordering, NIC interrupts, memory pressure, etc.).

### 8. Reproducibility — 5/10

**Strengths:** LOC breakdown provided. Algorithm pseudocode is detailed enough to implement. Architecture description is complete.

**Weaknesses:**
- **No code release mentioned.** 6,000 LOC is manageable; should be open-sourced.
- **No model weights released.** The 220-parameter model would fit in the paper as a table.
- **Synthetic traffic generator not described.** How are class-conditional features sampled? What are the exact distribution parameters? Without this, the 99.1% accuracy cannot be reproduced.
- **No description of the P4 source programs.** What P4 code was compiled? What does the AST look like?
- **Hardware platform is described but no OS tuning details** (CPU governor, isolcpus, hugepages, etc.)—critical for reproducing latency numbers.

---

## New Issues Found

### NEW #1 — CRITICAL: Direct Internal Contradiction on Statistical Analysis

**Section 5.9 (line 781):** *"Statistical significance is ensured by large test populations... These sample sizes provide **confidence intervals well below 1%** for all reported metrics."*

**Section 6.1 (line 913):** *"The evaluation reports **point estimates without confidence intervals or significance tests**. Latency measurements are single-configuration means without variance across repeated runs."*

These statements are **mutually contradictory** within the same paper. Section 5.9 claims CIs exist and are below 1%; Section 6.1 claims no CIs were computed. This is the most damaging inconsistency in the paper—it suggests either the limitations section was copy-pasted from the first round without updating Section 5.9, or the authors believe large sample sizes automatically produce narrow CIs without computing them (which is a statistical misunderstanding). **This MUST be fixed before publication.**

> **建议 / Suggestion:** Remove the false claim in Section 5.9 OR actually compute and report confidence intervals (bootstrap over latency measurements, binomial CI for accuracy).

### NEW #2 — MAJOR: Speedup Baseline Inconsistency (3.4× vs. 2.57×)

The abstract (line 11) claims: *"near-linear multi-core scaling (3.4× with 16 workers)"*
Table 17 (line 886) claims: *"Multi-core scaling (16 workers): >2.5× → 3.4× → ✓ Exceeds"*

But **Table 7 (line 694)** shows: 16 workers → 2.75 Mpps → **2.57×** speedup over the **optimized** baseline (1.07 Mpps).

The 3.4× figure appears to be computed as 2.75/0.81 ≈ 3.4× where 0.81 Mpps would be some intermediate baseline. In fact, it seems to compare against the **unoptimized** baseline (6.3 µs = 0.16 Mpps → 2.75/0.16 ≈ 17×, not 3.4× either). The arithmetic behind 3.4× is unclear and un-reproducible.

Furthermore, **"near-linear" for 2.57× on 16 workers (16% efficiency) is objectively not near-linear by any standard definition.** Line 696 itself calls this "sub-linear scaling." The abstract contradicts the body.

> **建议:** Fix abstract and Table 17 to match Table 7's 2.57×. Remove "near-linear" claim, or define explicitly what baseline is used.

### NEW #3 — MAJOR: Unsubstantiated "Real-World Validation" Claim

**Section 3.4 (line 257):** *"The patterns are derived from both synthetic traffic generation... and **validated against real-world encrypted traffic traces**."*

This is the **only** mention of real-world validation in the entire paper. No validation results, no comparison figures, no accuracy numbers on real traces are ever presented. If validation was performed, where are the results? If it was not performed, this claim is false.

This is particularly problematic because it appears in the problem formulation section (not in limitations), creating a false impression that real-world validation has been done.

> **建议:** Either present the real-world validation results with numbers, or remove the claim and state "derived from synthetic traffic generation calibrated against published statistics."

### NEW #4 — MAJOR: "First-Packet" Claim Still Misleading in Body Sections

Section 6.1 adds a clarification that the "first-packet" claim actually means "first application_data packet" (5th–6th packet in TLS 1.3). This is appropriate.

However, the following sections **still contain the original misleading language without cross-reference to the clarification:**

- **Section 2.5 (line 141):** *"every packet, including the **very first of a new flow**, receives optimal path assignment"*
- **Section 5.7 (line 736):** *"Our per-packet approach classifies every packet upon arrival, including the **very first packet of a new flow**."*
- **Abstract (line 9):** Implied by "every arriving packet can be parsed, classified..."

The limitation section's clarification is invisible to readers who don't reach Section 6. The misleading claims in Sections 2.5 and 5.7 are in the Related Work and Evaluation—the most-read parts of the paper.

> **建议:** Add cross-references: "the very first *classifiable* packet (see Section 6.1 for clarification)" or revise these sentences to match the limitation section.

### NEW #5 — MAJOR: P4 Claim Three-Way Inconsistency

The paper uses three different framings of the P4 claim in different sections:

| Location | Claim |
|---|---|
| Abstract (line 9) | "**P4-programmable** service logic" |
| Section 1.3 (line 57) | "**P4-Inspired** Programmable Service Logic" |
| Section 1.4 (line 77) | "The architecture is **expressed entirely in P4** and implemented as a portable C prototype" |
| Table 1 (line 159) | "**P4 programmable** ✓" |
| Section 6.1 | "P4-inspired" |
| Conclusion (line 962) | "**P4-inspired** programmable service logic" |

"The architecture is expressed entirely in P4" (line 77) is a **factually false statement.** The architecture is implemented in C, not P4. The P4 compiler (~1,200 LOC) translates a limited P4 subset to microcode—it is not a full P4 development environment. The abstract and Table 1 still claim "P4-programmable" while the body acknowledges "P4-inspired."

> **建议:** Unify to "P4-inspired" everywhere. Remove "expressed entirely in P4." Clarify in the abstract that the P4 compiler targets the software pipeline, not hardware switch ASICs.

### NEW #6 — MINOR: Definition 2 Overclaims Header Position Determinism

Definition 2 states: *"Crucially, they occupy **fixed positions** in the packet header stack."*

This is incorrect when:
- IP options are present (shifts TCP header offset)
- TCP options are present (shifts TLS record header offset)
- Middleboxes insert/remove header bytes (e.g., DPI appliances, NAT devices)
- IPv6 extension headers are used

The claim should include a caveat: "assuming no IP/TCP options and standard header stacking."

### NEW #7 — MINOR: Table 10 Uses Unrealistic Test Inputs

Table 10's test scenarios use perfectly representative feature values (e.g., VoIP: len=120, win=4000, tls_len=80). These are essentially the same values from Table 2's idealized ranges. Real traffic has much more variation. The test validates that the system works on "textbook" inputs but says nothing about robustness to edge cases, distribution shift, or adversarial inputs.

### NEW #8 — MINOR: Optimization Objective Is Non-Operational

The utility function $U(c_k, p_m, \gamma_k)$ in Section 3.2 is never specified. Without a concrete utility function, the optimization problem (Section 3.2) cannot be solved, verified, or compared against alternative formulations. The paper presents it as formalization but it is actually hand-waving disguised as mathematics.

### NEW #9 — MINOR: No Mention of HTTP/2 or HTTP/3 Impact

Modern web traffic increasingly uses HTTP/2 (multiplexed streams over single TCP connection) and HTTP/3 (over QUIC). HTTP/2 multiplexing fundamentally changes TLS record patterns—a single TCP connection may carry interleaved requests for different "services" (e.g., video embed in a web page). The paper's per-packet classification cannot distinguish different application contexts within the same HTTP/2 connection. This is a significant limitation that is not discussed.

### NEW #10 — MINOR: No Energy/Power Analysis

For satellite on-board deployment (mentioned extensively in motivation), energy consumption is critical. The paper reports no energy or power metrics. Integer arithmetic may be more energy-efficient than FPU, but this should be quantified.

---

## Devil's Advocate: Strongest Counter-Argument

**The paper's 99.1% accuracy claim is the house of cards upon which the entire contribution rests—and it is built on synthetic data with well-separated class distributions.**

The strongest反驳 argument is this: the paper demonstrates that a 220-parameter MLP can classify packets from **synthetic distributions that were designed to be well-separated** into four classes with 99.1% accuracy. This is essentially a self-fulfilling prophecy. If you generate training data from Gaussian mixtures with minimal overlap, then of course a classifier will achieve high accuracy on test data from the same mixtures. The real question—does this generalize to real encrypted traffic?—is explicitly deferred to future work.

The circular reasoning is subtle but pervasive: (a) the feature distributions in Table 2 are "derived from synthetic traffic generation matching published statistics"; (b) the classifier is trained on this synthetic data; (c) the classifier achieves 99.1% on synthetic test data; (d) the paper concludes "per-packet header features contain sufficient statistical signals" for classification. But step (d) is only validated for the synthetic distribution, not for real traffic. The "key observation" (Section 3.4) that "features exhibit class-conditional distributions with minimal overlap" is an **assumption** baked into the data generator, not an empirical finding.

Furthermore, the four features used (ipv4.len, tcp.window, tls.length, tls.type) are insufficient to distinguish many real-world scenarios:
- HTTP/2 multiplexing: a single TCP connection carries mixed content types
- Video calls (Zoom/Teams): combine video + audio + screen sharing in one connection
- Adaptive bitrate streaming: behavior changes dynamically based on network conditions
- Encrypted DNS (DoH): looks like regular HTTPS but has distinct patterns
- CDN-hosted content: same IP serves video, web, and API traffic

The paper's response—that these are "coarse categories" and finer classification is future work—acknowledges the limitation but also reveals that the current system may be solving a simpler problem than claimed.

A skeptical reviewer would argue: this is a well-engineered software pipeline that runs a small MLP on packet headers. The contribution is the integration, not the intelligence. The 99.1% accuracy on synthetic data is a necessary but insufficient demonstration. Until validated on real traffic—ideally from operational satellite UPFs—the paper's central claim remains unproven.

**隐藏循环论证 / Hidden circular reasoning:** The paper assumes feature distributions are well-separated → generates synthetic data reflecting this assumption → trains classifier on synthetic data → measures accuracy on synthetic data → concludes features are discriminative. The conclusion is identical to the assumption.

**过度泛化 / Overgeneralization:** Claims like "every arriving packet can be parsed, classified, and acted upon" and "per-packet service awareness" imply universal applicability, but the system only works for TCP+TLS traffic with four coarse service classes.

**未讨论的替代方案 / Unconsidered alternative:** Why not simply use DPI on the subset of traffic where it's feasible (non-encrypted, or TLS with known SNI), and fall back to heuristic classification only for truly opaque traffic? The paper assumes all traffic must be classified by ML without considering that a hybrid approach might be more accurate and practical.

---

## Overall Assessment

The revised paper has made **genuine progress** on several first-round issues. The honest acknowledgment of synthetic data limitations, the introduction of a rule-based baseline, the QUIC limitation discussion, and the net neutrality section are all commendable improvements. The five-pillar architecture provides a clear organizational framework, and the engineering work (6,000 LOC, zero dependencies) is respectable.

However, the revision has **introduced or failed to fix critical internal inconsistencies** that are disqualifying for a top venue:
1. The Section 5.9 vs. 6.1 contradiction on confidence intervals is a factual error that undermines scientific credibility.
2. The 3.4× vs. 2.57× speedup discrepancy between abstract and Table 7 suggests either carelessness or deliberate cherry-picking of baselines.
3. The continued "first-packet" overclaims in Sections 2.5 and 5.7, despite the Section 6.1 clarification, suggest the revision was not applied systematically.
4. The "expressed entirely in P4" claim (Section 1.4) is factually false and contradicts the "P4-inspired" framing elsewhere.
5. The "validated against real-world encrypted traffic traces" claim (Section 3.4) with no supporting data is a serious integrity concern.

The **evaluation remains the fundamental weakness.** 100% synthetic data, no confidence intervals, no error bars, no real traffic, no network-level evaluation. The paper presents a systems prototype but evaluates it only in isolation with synthetic inputs. For a top venue (SIGCOMM, NSDI, INFOCOM), real-world validation is essential.

**To reach top-conference standard, the paper needs:**
1. Fix ALL internal contradictions (especially #1, #2, #4, #5 above)
2. Run at least ONE experiment on real encrypted traffic (even a small pcap from a university network would help)
3. Report confidence intervals and/or error bars
4. Release the code and model weights
5. Provide the exact synthetic traffic generation parameters for reproducibility
6. Unify the P4 terminology across all sections

---

## Editorial Decision: **Major Revision**

The paper is not ready for acceptance in its current form due to (a) unresolved internal contradictions that undermine scientific credibility, (b) continued reliance on 100% synthetic evaluation, and (c) misleading claims in visible locations (abstract, related work) that contradict the paper's own limitations section. A second major revision is warranted, with the following mandatory requirements:

---

## Specific Revision Requests

### MUST FIX (Blocking)

1. **Resolve Section 5.9 vs. 6.1 contradiction on confidence intervals.** Either:
   - (a) Remove the false claim in Section 5.9 and acknowledge no CIs were computed, OR
   - (b) Actually compute and report CIs (bootstrap for latency, binomial Wilson CI for accuracy).
   - **不可接受 / Unacceptable:** Leaving both contradictory statements in the paper.

2. **Fix speedup inconsistency.** The abstract and Table 17 must report 2.57× (from Table 7), not 3.4×. Remove "near-linear" or explicitly state "2.57× over optimized baseline (16% parallel efficiency)."
   - **中英对照:** "near-linear" (近线性) is not a valid description of 16% efficiency.

3. **Remove or substantiate "validated against real-world encrypted traffic traces" claim (Section 3.4, line 257).** If no real-world validation was performed, this must be removed.

4. **Fix P4 claim inconsistency.** Unify to "P4-inspired" in abstract, Table 1, and Section 1.4. Remove "expressed entirely in P4" (line 77).

5. **Fix "first-packet" claims in Sections 2.5 and 5.7.** Add cross-reference to Section 6.1 clarification, or revise to "first classifiable application data packet."

### SHOULD FIX (Strongly Recommended)

6. **Add at least one real-traffic experiment.** Even a small evaluation on a publicly available pcap (e.g., UGRA 2023, or ISCX VPN-nonVPN) would significantly strengthen the paper. Report accuracy on real data alongside synthetic.

7. **Report confidence intervals or error bars.** For accuracy: binomial 95% Wilson CI. For latency: bootstrap CI from repeated runs (at least 30 runs). For scaling: mean ± std over multiple runs.

8. **Provide per-class breakdown for rule-based baseline.** The current single-number comparison (94.2%) is insufficient. Show the confusion matrix for rules vs. DNN.

9. **Release code and model weights.** 6,000 LOC is small enough for anonymous release. A supplementary zip file or anonymous GitHub repo.

10. **Describe the synthetic traffic generator in detail.** Distribution parameters (means, variances, mixture weights) for each class. This is essential for reproducibility.

### NICE TO HAVE

11. **Add energy/power characterization** for satellite on-board deployment scenario.
12. **Discuss HTTP/2 multiplexing impact** on per-packet classification.
13. **Compare against nDPI** or another industrial traffic classification tool.
14. **Reduce repetition** across sections—the five pillars are described at least four times.
15. **Verify all citations** for accuracy (especially [1], [14], [16]).

---

*Review prepared under strict second-round re-review protocol. All issues verified against specific line references in the manuscript.*
