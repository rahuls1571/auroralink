# Milestone 2 — Protocol Deep-Dive & Bridge Application Design




## Task M2-A — AuroraLink protocol & IP-core behavior

### Summary

Gain a working understanding of how AuroraLink moves data between FPGA and the A53 application, including whether software **polls** shared memory/FIFOs or is **notified** by hardware events.

### Activities

1. Study **RTDS / CHIL reference code** in this repo:
   - `RTDS_Aurora_Link/Kria_CHIL_Firmware/PEBB_CHIL/xllfifo_interrupt_example_1/` — especially `Axi_IO.c`, `main.c`, FIFO interrupt handlers.
2. Use **RTDS Simulator** documentation for AuroraLink integration (add the exact manual section URL to your notes when found).
3. Review **Xilinx Aurora** product documentation for what runs in PL vs what the PS must do (link layer in IP; PS sees AXI-FIFO or similar).
4. Map **IP-core configuration** parameters relevant to our bitstream (interrupt thresholds, FIFO depth, packet length registers, AXI base addresses, clock domains).
5. Use AI-assisted code walkthroughs where helpful; every conclusion must still cite **file, register, or doc**.

### Key questions (must answer in deliverable)

| # | Question |
|---|----------|
| 1 | How does AuroraLink work end-to-end on Kria (remote partner → Aurora IP → fabric → PS)? |
| 2 | **Push vs pull:** Does the IP “push” into PS-visible memory, or does software **poll** AXI shared regions? Clarify **stream push (PL)** vs **CPU read pull (PS)** vs **interrupt notification**. |
| 3 | What signals **packet / frame boundaries** to software? |
| 4 | What are the **configuration knobs** for the IP core and FIFO bridge (BD + driver)? |

### Acceptance criteria

- [ ] `docs/milestone2/M2-A_auroralink_findings.md` exists (≤ 3 pages).
- [ ] Push/pull question answered in one explicit sentence with vocabulary from [BRIDGE_SOFTWARE_DESIGN.md §2.3](BRIDGE_SOFTWARE_DESIGN.md).
- [ ] Reference implementation behavior described (`FifoRecvHandler` → `axiReceive`, `XLlFifo_*` calls).
- [ ] IP configuration checklist table (parameter, purpose, where documented).
- [ ] All claims have **source** (repo path, RTDS doc, or Xilinx PG).

---

## Task M2-B — libEGD protocol & API usage

### Summary

Understand how **EGD over UDP** is sent and received via **libEGD**, which functions to call for TX/RX, typical payload sizes, and whether the API model is synchronous or asynchronous.

### Activities

1. Deep-read `libEGD-master/` — README, examples (`json_publish`, `json_subscribe`, raw variants), and core client code.
2. Compare with minimal sample `EgdSend.c` / `EgdTest.h` (PDU type 13, port **18246**, payload cap **1400** bytes in-tree).
3. Trace **one publish path** and **one subscribe path** from application call → socket I/O.
4. Document **JsonClient** vs **EgdClient** trade-off (default to JsonClient unless profiling proves otherwise).
5. Incorporate official **EGD specification** when available (user-provided); note spec version in deliverable.

### Key questions (must answer in deliverable)

| # | Question |
|---|----------|
| 1 | How are messages **encoded** for production and **decoded** on consumption? |
| 2 | Which functions to call to **send** data? What happens inside (threads, buffers, UDP)? |
| 3 | Which functions to call to **receive** data? Callback vs blocking vs poll? |
| 4 | **Typical EGD data size** (exchange `DataLength`, UDP limits, library caps)? |
| 5 | **Sync vs async:** blocking calls, internal threads, or explicit event loop? |

### Acceptance criteria

- [ ] `docs/milestone2/M2-B_libegd_findings.md` exists (≤ 3 pages).
- [ ] API cheat sheet: function → direction (TX/RX) → sync model → notes.
- [ ] Sequence diagram or numbered call flow for publish and subscribe.
- [ ] Payload size limits documented with source (`EgdTest.h`, libEGD config XML, spec).
- [ ] Recommendation: JsonClient vs EgdClient for this bridge.

---

## Task M2-C — Cross-compile libEGD for Cortex-A53 (Kria PS)

### Summary

Establish a repeatable build of **libEGD** (and dependencies) for **ARM64 Linux** on the Zynq UltraScale+ **A53** where Ubuntu runs on Kria — not for R5/FPGA.

### Activities

1. Inventory libEGD build system (`CMakeLists.txt`, Docker builder, native deps from `hack/docker/Dockerfile.builder`).
2. Choose approach:
   - **Cross-compile** on dev host with `aarch64-linux-gnu` toolchain, or
   - **Native compile** on the Kria board, or
   - **Yocto/PetaLinux SDK** if that matches the deployed image.
3. Document sysroot / linker flags / required packages for Ubuntu on Kria.
4. Build at least **one example binary** (e.g. `json_publish` or `EgdSend` linked against libEGD) and verify it runs on A53.
5. Capture install layout (`libegd.so`, headers, pkg-config if any).

### Acceptance criteria

- [ ] `docs/milestone2/M2-C_cross_compile.md` with exact commands and toolchain versions.
- [ ] Artifact path or install prefix documented.
- [ ] Smoke test result on A53 (command + expected output).
- [ ] Known gaps / blockers listed (missing deps, ABI issues).

---

## Task M2-D — Bridge application architecture (bidirectional dataflow)

### Summary

Design the **mediator application** that reads Aurora-side data (AXI/FIFO path), sends to a remote controller via **libEGD**, and concurrently receives EGD data and drives Aurora TX — aligned with [DESIGN_INTENT.md](../DESIGN_INTENT.md).

### Activities

1. Define logical modules (Aurora adapter, translation core, EGD adapter) per [BRIDGE_SOFTWARE_DESIGN.md](BRIDGE_SOFTWARE_DESIGN.md).
2. Compare scheduling models:

   | Model | Consider |
   | ----- | -------- |
   | Single thread, round-robin | Simplicity, worst-case latency |
   | Multi-thread, event-driven | FIFO IRQ + socket `poll`/`epoll` |
   | Split processes (sidecar) | Isolation vs integration cost |

3. Specify **buffer handoff** (latest-value, timestamps, VALID/STALE/INVALID).
4. Decide **sync vs async** at each boundary (Aurora RX → buffer → EGD TX; EGD RX → buffer → Aurora TX).
5. Sketch thread/ISR diagram and failure modes (one side down, stale data).

### Key questions (must answer in deliverable)

| # | Question |
|---|----------|
| 1 | How is **bidirectional** flow scheduled without starving either side? |
| 2 | Where are **blocking** vs **non-blocking** operations allowed? |
| 3 | ISR / FIFO handler vs worker thread boundaries? |
| 4 | How does design honor **event-driven Aurora** vs **cyclic EGD** (design intent timing table)? |

### Acceptance criteria

- [ ] `docs/milestone2/M2-D_application_design.md` with architecture diagram (mermaid or ASCII).
- [ ] Explicit recommendation: threading model + rationale.
- [ ] Data-transfer table per boundary (mechanism, buffer, sync).
- [ ] Open risks and M3 implementation tasks listed.
- [ ] Cross-references M2-A/B/C findings (no contradictions).

---

## Dependency graph

```text
M2-A (Aurora) ──┐
M2-B (EGD)    ──┼──► M2-D (Application design)
M2-C (Build)  ──┘
```

M2-A and M2-B can run in parallel. M2-C can start after M2-B has a rough build picture. **M2-D should start after M2-A/B have draft findings** and incorporate M2-C constraints.
