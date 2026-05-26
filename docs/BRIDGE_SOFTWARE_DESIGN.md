# Bridge Software Design — AuroraLink (PL) ↔ EGD (Ethernet)

This document captures the **software architecture** for the EMS controller translation bridge on **AMD Xilinx Kria**, with **AuroraLink (Aurora-class IP in PL)** on one side and **EGD** (via **libEGD**) on the other. It complements [DESIGN_INTENT.md](../DESIGN_INTENT.md) (buffer semantics, success criteria) and [DOCUMENTATION.md](../DOCUMENTATION.md) (broader protocol context).

---

## 1. Scope and boundaries

| Layer | Responsibility | Typical artifacts |
| ----- | ---------------- | ----------------- |
| **PL — AuroraLink IP** | High-speed serial link, framing, AXI-Stream to fabric | Vivado BD, transceivers, Aurora core |
| **PL — bridge to PS** | Stream ↔ memory-mapped path (e.g. AXI FIFO, DMA, or similar) | `XLlFifo` or successor IP in block design |
| **PS — Aurora ingest / egress** | Map peripheral, react to data availability, unpack words into application structs | Today: `Axi_IO.c` + `main.c` interrupt path (reference CHIL tree) |
| **PS — translation / mediation** | State, buffering, mapping, freshness, degraded modes | **To be implemented** — core of this program |
| **PS — EGD I/O** | UDP production/consumption, encoding/decoding pages | **libEGD** (`JsonClient` / `EgdClient`, etc.) |

This design doc focuses on the **PS software split** (Aurora path, translation layer, EGD path) and **open investigations** that must be closed with measurements or RTL review.

---

## 2. Aurora side — how data reaches the processor

### 2.1 Physical path (reference model)

```text
Remote link partner  --fiber/serial-->  [AuroraLink IP in PL]
                                              |
                                         AXI4-Stream
                                              |
                                         FIFO / bridge IP
                                              |
                                    AXI4 (memory-mapped)
                                              |
                                    PS (Cortex-A53)  ←  drivers + application
```

**AuroraLink runs in the FPGA.** Application code on the ARM cores does **not** implement the link layer; it **reads and writes** a **memory-mapped PL peripheral** (here, the **AXI FIFO** via `XLlFifo_*` APIs) that is **fed by** (RX) or **feeds** (TX) the stream coming from / going to the Aurora datapath.

### 2.2 Role of `Axi_IO` (reference implementation)

In the existing **PEBB CHIL** reference, `Axi_IO.c`:

- Declares **what each 32-bit word means** (`rxData[]` / `txData[]` tables, scaling, types).
- **`axiReceive()`** — drains the **RX FIFO** word-by-word and writes into controller variables.
- **`axiSend()`** — packs application state and **fills the TX FIFO** word-by-word.

The file header states explicitly: data is **to/from PL through the AXI FIFO** using **mapped addresses** and Xilinx drivers.

### 2.3 Investigation — “push” vs “pull” (PL → processor)

**Clarify vocabulary** (avoid ambiguous “push/pull”):

| View | Meaning in this context |
| ---- | ------------------------- |
| **Stream producer (PL)** | Aurora + fabric **pushes** AXI-Stream beats into the FIFO when the link delivers data (`TVALID`/`TREADY`). **No application CPU** in that hop. |
| **Delivery notification** | FIFO IP can **assert an interrupt** to the PS when threshold met / packet complete — **hardware “nudge”** to software. |
| **Data retrieval (PS)** | Software **pulls** each word with `XLlFifo_RxGetWord()` (explicit reads over AXI). That is **processor-initiated** bus traffic. |

**Reference behavior in tree:** `FifoRecvHandler()` → `axiReceive()` is called from the **FIFO interrupt path** in `main.c` — i.e. **event-driven wakeup**, then **CPU pull** of FIFO contents.

**Open items to confirm on *your* bitstream / BD:**

1. **Interrupt vs poll** — Is RX always interrupt-driven, or is there a polled or DMA path in the target design?
2. **Packet boundary** — Does hardware expose “frame complete” or only “FIFO non-empty”? (Affects how the translation layer knows a **logical** Aurora message is complete.)
3. **Who clears / commits** — FIFO length register semantics, partial reads, error recovery (`ReceiveLength` vs `RECEIVE_LENGTH` pattern in `axiReceive`).
4. **Cache coherency** — If any buffer is in DDR with PL visibility, document **non-cacheable** regions vs **flush/invalidate** policy (FIFO MMR path is usually uncached; DMA would differ).

**Design decision placeholder:** Record the conclusion as one line in the implementation guide, e.g. “RX: **IRQ-driven notification + CPU word-pull from AXI4 FIFO**” or “RX: **DMA SG to DDR + eventfd**”, once RTL + measurements are fixed.

---

## 3. EGD side — libEGD

The **EGD interface** is implemented in software using **libEGD** (see `libEGD-master/` in repo):

- **Publish** — periodic or triggered construction of EGD production messages and send over UDP.
- **Subscribe** — receive and parse peer EGD traffic.

**API choice** (from upstream README):

- **`JsonClient`** — preferred for maintainability and faster integration.
- **`EgdClient` (raw bytes)** — reserved for tight latency budgets after profiling.

The translation bridge must **not** embed Aurora framing inside libEGD; it should feed libEGD **canonical** structs / JSON pages produced by the mediation layer.

---

## 4. Mediation layer (between Aurora path and EGD path)

This is the **new** software you are designing: a **bounded context** between `Axi_IO`-style I/O and libEGD I/O.

### 4.1 Responsibilities

| Function | Description |
| -------- | ----------- |
| **Ingress adapters** | Normalize Aurora-side words into an **internal canonical model** (aligned with [DESIGN_INTENT.md](../DESIGN_INTENT.md) “internal representation”). |
| **Egress adapters** | Map canonical state to **EGD page layout** and to **Aurora TX word layout**. |
| **State machine** | Explicit states for link health, CHIL/EMS modes, startup, fault, and safe outputs (e.g. `EMPTY` / `VALID` / `STALE` / `INVALID` from design intent). |
| **Buffers** | Latest-value or small bounded queues per design intent; **timestamps** and **validity** flags at boundaries. |
| **Scheduling** | Reconcile **event-driven** Aurora activity with **cyclic** EGD publication (see timing table in design intent). |
| **Observability** | Counters for drops, length mismatches, sequence gaps, NaN/glitches (patterns already present in reference `axiReceive`). |

### 4.2 Suggested module boundaries (logical)

```text
                    +---------------------------+
  AXI FIFO /        |  Aurora adapter           |
  XLlFifo driver    |  (RX unpack / TX pack)    |
                    +------------+--------------+
                                 |
                    +------------v--------------+
                    |  Translation core        |
                    |  - state machine         |
                    |  - canonical model       |
                    |  - mapping tables        |
                    |  - shared buffer + TS    |
                    +------------+--------------+
                                 |
                    +------------v--------------+
                    |  EGD adapter              |
                    |  (libEGD publish/sub)   |
                    +---------------------------+
```

Threads vs single loop is an **implementation** choice; the **logical** separation above should hold regardless.

### 4.3 Interaction with existing CHIL code

**Option A — Evolve in place** — Extend/refactor `Axi_IO` + main loop to call into mediation + libEGD (fastest bring-up, higher coupling).

**Option B — Sidecar process** — Separate process with shared memory / message queue between FIFO service and translator (cleaner isolation, more integration work).

Record the chosen option in the next revision of this document.

---

## 5. Threading and synchronization (outline)

| Concern | Options to evaluate |
| ------- | --------------------- |
| **FIFO RX to translation** | Same thread as ISR (minimal latency, stack size risk) vs ISR defers to worker queue (preferred for heavy translation). |
| **Translation to EGD TX** | Timer-driven publish vs “dirty flag” after each Aurora update. |
| **EGD RX to Aurora TX** | Socket readiness vs dedicated reader thread; rate limit on TX to PL. |
| **Locking** | Mutex around canonical buffer vs lock-free single-writer single-reader with aligned words — start simple per design intent. |

---

## 6. Verification hooks

Align lab tests with:

- **Aurora path** — Inject length mismatch / sequence jump; assert `INVALID` / fault flags and safe outputs.
- **EGD path** — Loopback or second host with known Producer/Exchange IDs; PCAP golden compares.
- **End-to-end** — Latency distribution Aurora event → EGD wire (and reverse).

---

## 7. Document control

| Version | Date | Author | Notes |
| ------- | ---- | ------ | ----- |
| 0.1 | (initial) | — | Skeleton: Aurora + Axi_IO + libEGD + mediation; push/pull investigation framed |

**Next edits:** Close Section 2.3 with BD-specific facts; add sequence diagrams for both directions; add API table for libEGD pages used.
