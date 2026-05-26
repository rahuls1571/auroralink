# KR260 Aurora + FIFO + Pattern Generator — पूरी Setup Guide (Hindi में)

> **लक्ष्य:** हमारे Aurora link को KR260 board पर end-to-end test करना, बिना real RTDS simulator के। हम RTDS की जगह FPGA के अंदर एक "Pattern Generator" लगा देंगे जो वो data fake करेगा जो RTDS भेजता।

---

## Table of Contents

1. [Big Picture — हम actually क्या बना रहे हैं?](#part-1)
2. [Train Station की कहानी (Refresher)](#part-2)
3. [Block-by-Block Architecture (train analogy के साथ)](#part-3)
4. [.v file में `m_axis` क्या है? — Line by Line](#part-4)
5. [Counter / Pattern Generator Design — पूरी Verilog](#part-5)
6. [क्या Counter Output Aurora Format Match करता है?](#part-6)
7. [TX Pin Connections — Aurora TX physically कहाँ जाता है?](#part-7)
8. [Loopback बनाम Real Fiber — कौन सा और कब?](#part-8)
9. [Clock और Reset — Detailed Explanation](#part-9)
10. [ILA की क्यों ज़रूरत है? क्या Debug ज़रूरी है?](#part-10)
11. [Buffer/FIFO Handling — Race Conditions और Backpressure](#part-11)
12. [पूरा Closed-Loop Cycle — Counter → Controller → Switches → वापस](#part-12)
13. [Step-by-Step Vivado Implementation Guide](#part-13)
14. [KR260 पर Bring-Up और Testing (शुरू से)](#part-14)
15. [Q&A — हर सवाल जो आएगा](#part-15)

---

<a id="part-1"></a>
## Part 1: Big Picture — हम actually क्या बना रहे हैं?

### 1.1 Real System (production वाला)

```
RTDS Simulator (PC)  ──fiber──►  Aurora RX  ──►  AXI FIFO  ──►  A53 CPU (main.c, Axi_IO.c चलाता है)
                                                                       │
                                                                       │ controller duty
                                                                       │ cycles compute करता है
                                                                       ▼
RTDS Simulator (PC)  ◄──fiber──  Aurora TX  ◄──  AXI FIFO  ◄──  A53 CPU
```

Production में, **RTDS** एक real-time power-system simulator है जो हर 125 µs में fiber पर 37 words भेजता है। Kria board receive करता है, controller चलाता है, और 23 words वापस भेजता है।

### 1.2 Lab/Bench Test System (ये document)

हमारे desk पर RTDS नहीं है। तो हम उसे **FPGA के अंदर fake** करेंगे एक "Pattern Generator" से:

```
Pattern Generator (Verilog)  ──►  TX FIFO  ──►  Aurora TX  ─┐
                                                            │ (loopback —
                                                            │  TX wires
                                                            │  internally
                                                            │  RX wires से
                                                            │  जुड़ी हैं)
A53 CPU (controller)  ◄──  RX FIFO  ◄──  Aurora RX  ◄───────┘
            │
            ▼
        Duty cycles compute करता है, अपने TX path से 23 words वापस भेजता है
```

**Pattern generator का काम:** RTDS होने का नाटक करना। एक schedule पर 37 valid words generate करना, उन्हें Aurora से push करना, loopback के through वापस लाना, और हमारे existing software को process करने देना।

### 1.3 ये क्यों करना है?

1. **RTDS available नहीं** — RTDS hardware महंगा है और shared है। हम Kria पर अकेले develop कर सकते हैं।
2. **Link verify करना** — Real RTDS connect करने से पहले prove करें कि Aurora + FIFO + interrupt + software chain end-to-end काम करती है।
3. **अपनी speed पर debug** — RTDS strict 8 kHz पर data भेजता है; pattern generator को 1 Hz तक slow किया जा सकता है, pause किया जा सकता है, single-step हो सकता है।
4. **Reproducible test data** — Pattern generator predictable values भेजता है (जैसे counter 1, 2, 3, ...) ताकि हम verify कर सकें क्या receive हुआ।

<a id="part-2"></a>
## Part 2: Train Station की कहानी (Refresher)

पूरी guide में हम train station analogy use करेंगे। इसे एक बार याद कर लो:

| Real Component | Train Analogy |
|----------------|---------------|
| RTDS / Pattern Generator | **Train Driver** origin station पर — 37 wagons में cargo भरता है, departure schedule करता है |
| Pattern Generator output (AXI Stream) | **Train depot से निकल रही है** — एक line में wagons |
| TX FIFO | **Cargo loading platform** — wagons यहाँ थोड़ी देर wait करते हैं जब तक station signals set न हो जाएँ |
| Aurora TX | **Locomotive engine** — wagons को fiber-optic light pulses में encode करता है, track पर push करता है |
| Fiber optic cable / Loopback | **Railway track** — single rail, एक direction |
| Aurora RX | **Receiving locomotive** — light pulses को वापस wagons में decode करता है |
| RX FIFO | **Arrival platform** — wagons यहाँ station master के inspect करने के लिए wait करते हैं |
| Interrupt (RC flag) | **Platform बेल** — आखिरी wagon आने पर बजती है (TLAST=1) |
| A53 CPU | **Station Master** — सिर्फ तब काम करता है जब बेल बजे, wagon contents process करता है |
| ILA (debug) | **Security cameras** हर checkpoint पर — record करते हैं क्या हुआ, बाद में replay |

> **Train Tip:** इस document का हर step या तो "wagon भरना", "track पर निकलना", "platform पर पहुँचना", या "station master action" से map होता है। अगर कहीं अटक जाओ, यहाँ वापस आ जाना।

<a id="part-3"></a>
## Part 3: Block-by-Block Architecture

### 3.1 पूरा Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                            KR260 BOARD — FPGA SIDE (PL)                      │
│                                                                              │
│  ┌────────────────┐                                                          │
│  │  Pattern       │  M_AXIS                                                  │
│  │  Generator     │ ─────────►┌─────────────┐                                │
│  │  (counter.v)   │           │  TX AXIS    │ M_AXIS                         │
│  │                │           │  Data FIFO  │ ───────►┌──────────────────┐   │
│  │  37 word       │           │  (Xilinx)   │         │  Aurora 64B/66B  │   │
│  │  packets       │           └─────────────┘         │  (TX side)       │   │
│  │  generate करता │                                   │                  │   │
│  └────────────────┘                                   │  GTH transceiver │   │
│                                                       │  TX pin          │   │
│        ┌──────────────────────────────────────────────┤                  │   │
│        │  Internal PMA loopback YA external fiber    │  RX pin          │   │
│        ▼                                              │                  │   │
│  ┌────────────────┐                                   │  64B/66B encoder │   │
│  │  Aurora 64B/66B│                                   │  CRC/scrambler   │   │
│  │  (RX side)     │                                   └──────────────────┘   │
│  │                │                                                          │
│  │  64b को वापस   │  M_AXI_RX                                                │
│  │  AXI Stream    │ ─────────►┌─────────────┐                                │
│  │  में decode    │           │  AXI4-Stream│  AXI-Lite (CPU control)        │
│  └────────────────┘           │  FIFO       │ ◄═════════════════════════╗    │
│                               │  (PG080)    │                           ║    │
│                               │  RX side    │                           ║    │
│                               │             │ ── interrupt ─────────╗   ║    │
│                               └─────────────┘                       ║   ║    │
│                                                                     ║   ║    │
│  ┌──── ILA ──────────────────────────────────────────────────────┐  ║   ║    │
│  │  TDATA, TVALID, TREADY, TLAST, channel_up पर snoop           │  ║   ║    │
│  └───────────────────────────────────────────────────────────────┘  ║   ║    │
│                                                                     ║   ║    │
│ ════════════════════════════════════════════════════════════════════╬═══╬════│
│                            PROCESSING SYSTEM (PS)                   ║   ║    │
│                                                                     ║   ║    │
│  ┌────────────────────────────────────────────────────────────┐    ║   ║    │
│  │  A53 CPU हमारी Vitis bare-metal app चला रहा:              │    ║   ║    │
│  │   - main.c (interrupt setup, FifoHandler)                 │    ║   ║    │
│  │   - Axi_IO.c (axiReceive, axiSend)                        │    ║   ║    │
│  │   - PEBB_Control_step() (auto-generated PLECS controller) │    ║   ║    │
│  │                                                            │ ◄══╝   ║    │
│  │  GIC interrupt receive करता है ─► FifoHandler ─► axiReceive ─►   │  ║    │
│  │   memory-mapped registers से RX FIFO से 37 words पढ़ता है │ ═══════╝    │
│  └────────────────────────────────────────────────────────────┘             │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 हर Block क्या करता है (Train Analogy के साथ)

#### Block 1 — Pattern Generator (`axis_pattern_generator.v`)
**Train role:** Train driver जो wagons भरता है।

- हम इसे Verilog में लिखते हैं (ये Xilinx IP नहीं है — हम बनाते हैं)।
- हर cycle, ये `m_axis_tdata` पर एक 32-bit word produce करता है।
- 37 words के बाद, `m_axis_tlast=1` assert करता है signal देने के लिए कि "train end हो गया"।
- फिर अगली 37-word train भेजने से पहले कुछ configurable time wait करता है (जैसे counter या timer)।

#### Block 2 — TX AXIS Data FIFO (Xilinx IP)
**Train role:** Loading platform जो wagons को थोड़ी देर hold करता है locomotive के pick करने तक।

- ये pattern generator और Aurora के बीच एक छोटा buffer है।
- क्यों चाहिए: Aurora हमेशा "ready" नहीं हो सकता (`tready=0`) — जैसे initialization के दौरान। FIFO इसे absorb करता है ताकि pattern generator produce करता रहे।
- Xilinx IP name: **AXI4-Stream Data FIFO** (note: ये छोटा simple FIFO है, interrupts वाले बड़े PG080 RX FIFO से अलग है)।
- Recommended depth: 64 से 256 entries।

#### Block 3 — Aurora 64B/66B IP (TX side)
**Train role:** वो locomotive जो wagons को railway track पर खींचता है।

- AXI Stream input लेता है (`s_axi_tx`)।
- हर word को **64B/66B line coding** से encode करता है (एक serial-link standard)।
- **Serial differential pair** output करता है (`gtX_txp` और `gtX_txn`) — ये GTH transceiver pins पर जाते हैं।
- Handshaking भी handle करता है (`channel_up` signal high होता है जब link establish हो जाता है)।

#### Block 4 — Loopback / Fiber Cable
**Train role:** Railway track खुद।

यहाँ दो options हैं (detail में [Part 8](#part-8) में):

1. **Internal PMA loopback** — Aurora के TX pins FPGA के serial transceiver के अंदर internally वापस RX pins से wire हैं। कोई external cable नहीं चाहिए। पहले bring-up के लिए ideal।
2. **External fiber loopback** — एक real fiber cable same board के TX SFP को RX SFP में plug करती है। Actual physical layer test करता है।
3. **External link to RTDS** — Real production setup।

#### Block 5 — Aurora 64B/66B IP (RX side)
**Train role:** Receiving locomotive जो wagons को destination platform पर वापस लाता है।

- 66-bit line-encoded data को plain 32-bit words में decode करता है।
- AXI Stream output करता है (`m_axi_rx`)।
- Packet end होने पर `m_axi_rx_tlast` assert करता है।

> Note: हमारे setup में, **एक ही Aurora IP block में TX और RX दोनों होते हैं** — वो एक IP के दो हिस्से हैं, अलग IPs नहीं।

#### Block 6 — RX FIFO (AXI4-Stream FIFO, PG080)
**Train role:** Arrival platform station bell के साथ।

- ये **बड़ा, smart FIFO** है — Xilinx PG080 — TX side के छोटे Data FIFO से अलग।
- इसमें memory-mapped registers (AXI-Lite) हैं जो CPU `Axi_IO.c` में `XLlFifo_*` driver functions से read करता है।
- इसमें **interrupt output** है जो PS side के GIC (General Interrupt Controller) पर जाता है।
- TLAST receive होने पर **RC (Receive Complete)** interrupt assert करता है।
- Software 37 बार `XLlFifo_RxGetWord()` call करता है इसे drain करने के लिए।

#### Block 7 — ILA (Integrated Logic Analyzer)
**Train role:** Multiple checkpoints पर security cameras।

- Xilinx debug IP. FPGA के अंदर BRAM में real-time signal values record करता है।
- हम इसे ("probe") सारे AXI Stream signals से connect करते हैं।
- Run के बाद, हम Vivado Hardware Manager खोलते हैं और waveform देखते हैं कि wires पर actually क्या हुआ।
- More in [Part 10](#part-10).

#### Block 8 — A53 CPU (PS)
**Train role:** Station master जो बेल बजने पर action लेता है।

- हमारा existing software (`main.c` + `Axi_IO.c`) चलाता है — production से unchanged।
- RX FIFO से interrupt receive करता है → 37 words पढ़ता है → controller चलाता है → TX path से 23 words भेजता है।

<a id="part-4"></a>
## Part 4: .v file में `m_axis` क्या है? — Line by Line

### 4.1 AXI Stream क्या है? (Quick Recap)

AXI Stream एक simple unidirectional protocol है जो FPGA के अंदर एक block से दूसरे block तक data move करने के लिए use होता है। ये blocks के बीच "highway" है।

इसमें **5 main signals** हैं (हम इन्हें "AXI Stream signals" कहते हैं):

| Signal | Direction | मतलब |
|--------|-----------|---------|
| `tdata[31:0]` | output | Actual 32-bit data word |
| `tvalid` | output | "मेरे पास अभी valid data है" — sender कहता है |
| `tready` | input | "मैं अभी accept कर सकता हूँ" — receiver कहता है |
| `tlast` | output | "ये packet का last word है" |
| `aclk` | input | Clock — सब कुछ इसके synchronous है |
| `aresetn` | input | Active-low reset (`n` = "not", यानी 0 means reset) |

Transfer **सिर्फ उस clock edge पर होता है जहाँ `tvalid=1` AND `tready=1` simultaneously** हों। इसे "handshake" कहते हैं।

### 4.2 `m_axis` का क्या मतलब है? — Naming Convention

`m_axis` = **M**aster **AXI S**tream port.

AXI में, हर connection के दो sides होते हैं:
- **Master** = वो side जो data produce करती है (भेजती है)
- **Slave** = वो side जो data consume करती है (receive करती है)

Xilinx tools में naming conventions:
- Signals जो `m_axis_*` से शुरू होते हैं → ये block MASTER है (data output करता है)।
- Signals जो `s_axis_*` से शुरू होते हैं → ये block SLAVE है (data receive करता है)।

जब आप अपनी custom RTL को Vivado में IP के रूप में package करते हो, tool इन naming patterns के basis पर **automatically AXI Stream interface detect** कर लेता है। इसीलिए हमें **exact यही names** use करने ज़रूरी हैं।

### 4.3 Pattern Generator की `.v` File — Skeleton

```verilog
module axis_pattern_generator #(
    parameter PACKET_LENGTH = 37,        // per train 37 words
    parameter INTER_PKT_GAP = 32'd100000 // trains के बीच इतने clocks wait
)(
    input  wire        aclk,             // clock — Aurora user clock जैसा
    input  wire        aresetn,          // active-low reset

    // AXI Stream MASTER port — ये block data PRODUCE करता है
    output wire [31:0] m_axis_tdata,     // 32-bit data
    output wire        m_axis_tvalid,    // "data valid है"
    input  wire        m_axis_tready,    // "downstream ready है" (FIFO accept करता है)
    output wire        m_axis_tlast      // "packet का last word"
);
    // ... internal logic ...
endmodule
```

**हर signal की train analogy:**

- `m_axis_tdata` = wagon के अंदर का cargo (आप क्या भेज रहे हो)
- `m_axis_tvalid` = conductor green flag wave कर रहा है ("निकलने के लिए ready!")
- `m_axis_tready` = अगला station reply करता है "हाँ, भेज दो" (handshake)
- `m_axis_tlast` = LAST WAGON पर red flag लगा है ("इस train का end")
- Wagon तभी move होता है जब **दोनों** flags up हों: sender से green + receiver से green-back।

### 4.4 इन Names के साथ Vivado क्या करता है

जब आप **Tools → Create and Package New IP** click करते हो:

1. Vivado आपकी `.v` file की port list scan करता है।
2. वो `m_axis_tdata`, `m_axis_tvalid`, `m_axis_tready`, `m_axis_tlast` देखता है।
3. इन्हें auto-group करके एक **AXI4-Stream Master interface** बनाता है जिसका नाम `m_axis` होता है।
4. अब Block Design GUI में, ये पूरा bundle एक single connection point की तरह दिखता है — आप बस अपने IP के `m_axis` pin से FIFO के `s_axis` pin तक एक wire drag करते हो।

**अगर आपने names misspell किए** (जैसे `tdat` instead of `tdata`, या `m_axi_tdata` बिना `s` के), तो Vivado इन्हें AXI Stream के रूप में recognize नहीं करेगा → आपको हर signal individually wire करना पड़ेगा, जो messy और error-prone है।

### 4.5 "s_axis" Side (ये क्यों matter करता है)

TX FIFO में एक **slave** AXI Stream input port होता है, जिसका नाम `s_axis` है।

```
Pattern Generator                 TX FIFO
    │                                │
    │  m_axis_tdata    ───────►      s_axis_tdata
    │  m_axis_tvalid   ───────►      s_axis_tvalid
    │  m_axis_tready   ◄───────      s_axis_tready
    │  m_axis_tlast    ───────►      s_axis_tlast
```

> Master output → Slave input. हमेशा।

<a id="part-5"></a>
## Part 5: Counter / Pattern Generator Design — पूरी Verilog

### 5.1 "Counter" Pattern Generator क्यों?

सबसे simple pattern generator एक **counter** है: हर word वो भेजता है वो बस एक incrementing integer होता है (1, 2, 3, ...)। इससे correctness verify करना trivial हो जाता है:
- अगर हम 1, 2, 3, ..., 37 भेजें और वापस 1, 2, 3, ..., 37 receive हो → link काम कर रहा है।
- अगर हम 1, 2, 7, 4, ... receive करें → link corrupted है।
- अगर हम 1, 2, 3 receive करें फिर कुछ नहीं → link word 3 के बाद टूट गया।

लेकिन **हमारे specific system के लिए**, pure counter काफी नहीं है। Software (`Axi_IO.c`) expect करता है:
- Word 35 = `run` flag (1 होना चाहिए, वरना controller नहीं चलेगा)
- Words 2–34 के ज़्यादातर = IEEE 754 floats (एक counter value garbage float के रूप में interpret होगी, NaN हो सकती है → CHIL_error fire)
- Word 37 = sequence number (1 to 1000, increments)

**इसलिए हमें एक "smart counter" pattern generator चाहिए** जो वो values produce करे जो हमारा software accept करे।

### 5.2 `Axi_IO.c` पढ़ना — हमें क्या Format Produce करना है?

`Axi_IO.h` से:
```c
#define WORD_SIZE 4              // per word 4 bytes
#define RECEIVE_LENGTH 37        // exactly 37 words per packet
#define UNIT_SCALE 1000          // RTDS kV/kA भेजता है, code V/A expect करता है
#define SEQ_MAX 1000             // sequence number max value
```

`Axi_IO.c` से — 37-word RX layout:

| Word # | Variable | `axiType` | `localType` | Scale | क्या value भेजनी है |
|--------|----------|-----------|-------------|-------|--------------------|
| 1  | `key` | s32 | s32 | 1 | कोई भी int (जैसे 0xCAFE0001) |
| 2  | `feedback[0]` (i_high A) | float | double | 1000 | float, जैसे 1.5 (= 1.5 kA → 1500 A) |
| 3  | `feedback[4]` (vhigh B) | float | double | 1000 | float, जैसे 11.0 |
| 4  | `feedback[3]` (vhigh A) | float | double | 1000 | float, जैसे 11.0 |
| 5  | `feedback[7]` (v_dcc) | float | double | 1000 | float, जैसे 12.0 |
| 6  | `feedback[6]` (i_low) | float | double | 1000 | float, जैसे 0.5 |
| 7  | dummy | float | double | 1000 | कोई भी float (जैसे 0.0) |
| 8  | `PEBB_NEXT_STATE_CMD` | float | double | 1 | कोई भी float |
| 9  | `P_CMD` | float | double | 1000 | float, जैसे 5.0 |
| 10 | `LOWSIDE_CONTACTOR_CMD` | float | double | 1 | float, जैसे 1.0 |
| 11 | `PEBB_POWER_LOWER_LIMIT` | float | double | 1e6 | float, जैसे 0.0 |
| 12 | `PEBB_POWER_UPPER_LIMIT` | float | double | 1e6 | float, जैसे 10.0 |
| 13 | dummy | float | double | 1000 | कोई भी float (0.0 OK) |
| 14 | dummy | float | double | 1000 | कोई भी float (0.0 OK) |
| 15 | `ctr` (substep counter) | s32 | s32 | 1 | int counter |
| 16-19 | dummies | float | double | 1000 | कोई भी non-NaN float |
| 20 | `V_CMD` | float | double | 1000 | float, जैसे 11.0 |
| 21 | `ForceFault` | float | double | 1 | float, जैसे 0.0 (no fault) |
| 22-25 | dummies | float | double | 1 | कोई भी non-NaN float |
| 26 | `EN_GRID_SUPPORT` | float | double | 1 | float, जैसे 0.0 |
| 27 | `Q_CMD` | float | double | 1000 | float, जैसे 0.0 |
| 28 | `Q_LIMIT` | float | double | 1e6 | float, जैसे 5.0 |
| 29 | `feedback[1]` (i_high B) | float | double | 1000 | float |
| 30 | `feedback[2]` (i_high C) | float | double | 1000 | float |
| 31 | `feedback[5]` (v_high C) | float | double | 1000 | float |
| 32-34 | `feedback[8..10]` (v_stator) | float | double | 1000 | float |
| 35 | `run` | s32 | s32 | 1 | **MUST BE 1** controller चलाने के लिए |
| 36 | `RXI_chil` | s32 | s32 | 1 | int (0 या 1) |
| 37 | `seq` (sequence number) | s32 | s32 | 1 | counter 1..1000, increments |

> **CRITICAL:** Float words valid IEEE 754 floats होने चाहिए — NaN नहीं। NaN `CHIL_error |= 1` set करता है और UART log में error print करता है।

### 5.3 IEEE 754 Float Encoding — Number कैसे भेजें

`1.5` float value को 32 bits के रूप में भेजने के लिए, आप IEEE 754 single-precision में encode करते हो:

| Bits | Field | 1.5 के लिए Value |
|------|-------|---------------|
| [31] | sign | 0 (positive) |
| [30:23] | exponent (biased) | 127 (0x7F) |
| [22:0] | mantissa | 0x400000 |

तो `1.5f` hex में = `0x3FC00000`।

**Common preset values जो आप Verilog में hard-code कर सकते हो:**

| Value | IEEE 754 hex |
|-------|--------------|
| 0.0 | `32'h00000000` |
| 1.0 | `32'h3F800000` |
| 1.5 | `32'h3FC00000` |
| 2.0 | `32'h40000000` |
| 5.0 | `32'h40A00000` |
| 10.0 | `32'h41200000` |
| 11.0 | `32'h41300000` |
| 12.0 | `32'h41400000` |
| -1.0 | `32'hBF800000` |
| 100.0 | `32'h42C80000` |
| 1000.0 | `32'h447A0000` |

> **Important:** Verilog के अंदर floats convert करने की कोशिश मत करो — paper पर या Python में pre-compute करो (`struct.pack('<f', 1.5).hex()`) और hex hard-code करो। Verilog में float math करने के लिए DSP/floating-point IP चाहिए, जो test pattern के लिए overkill है।

### 5.4 Smart Counter Generator — पूरी Verilog

ये generator 37 valid words का fixed packet भेजता है, हर packet पर sequence number increment करता है, और inter-packet gap use करता है।

```verilog
//-------------------------------------------------------------------------
// axis_pattern_generator.v
// 37-word AXI4-Stream pattern generator जो RTDS output mimic करता है।
// Valid IEEE-754 floats और integers produce करता है उस format में जो
// A53 PS side पर Axi_IO.c expect करता है।
//-------------------------------------------------------------------------
module axis_pattern_generator #(
    parameter integer PACKET_LENGTH = 37,
    parameter integer INTER_PKT_GAP = 32'd1_000_000  // ~10 ms at 100 MHz
)(
    input  wire        aclk,
    input  wire        aresetn,

    output reg  [31:0] m_axis_tdata,
    output reg         m_axis_tvalid,
    input  wire        m_axis_tready,
    output reg         m_axis_tlast
);

    // ROM जो 37 fixed payload values hold करता है।
    // Index 0 = word 1, index 36 = word 37.
    // Words 1, 15, 35, 36, 37 integers हैं; बाकी सब IEEE-754 float है।
    reg [31:0] payload [0:36];

    initial begin
        payload[0]  = 32'hCAFE0001;     // Word 1: key (s32)
        payload[1]  = 32'h3FC00000;     // Word 2: i_high     = 1.5 kA
        payload[2]  = 32'h41300000;     // Word 3: vhigh_B    = 11.0 kV
        payload[3]  = 32'h41300000;     // Word 4: vhigh_A    = 11.0 kV
        payload[4]  = 32'h41400000;     // Word 5: v_dcc      = 12.0 kV
        payload[5]  = 32'h3F000000;     // Word 6: i_low      = 0.5 kA
        payload[6]  = 32'h00000000;     // Word 7: dummy
        payload[7]  = 32'h00000000;     // Word 8: NEXT_STATE = 0.0
        payload[8]  = 32'h40A00000;     // Word 9: P_CMD      = 5.0 MW
        payload[9]  = 32'h3F800000;     // Word 10: LOWSIDE_CONTACTOR = 1.0
        payload[10] = 32'h00000000;     // Word 11: P_LOW     = 0.0
        payload[11] = 32'h41200000;     // Word 12: P_UP      = 10.0
        payload[12] = 32'h00000000;     // Word 13: dummy
        payload[13] = 32'h00000000;     // Word 14: dummy
        // Word 15 (ctr) dynamic है — नीचे substep_counter देखो
        payload[15] = 32'h00000000;     // Word 16: dummy
        payload[16] = 32'h00000000;     // Word 17: dummy
        payload[17] = 32'h00000000;     // Word 18: dummy
        payload[18] = 32'h00000000;     // Word 19: dummy
        payload[19] = 32'h41300000;     // Word 20: V_CMD     = 11.0
        payload[20] = 32'h00000000;     // Word 21: ForceFault= 0.0 (no fault)
        payload[21] = 32'h00000000;     // Word 22: dummy
        payload[22] = 32'h00000000;     // Word 23: dummy
        payload[23] = 32'h00000000;     // Word 24: dummy
        payload[24] = 32'h00000000;     // Word 25: dummy
        payload[25] = 32'h00000000;     // Word 26: EN_GRID   = 0.0
        payload[26] = 32'h00000000;     // Word 27: Q_CMD     = 0.0
        payload[27] = 32'h40A00000;     // Word 28: Q_LIMIT   = 5.0
        payload[28] = 32'h3FC00000;     // Word 29: i_high_B  = 1.5
        payload[29] = 32'h3FC00000;     // Word 30: i_high_C  = 1.5
        payload[30] = 32'h41400000;     // Word 31: v_high_C  = 12.0
        payload[31] = 32'h41300000;     // Word 32: v_stator_A= 11.0
        payload[32] = 32'h41300000;     // Word 33: v_stator_B= 11.0
        payload[33] = 32'h41300000;     // Word 34: v_stator_C= 11.0
        payload[34] = 32'h00000001;     // Word 35: run = 1   (REQUIRED!)
        payload[35] = 32'h00000001;     // Word 36: RXI_chil = 1
        // Word 37 (seq) dynamic है — नीचे seq_counter देखो
    end

    // FSM states
    localparam ST_IDLE  = 2'd0;
    localparam ST_SEND  = 2'd1;
    localparam ST_GAP   = 2'd2;

    reg [1:0]  state;
    reg [5:0]  word_idx;          // 0..36 (6 bits काफी हैं)
    reg [31:0] gap_count;
    reg [31:0] substep_counter;   // word 15 (ctr) के लिए — हर packet पर increment
    reg [31:0] seq_counter;       // word 37 (seq) के लिए — 1..1000 wraps

    always @(posedge aclk) begin
        if (!aresetn) begin
            state            <= ST_IDLE;
            word_idx         <= 6'd0;
            gap_count        <= 32'd0;
            substep_counter  <= 32'd0;
            seq_counter      <= 32'd1;     // 1 से start
            m_axis_tdata     <= 32'd0;
            m_axis_tvalid    <= 1'b0;
            m_axis_tlast     <= 1'b0;
        end else begin
            case (state)
                //----------------------------------------------------------
                // IDLE: नया packet भेजने की तैयारी
                //----------------------------------------------------------
                ST_IDLE: begin
                    word_idx      <= 6'd0;
                    m_axis_tvalid <= 1'b1;
                    m_axis_tlast  <= 1'b0;
                    m_axis_tdata  <= payload[0];   // word 1 = key
                    state         <= ST_SEND;
                end

                //----------------------------------------------------------
                // SEND: हर accepted handshake पर एक word stream करो
                //----------------------------------------------------------
                ST_SEND: begin
                    if (m_axis_tvalid && m_axis_tready) begin
                        // word अभी accept हुआ — advance
                        if (word_idx == PACKET_LENGTH - 1) begin
                            // last word अभी भेजा गया — gap में जाओ
                            m_axis_tvalid <= 1'b0;
                            m_axis_tlast  <= 1'b0;
                            gap_count     <= 32'd0;
                            substep_counter <= substep_counter + 1;
                            seq_counter   <= (seq_counter == SEQ_MAX) ?
                                             32'd1 : seq_counter + 1;
                            state         <= ST_GAP;
                        end else begin
                            word_idx <= word_idx + 1;
                            // next word का data pick करो
                            case (word_idx + 1)  // word_idx next cycle में ये value होगी
                                6'd14: m_axis_tdata <= substep_counter;          // word 15 = ctr
                                6'd36: m_axis_tdata <= seq_counter;              // word 37 = seq
                                default: m_axis_tdata <= payload[word_idx + 1]; // ROM
                            endcase
                            // LAST word पर TLAST assert करो
                            m_axis_tlast <= (word_idx + 1 == PACKET_LENGTH - 1);
                        end
                    end
                    // अगर !tready, तो tvalid/tdata/tlast steady hold करो (AXIS rule)
                end

                //----------------------------------------------------------
                // GAP: अगले packet से पहले INTER_PKT_GAP cycles wait
                //----------------------------------------------------------
                ST_GAP: begin
                    if (gap_count == INTER_PKT_GAP - 1) begin
                        state <= ST_IDLE;
                    end else begin
                        gap_count <= gap_count + 1;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end

    // Helper localparam
    localparam SEQ_MAX = 32'd1000;

endmodule
```

**इस module की train story:**

1. **IDLE state** — engine depot पर बैठा है। Driver आता है, wagon 1 उठाता है, "ready to depart" signal देता है (`tvalid=1`)।
2. **SEND state** — हर cycle जहाँ next station कहता है "send it" (`tready=1`), एक wagon निकलता है। Wagon 36 send होने के बाद, wagon 37 पर red flag होती है (`tlast=1`)।
3. **GAP state** — engine depot वापस जाता है, fixed clocks wait करता है (जैसे 100 MHz पर 10 ms = 1,000,000 cycles), ताकि receiving station के पास process करने का time हो अगली train आने से पहले।
4. **Sequence counter** — हर train को नई ID मिलती है (1, 2, ..., 1000, फिर wrap)।

### 5.5 INTER_PKT_GAP कैसे choose करें

Real RTDS हर **125 µs (8 kHz)** में एक packet भेजता है। Test purposes के लिए, आप इसे slow रखना चाह सकते हो debug करने के लिए:

| Goal | Clock @ 100 MHz | INTER_PKT_GAP value |
|------|-----------------|---------------------|
| Real RTDS rate match | 125 µs | 12,500 |
| 1 packet per ms (slower) | 1 ms | 100,000 |
| 1 packet per 10 ms | 10 ms | 1,000,000 |
| 1 packet per second (बहुत slow, easy to single-step) | 1 s | 100,000,000 |

> Tip: बहुत slow से शुरू करो (1 packet/sec)। Verify करो सब काम कर रहा है। फिर speed up करो।

### 5.6 ये Counter Output हमारे Software के लिए VALID क्यों है

| Software Check | इसे हम कैसे Pass करते हैं |
|----------------|----------------|
| `ReceiveLength == 37` | हम exactly 37 words भेजते हैं और सिर्फ word 37 पर `tlast` assert करते हैं। |
| No NaN floats | सारे hard-coded floats valid IEEE 754 हैं (0.0, 1.0, 5.0, etc.)। |
| `run == 1` | Word 35 hard-coded `32'h00000001` पर। |
| Sequence number reasonable | `seq_counter` 1 से start, 1000 पर wraps। |
| Stable repeated packets | हर packet की identical structure, same payload values। |

<a id="part-6"></a>
## Part 6: क्या Counter Output Aurora Format Match करता है?

### 6.1 Short Answer

**नहीं — पर आपको चिंता करने की ज़रूरत नहीं।** Aurora IP खुद आपके लिए AuroraLink/64B-66B encoding करता है। आपका pattern generator बस clean **AXI Stream** (TDATA + TVALID + TREADY + TLAST) produce करता है। Aurora उसे अपनी framing में wrap करता है।

### 6.2 कौन-कौन सी Layers हैं?

```
Layer 4 — Application data         आपके 37 × 32-bit words (जो हम generate करते हैं)
            ↓
Layer 3 — AXI Stream framing       TDATA + TVALID + TLAST  (जो हम output करते हैं)
            ↓
Layer 2 — AuroraLink framing       Aurora IP add करता है: SOF marker, K-codes, CRC, idle codes
            ↓
Layer 1 — 64B/66B line coding      Aurora IP 64 bits → 66 bits encode करता है, scrambles
            ↓
Layer 0 — Serial differential pair Bits GTH transceiver fiber/loopback पर जाते हैं
```

आपको सिर्फ Layer 4 और Layer 3 की चिंता है। Aurora IP 0, 1, 2 handle करता है।

### 6.3 "AuroraLink Format" क्या Count होता है?

जब लोग "AuroraLink format" कहते हैं, वो often मतलब करते हैं:

1. **Wire format** — fiber पर actually कौन से bits flow करते हैं। ये Aurora IP की configuration (line rate, lane count, 64B/66B vs 8B/10B) से determine होता है। दोनों ends पर इन settings पर agree होना चाहिए।
2. **Packet structure** — TLAST framing, packet length। ये आपकी AXI Stream signaling से determine होता है।

अगर RTDS expect करता है "exactly 37 × 32-bit words TLAST last पर वाले packets", तो आपका pattern generator output सही format में है। RTDS को इससे फर्क नहीं पड़ता कि contents floats हैं या counters — वो बस framing देखता है।

### 6.4 "Smart" Counter बनाम Pure Counter

| Approach | Output | क्या SOFTWARE accept करेगा? | Use case |
|----------|--------|------------------------------|----------|
| **Pure counter** (1, 2, 3, ..., 37) | 37 incrementing ints | NO — float words invalid हो जाते हैं (small ints denormal floats जैसे दिखते हैं, NaN check trigger हो सकता है) | सिर्फ raw link bring-up के लिए useful — verify Aurora वही numbers deliver करता है जो हम put करते हैं। |
| **Smart counter** (Section 5.4) | Valid floats + run=1 + seq | YES — controller चलेगा, no error flags | End-to-end testing including controller के लिए इसे use करो। |

**Recommendation:** दोनों build करो और parameter या multiplexer से switch करो। पहले pure counter use करो; link काम करने पर smart counter में switch करो।

<a id="part-7"></a>
## Part 7: TX Pin Connections — Aurora TX Physically कहाँ जाता है?

### 7.1 KR260 Hardware

Kria KR260 carrier card में multiple physical interfaces हैं जो Aurora drive कर सकते हैं:

| Interface | Connector type | Signal pair | Usage |
|-----------|---------------|-------------|-------|
| **SFP+ cage 0** | SFP+ optical/copper module slot | `MGTH_TX0_P/N` और `MGTH_RX0_P/N` | Aurora के लिए सबसे common — fiber transceiver plug करो |
| **SFP+ cage 1** | SFP+ optical/copper module slot | `MGTH_TX1_P/N` और `MGTH_RX1_P/N` | Second SFP+, second Aurora link के लिए use हो सकता |
| **SLVS-EC connector** | Cameras के लिए high-speed SLVS-EC | Different pins | Aurora नहीं |
| **PMOD connectors** | LVCMOS GPIOs | N/A | Aurora के लिए नहीं — ये slow GPIOs हैं |

> **Aurora को high-speed serial transceiver (Zynq UltraScale+ पर GTH) चाहिए।** ये regular GPIO pins पर नहीं चल सकता। KR260 पर, इसका मतलब है SFP+ cages।

### 7.2 GTH Transceiver

Zynq UltraScale+ (KR260 की chip) के अंदर, dedicated **multi-gigabit transceivers** हैं जिन्हें **GTH** कहते हैं। ये only physical pins हैं जो Aurora कर सकते हैं।

```
      FPGA के अंदर
      ┌─────────────────────┐
      │   Aurora 64B/66B IP │
      │                     │
      │   parallel data     │
      │   (32 या 64 bits)   │
      └──────┬──────────────┘
             │
             ▼
      ┌─────────────────────┐
      │   GTH Quad          │   "Quad" = 4 transceivers + 1 reference clock PLL का group
      │   ┌─────────────┐   │
      │   │ Channel 0   │   │  ← Aurora 1 channel (lane) use करता है
      │   │  (TX SerDes)│   │
      │   │  (RX SerDes)│   │
      │   └─────────────┘   │
      └─────┬───────┬───────┘
            │       │
       MGTH_TX_P  MGTH_TX_N    ← differential pair (2 physical pins)
       MGTH_RX_P  MGTH_RX_N
            │       │
            ▼       ▼
       (KR260 carrier card पर SFP+ cage)
            │
            ▼
       Optical fiber (या copper SFP+ DAC cable)
            │
            ▼
       (दूसरा end — RTDS या loopback)
```

### 7.3 Pin Assignments (KR260 Schematic में Look Up करो)

Exact pin numbers इनसे आते हैं:
- KR260 user guide / schematic (Xilinx UG1092 या PG)
- Vivado की **KR260 board file**, जो board target set करने पर pins auto-assign करती है

जब आप Vivado project create करते हो और **KR260** को board choose करते हो, फिर **Aurora 64B/66B** IP add करते हो, Vivado का IP integrator:

1. एक "Board" tab show करेगा जहाँ आप Aurora IP के GTH interface को board interface (जैसे "SFP+ Connector 0") से map कर सकते हो।
2. XDC pin constraints auto-generate करता है ताकि आपको manually pin numbers type न करने पड़ें।

**अगर board flow use नहीं करते**, तो आपको constraint file (`.xdc`) लिखनी पड़ेगी जैसे:
```tcl
# Example XDC for KR260 SFP+0
set_property PACKAGE_PIN <pin_letter_number> [get_ports {gt_txp[0]}]
set_property PACKAGE_PIN <pin_letter_number> [get_ports {gt_txn[0]}]
set_property PACKAGE_PIN <pin_letter_number> [get_ports {gt_rxp[0]}]
set_property PACKAGE_PIN <pin_letter_number> [get_ports {gt_rxn[0]}]
# Reference clock pin
set_property PACKAGE_PIN <pin_letter_number> [get_ports {gt_refclk_p}]
set_property PACKAGE_PIN <pin_letter_number> [get_ports {gt_refclk_n}]
```

Pin letters/numbers fill करने के लिए KR260 schematic use करो। Board-file flow strongly recommended है — ये आपके लिए handle कर लेता है।

### 7.4 Reference Clock

Aurora self-clock नहीं हो सकता — इसे external **reference clock** चाहिए (typically 156.25 MHz 10 Gbps line rate के लिए)। KR260 पर:

- SFP+ cage में Si570 programmable oscillator है जो ये reference clock provide करता है।
- Aurora IP wizard आपसे reference clock source select करने को कहता है — वो pick करो जो आपके SFP+ cage से tied है।

<a id="part-8"></a>
## Part 8: Loopback बनाम Real Fiber — कौन सा और कब?

### 8.1 तीन Options की Comparison

| Mode | क्या करता है | Hardware चाहिए | क्या Test करता है? |
|------|-------------|-----------------|-------------|
| **A. Internal PMA Near-End Loopback** | TX serial output GTH transceiver के अंदर RX serial input से wire है | कुछ नहीं — Aurora IP में software setting | सिर्फ digital chain (Aurora encoder, FIFO, software)। Physical layer skip करता है। |
| **B. External fiber loopback** | Same board के SFP+0 TX से SFP+0 RX (या SFP+1) पर short fiber cable | 1 SFP+ optical transceiver + 1 fiber jumper | Test में actual physical/optical layer add करता है। |
| **C. RTDS के साथ external link** | KR260 SFP+ से RTDS aurora card तक real cable | Production setup | Complete production path। |

### 8.2 Train Analogy

- **A. Internal PMA Loopback** = train depot से निकलती है, पर real track पर जाने के बजाय, engineers depot का exit door directly वापस इसके entrance पर wire कर देते हैं। Train depot के अंदर ही circle करती है।
  - Pros: Tracks नहीं चाहिए। Cheap test।
  - Cons: Rails काम करते हैं ये prove नहीं करता।

- **B. External fiber loopback** = train निकलती है, short curved track पर चलती है जो तुरंत same station में वापस आ जाती है।
  - Pros: Actual rail और points test करता है (physical fiber + SFP+)।
  - Cons: Hardware चाहिए (SFP module + fiber)।

- **C. External to RTDS** = next city तक real journey।
  - Pros: Production reality।
  - Cons: RTDS available होना चाहिए।

### 8.3 Internal PMA Loopback कैसे Enable करें

Aurora 64B/66B IP wizard में (Vivado):

1. IP customization GUI open करो (Block Design में Aurora IP पर double-click)।
2. **Debug and Control** tab navigate करो (या similar — exact name IP version पर depend करता है)।
3. एक "Loopback" parameter हो सकता है — initial bring-up के लिए **Near-End PMA Loopback** set करो।

**OR** — बेहतर तरीका — **Aurora IP के `loopback[2:0]` input को dynamically drive करो**। ये आपको runtime पर switch या AXI register से loopback toggle करने देता है बिना re-synthesizing।

Aurora IP में `loopback[2:0]` input port है। Values:

| `loopback[2:0]` | मतलब |
|------------------|---------|
| `3'b000` | Normal operation (no loopback) — real fiber/RTDS के लिए चाहिए |
| `3'b001` | Near-End PCS Loopback (encoder के बाद, serializer से पहले) |
| `3'b010` | Near-End PMA Loopback (serializer के बाद, pins से पहले) ← **सबसे useful** |
| `3'b100` | Far-End Loopback (remote tester द्वारा use) |

हमारे setup के लिए:
- FPGA bring-up के दौरान → `loopback = 3'b010` tie करो (BD में constant)।
- Real fiber के लिए → `loopback = 3'b000` tie करो।
- Runtime toggle के लिए → `loopback` को `vio` (Virtual IO) IP या GPIO bit से connect करो।

### 8.4 Loopback में TLAST का क्या होता है?

> **Architecture से उठा important सवाल:** PMA loopback में, क्या TLAST round-trip survive करता है?

**हाँ।** Aurora 64B/66B link के पार AXI Stream framing preserve करता है। जो भी TVALID/TLAST patterns आप `s_axi_tx` पर put करते हो, वही `m_axi_rx` पर वापस मिलते हैं। ये पूरा point है Aurora का "framing interface" use करने का — ये transparent है।

तो TX पर 37-word packet send → RX पर 37-word packet आता है, TLAST अभी भी word 37 पर।

### 8.5 हमारे Bring-Up के लिए Recommendation

```
Phase 1: Bare counter generator → loopback (PMA) → सिर्फ ILA।
         Goal: ILA में counter values RX पर वापस आते देखो। Software involved नहीं।

Phase 2: Smart counter generator → loopback (PMA) → RX FIFO → A53 software।
         Goal: A53 interrupt receive करे, axiReceive() variables correctly load करे।

Phase 3: Smart counter → loopback → A53 controller चले → A53 TX path → loopback again।
         Goal: Full closed loop. SECOND pattern generator instance use करो OR "mirror"
         block जो RTDS-side data feed करे; OR accept करो कि A53 TX खुद से loop back
         नहीं करेगा (सिर्फ एक direction)।

Phase 4: Loopback disable करो। Real RTDS को fiber से connect करो। Production test।
```

### 8.6 "Closed Loop" का सवाल — Important!

आपने लिखा:
> "document के अनुसार, counter generator का output जो kriakv260 board को दिया जाता है, उससे switches on/off पर actions लिए जाते हैं, और output एक variable में store होता है, फिर वो output simulator को वापस भेजा जाता है ताकि नया data produce हो, चलो सुनिश्चित करते हैं कि counter ये complete cycle handle कर सके।"

ये subtle है। Train analogy से clarify करते हैं:

**Production में**:
- RTDS 37-word train भेजता है → Kria receive करता है → controller compute करता है → Kria 23-word train वापस भेजता है → RTDS receive करता है, simulate करता है, नया 37-word train भेजता है। Loop!

**Lab में simple PMA loopback के साथ**:
- Pattern generator 37-word train भेजता है → TX से जाता है → वापस loops → RX के रूप में receive (37 words) → controller चलता है।
- BUT: जब controller TX से 23-word train भेजता है → वो भी RX के रूप में loops back — पर सिर्फ 23 words है! Software print करेगा "Warning: receive Length mismatch. Received 23 words. Expected 37."
- और worse: pattern generator से next generated 37-word packet controller के 23-word packet के साथ same TX FIFO/Aurora के लिए लड़ेगा।

**तो PURE loopback controller के साथ loop CLOSE नहीं करता।** ये सिर्फ एक direction का TX→RX path close करता है।

### 8.7 Lab में Truly Loop कैसे Close करें

"RTDS controller output के respond करता है" को genuinely simulate करने के लिए, तीन options हैं:

**Option A — दो separate Aurora links + bridge logic।**

```
Pattern Gen ──► TX0 FIFO ──► Aurora0 TX ──► loopback ──► Aurora0 RX ──► RX0 FIFO ──► A53
                                                                                      │
                                                                                      ▼
A53 ──► TX1 FIFO ──► Aurora1 TX ──► (no loopback — terminate या ILA only)
```

Controller TX कहीं नहीं जाता (हम बस verification के लिए ILA करते हैं)। Pattern gen हमेशा RX feed करता है। Simple पर closed-loop नहीं।

**Option B — "RTDS Emulator" custom block।**

Simple counter को smarter block से replace करो जो:
1. A53 के TX (23 words) सुनता है।
2. उन 23 words के basis पर internal state model update करता है (जैसे `switch_1[0]` duty याद रखता है)।
3. Next 37-word RX packet generate करता है जिसके `feedback[]` values simulated state reflect करते हैं।

ये essentially FPGA में tiny RTDS बनाना है। Complex — सिर्फ तब worth है जब RTDS long time के लिए unavailable हो।

**Option C — Decoupled lab test (हमारे scope के लिए recommended)।**

```
Phase A: Pattern Gen ──► TX → loopback → RX → A53 (हम reception verify करते हैं)।
Phase B: A53 ──► TX → ILA 23 words capture करता है (हम transmission verify करते हैं)।
```

हम loop CLOSE नहीं करते। हम हर direction separately test करते हैं। ये सबसे cleanest lab approach है जब तक हम actively RTDS replace नहीं कर रहे। User का pattern generator original doc से **Phase A** के लिए है।

### 8.8 Counter को क्या Send करना चाहिए (Closed-Loop Aware)

अगर हम Option C choose करते हैं, counter बस fixed schedule पर 37-word RTDS-format packet भेजता है। Controller से वापस आने वाले 23 words ILA से observe होते हैं; हम उन्हें FPGA में "consume" करने की कोशिश नहीं करते।

अगर हम कभी Option B (full RTDS emulator) करें, counter generator एक state-machine से replace होगा जो उस TX पर dependent packets produce करता है जो उसे receive हुआ। इस document का scope नहीं, पर यहाँ framework बाद में add करने को support करता है।

<a id="part-9"></a>
## Part 9: Clock और Reset — Detailed Explanation

ये वो part है जहाँ ज़्यादातर लोग अटक जाते हैं। ध्यान से पढ़ो।

### 9.1 इतने Clocks क्यों हैं?

FPGA में, different blocks को different reasons के लिए different clock speeds चाहिए:

| Clock | Typical frequency | किसके द्वारा driven | किसके द्वारा used |
|-------|-------------------|-----------|---------|
| **PS clock (PL clock 0)** | 100 MHz | Zynq MPSoC (PS से PL_CLK0) | Pattern generator, FIFOs, ILA, AXI-Lite slaves |
| **GT reference clock** | 156.25 MHz | KR260 पर external Si570 oscillator | Aurora का GTH PLL |
| **Aurora user clock** | 161 MHz (line rate के साथ vary) | Aurora IP खुद (output) | Aurora का user-side AXI Stream port |
| **AXI-Lite clock** | usually PS clock जैसा | PS_CLK0 | Aurora का `s_axi_lite` और FIFO control register |

### 9.2 Clocks के लिए Train Analogy

Multiple trains different time schedules पर चल रही हैं imagine करो:
- Local trains 100 mph पर (PS clock)।
- Express trains 161 mph पर (Aurora user clock)।
- GTH ("engine room") में signaling system अपनी atomic clock पर 156.25 MHz पर (GT reference)।

Local train से express train में wagon pass करने के लिए, आपको **transfer station** (FIFO) चाहिए जहाँ wagon थोड़ी देर wait करे। इसीलिए हम pattern generator (PS clock) और Aurora (user clock) के बीच FIFO लगाते हैं।

### 9.3 पूरा Clock Map

```
┌──────────────────────────────────────────────────────────────────────┐
│                          KR260 PS                                    │
│                                                                      │
│   ┌──────────┐                                                       │
│   │PS_CLK    │   pl_clk0 = 100 MHz (Zynq IP में configurable)        │
│   │  source  │ ────────────────────────────────────────►             │
│   └──────────┘                                                       │
└──────────┼───────────────────────────────────────────────────────────┘
           │
           │  (ये PL के सब के लिए `aclk` है except Aurora's user-side)
           ▼
┌──────────────────────────────────────────────────────────────────────┐
│                          KR260 PL (FPGA)                             │
│                                                                      │
│  ┌────────────────────┐                                              │
│  │ proc_sys_reset     │ ◄── pl_resetn from PS                        │
│  │ (synchronous       │ ◄── pl_clk0  from PS                         │
│  │  reset generator)  │                                              │
│  │                    │ ── peripheral_aresetn ─────────►   सारे    │
│  │                    │ ── interconnect_aresetn ───────►   blocks  │
│  └────────────────────┘                                              │
│                                                                      │
│  ┌────────────────┐  ┌─────────────┐    ┌──────────────────┐         │
│  │Pattern Gen     │  │ TX FIFO     │    │ Aurora 64B/66B   │         │
│  │aclk = pl_clk0  │  │ aclk = pl_  │    │                  │         │
│  │aresetn=periph_ │  │ clk0        │    │  init_clk = pl_  │         │
│  │aresetn         │  │ aresetn=    │    │      clk0 (या   │         │
│  └───────┬────────┘  │ peripheral_ │    │      अपनी own)  │         │
│          │           │ aresetn     │    │                  │         │
│          │ AXIS      │             │    │  user_clk = OUT  │         │
│          ▼           │             │    │      (Aurora IP  │         │
│      s_axis ────►    │ m_axis ────►│ s_axi_tx_aclk = AURORA_USER_CLK │
│                      └─────────────┘    │                  │         │
│                                         │  gt_refclk_p/n =                       │
│                                         │  external 156.25  │         │
│                                         │  MHz (board pin) │         │
│                                         └──────────────────┘         │
│                                                                      │
│  ┌────────────────────┐                                              │
│  │ AXI4-Stream FIFO   │                                              │
│  │ (RX, PG080)        │                                              │
│  │                    │                                              │
│  │ axis_aclk =        │                                              │
│  │   aurora_user_clk  │ ◄── note: Aurora के RX side के same clock   │
│  │                    │                                              │
│  │ axi_lite_aclk =    │                                              │
│  │   pl_clk0          │ ◄── register reads के लिए different clock  │
│  └────────────────────┘                                              │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### 9.4 Step-by-Step Connections

Vivado Block Design में precisely क्या connect करना है:

#### Pattern Generator
- `aclk` → `Zynq.pl_clk0`
- `aresetn` → `proc_sys_reset.peripheral_aresetn`

#### TX AXIS Data FIFO (छोटा, PatternGen और Aurora के बीच)
- `s_axis_aclk` → `pl_clk0`
- `s_axis_aresetn` → `peripheral_aresetn`
- `m_axis_aclk` → **Aurora का `user_clk_out`** (ये FIFO clock domains cross करता है!)
- `m_axis_aresetn` → user_clk से synchronized reset (Aurora provide करता है, OR second `proc_sys_reset` use करो जो `user_clk` पर चले)

> **ये एक clock-crossing FIFO है!** Pattern gen 100 MHz पर चलता है, Aurora user side 161 MHz पर। Data FIFO **asynchronous** (independent input/output clocks) configure होना चाहिए। FIFO IP configuration में set करो: "Clock Type" = "Independent Clocks"।

#### Aurora 64B/66B
- `init_clk` → `pl_clk0` (IP की internal init state machine के लिए)
- `gt_refclk1_p`, `gt_refclk1_n` → external 156.25 MHz (board pin — board flow handle करता है)
- `reset_pb` → `peripheral_aresetn` inverted, OR दूसरा reset signal (typically active high — IP doc check करो)
- `pma_init` → या तो low tied या "PMA reset" pulse से driven (wizard default use करो)
- `s_axi_tx_aclk` → Aurora से `user_clk_out` के रूप में आता है (ये इसकी own user clock है)
- `s_axi_tx_aresetn` → Aurora का own reset output use करो, OR synced reset

#### RX AXI4-Stream FIFO (PG080)
- `s_axis_aclk` → Aurora का `user_clk_out` (RX side)
- `s_axis_aresetn` → user_clk से synced reset
- `s_axi_aclk` (AXI-Lite control side) → `pl_clk0`
- `s_axi_aresetn` → `peripheral_aresetn`

#### proc_sys_reset (reset generator)
- `slowest_sync_clk` → `pl_clk0` (या जो भी सबसे slowest care करते हो — usually pl_clk0)
- `ext_reset_in` → Zynq का `pl_resetn` (या `pl_resetn0`)
- `peripheral_aresetn` → output, ज़्यादातर blocks पर fans out
- `interconnect_aresetn` → output, AXI interconnects द्वारा used

### 9.5 दो `proc_sys_reset` Instances?

अगर आपके design में multiple clock domains हैं (और हमारे में हैं — pl_clk0 + Aurora user_clk), तो आपको **TWO** `proc_sys_reset` IP instances चाहिए:

| Instance | `slowest_sync_clk` | Purpose |
|----------|-------------------|---------|
| `rst_pl_clk0` | `pl_clk0` | 100 MHz clock पर चलने वाले blocks reset करता है |
| `rst_aurora_user` | Aurora का `user_clk_out` | Aurora के 161 MHz clock पर चलने वाले blocks reset करता है |

दोनों Zynq का `pl_resetn0` अपने `ext_reset_in` के रूप में use कर सकते हैं। हर एक अपने clock domain के लिए synchronized active-low reset generate करता है।

### 9.6 Vivado का "Run Connection Automation" क्या करता है

जब आप BD में IP drop करने के बाद **Run Connection Automation** click करते हो:

- ये usually AXI-Lite paths के लिए clocks/resets automatically figure out करता है।
- ये streaming side के clock crossing को correctly handle नहीं कर सकता — manually verify करो।
- Aurora के लिए, ये user_clk side पर attempt भी नहीं कर सकता — आप typically वो खुद wire करते हो।

> **Tip:** Connection Automation को blindly accept मत करो। BD validation report open करो और सुनिश्चित करो कि हर block का `*_aclk` और `*_aresetn` sensible source से connected है।

### 9.7 Reset Order (Aurora के लिए Important)

Aurora का bring-up sequence sensitive है:

1. Power applied।
2. PS_CLK starts।
3. `pl_resetn0` deasserts (PS reset release करता है)।
4. `proc_sys_reset` clean synchronous resets produce करता है।
5. Aurora का `init_clk` starts → IP internal reset sequence perform करता है।
6. Aurora **PMA reset** perform करता है (`pma_init` pulse)।
7. Aurora दूसरे end (या loopback में खुद से) के साथ handshake attempt करता है।
8. Successful handshake के बाद, Aurora `channel_up = 1` assert करता है और `user_clk_out` producing start करता है।
9. AXI Stream interface अब usable है।

> अगर `channel_up` 0 पर stuck है, आपका link काम नहीं कर रहा। Check करो (a) ref clock present, (b) अगर fiber नहीं तो loopback enabled, (c) reset sequence complete।

<a id="part-10"></a>
## Part 10: ILA की क्यों ज़रूरत है? क्या Debug ज़रूरी है?

### 10.1 Short Answer

**ILA strongly recommended है पर strictly mandatory नहीं है।** First bring-up के दौरान ये essentially mandatory है क्योंकि इसके बिना आपके पास FPGA के अंदर AXI Stream wires पर क्या हो रहा है ये देखने का कोई तरीका नहीं है।

### 10.2 ILA क्या है?

ILA = **Integrated Logic Analyzer**। ये Xilinx debug IP है जो:

- FPGA fabric के अंदर रहता है।
- हर clock edge पर specified signals को on-chip BRAM buffer में sample करता है।
- Trigger होने पर (जैसे `tlast=1` पर), samples का window capture करता है (जैसे 1024 cycles)।
- आपको Vivado Hardware Manager में उन samples को waveform के रूप में view करने देता है — real oscilloscope जैसा, पर chip के अंदर digital signals के लिए।

### 10.3 Train Analogy

ILA = platform पर security cameras। बिना cameras के:
- Train आती है → conductor कहता है "passenger missing!" → आपको idea नहीं क्यों।
- Cameras के साथ: आप footage replay करते हो → "ah, passenger wagon 12 पर गिरा — wagon connection broken।"

### 10.4 ILA से क्या Probe करें — Recommended Signal List

ये signals probe करो (सब एक ILA पर, configurable trigger):

**Pattern Generator output (TX side):**
- `m_axis_tdata[31:0]`
- `m_axis_tvalid`
- `m_axis_tready`
- `m_axis_tlast`

**Aurora link status:**
- `channel_up` — high जब link up हो (सबसे important signal!)
- `lane_up` — per-lane handshake (multi-lane Aurora के लिए; हमारे single-lane के लिए channel_up जैसा)
- `gt_pll_lock` — high जब GT PLL locked हो
- `crc_pass_fail_n` — high जब RX पर CRC OK हो

**Aurora RX output (loopback के बाद):**
- `m_axi_rx_tdata[31:0]`
- `m_axi_rx_tvalid`
- `m_axi_rx_tlast`

**RX FIFO:**
- `axis_str_rxd_tvalid` (Aurora से FIFO में जाने वाला data)
- `interrupt` (GIC को output)

**FIFO control (optional):**
- `prog_full`, `prog_empty` (almost-full/empty flags) — backpressure status बताता है

### 10.5 ILA Setup करना

1. **ILA (Integrated Logic Analyzer)** IP को IP catalog से Block Design में add करो।
2. ILA IP configure करो:
   - **Number of probes:** above list के लिए ~12–15।
   - **Sample data depth:** 1024 या 4096 (more = more BRAM used)।
   - **Probe widths:** signals match करो (जैसे probe0 = tdata के लिए 32 bits, probe1 = tvalid के लिए 1 bit, etc.)।
3. Source signals से ILA के probe inputs तक wires draw करके probes connect करो।
4. ILA का `clk` उस clock पर set करो जिस पर signals चलते हैं (most often `pl_clk0`, पर Aurora-side signals के लिए ये Aurora का `user_clk` होना चाहिए)। आपको TWO ILAs चाहिए हो सकते हैं अगर signals different clocks पर हों।

> **Tip:** Synthesis के दौरान, Vivado कभी-कभी signals को optimize away कर देगा। इसे prevent करने के लिए, उन्हें अपने RTL में `(* mark_debug = "true" *)` से mark करो OR "Set Up Debug" wizard use करो।

### 10.6 Triggering — सही Moment Catch कैसे करें

Trigger के बिना, ILA continuously capture करता है और पुराने samples overwrite कर देता है। आप **interesting event के around** capture करना चाहते हो।

Useful triggers:
- `m_axis_tlast == 1` → हर packet boundary capture करता है
- `channel_up == 0 → 1` (rising edge) → link up आने का moment catch करता है
- एक specific tdata value (जैसे word 1 पर magic number `0xCAFE0001`)
- एक ERROR pulse (जैसे `crc_pass_fail_n == 0`)

### 10.7 ILA कब SKIP कर सकते हो

जब सब काम करने लगे और आप link पर trust करो, ILA mostly FPGA area और BRAM costs करता है। आप कर सकते हो:

- Final bitstream में ILA disable (या IP entirely remove)।
- सिर्फ `channel_up` पर एक minimal ILA रखो occasional diagnostics के लिए।

Development के लिए → ILA रखो। Production के लिए → optional।

### 10.8 ILA बनाम Software Logging

| Method | Pros | Cons |
|--------|------|------|
| ILA | Clock-cycle precision पर actual hardware signals देखो। Software नहीं चाहिए। | Capture के छोटे windows तक limited। |
| C से `xil_printf` | Add करना easy। Software logic track करने के लिए अच्छा। | Raw FPGA wires नहीं देख सकते। Slow (~1 ms per print)। |
| दोनों | FPGA-side timing के लिए ILA; software state के लिए printf। | Best practice। |

> **हमारे PEBB system के लिए:** ILA essential है verify करने के लिए कि FIFO interrupt expected time पर fire हो रहा है, 37 words actually FIFO में पहुँचे हैं, और loopback round-trip intact है। Software printf (`xil_printf` में) confirm करता है कि controller उन्हें correctly interpret कर रहा है।

<a id="part-11"></a>
## Part 11: Buffer/FIFO Handling — Race Conditions और Backpressure

ये वो part है जहाँ production systems usually field में टूटते हैं। ध्यान से पढ़ो।

### 11.1 RX FIFO की पाँच Possible States

किसी भी moment पर RX FIFO इन states में से एक में हो सकता है:

| State | Description | Incoming data का क्या होता है? | Software क्या देखता है |
|-------|-------------|-------------------------------|--------------------|
| Empty | कोई words queued नहीं | New word आता है, stored होता है। | Occupancy=0, no interrupt। |
| Partial | Current packet के कुछ words in हैं, पर TLAST अभी नहीं देखा | New word stored, occupancy increases। | No interrupt yet (TLAST का wait)। |
| One complete packet | TLAST एक बार receive हुआ। CPU अभी drain नहीं किया। | New word stored अगर next packet का start है। | Interrupt asserted। |
| Multiple complete packets | TLAST multiple times receive हुआ। CPU slow है। | New word stored अगर capacity है। | Interrupt asserted रहता है। |
| Full / Overflowing | FIFO full है। Backpressure (`tready=0`) upstream propagate होता है। | New incoming words Aurora पर blocked। | Aurora link drop कर सकता है (अगर push back नहीं कर सकता)। |

### 11.2 Backpressure — AXI Stream Overflow कैसे Prevent करता है

AXI Stream protocol में built-in flow-control mechanism है: **TREADY**।

- अगर RX FIFO full है → ये Aurora को `s_axis_tready` deassert करता है।
- Aurora `tready=0` देखता है → words output करना बंद करता है।
- Aurora के internal buffers कुछ words absorb कर सकते हैं, फिर इसे somehow upstream sender को "stop" बताना है।
- RTDS के साथ real link के लिए, RTDS रुक नहीं सकता (ये hard real-time है)। Aurora के CRC/sequence numbers detect करने देते हैं कि data dropped हुआ, पर recovery user पर है।
- हमारे loopback test के लिए, pattern generator अपने `m_axis_tready` पर `tready=0` देखता है और pauses — perfectly safe।

### 11.3 Backpressure के लिए Train Analogy

- Arrival platform में 5 wagon spots हैं।
- Wagons आते रहते हैं। Spots fill होते जाते हैं।
- Arrival platform staff railway controller को "STOP" flag (`tready=0`) raise करती है।
- आ रही trains previous station पर रुक जाती हैं।
- जब station master finally inspect करके wagons clear करता है, "STOP" flag गिर जाता है, trains resume।
- RTDS के लिए (जो रुक नहीं सकता) — wagons simply ditch में drop हो जाते (link errors)।

### 11.4 अगर Packet Read हो रहा हो जब New Data आए तो?

ये आपका key सवाल है — carefully trace करते हैं।

**Scenario:** Software `axiReceive()` के middle में है, मान लो 20th word के लिए `XLlFifo_RxGetWord()` call कर रहा है। Meanwhile, RTDS next 37-word packet भेजता है।

```
Time → 

t=0    : Packet N fully आता है। Interrupt fires। CPU FifoHandler → axiReceive start करता है।
t=10us : axiReceive packet N का word 5 पढ़ रहा है।
t=125us: Packet N+1 RTDS से आना start करता है। (8 kHz cycle)
         FIFO इन new words को अपनी memory में accept करता है (FIFO multi-packet capable है!)।
t=130us: axiReceive अभी भी packet N का word ~6 पढ़ रहा है। Packet N+1 fully arrived।
         Interrupt asserted रहता है (या re-asserts) packet N+1 के RC के लिए।
t=180us: axiReceive packet N complete करता है। FifoHandler से return करता है।
         FifoHandler loop देखता है `Pending` अभी भी non-zero है — FifoRecvHandler फिर call करता है।
         OR: GIC immediately interrupt फिर fire करता है।
t=181us: axiReceive packet N+1 पर start करता है (अब FIFO में "current" packet)।
```

**ये काम करता है क्योंकि:**

1. Xilinx **AXI4-Stream FIFO IP (PG080)** **multiple complete packets** store करता है। हर packet की length separately preserve होती है — `XLlFifo_iRxGetLen()` सिर्फ **current** packet की length return करता है।
2. Packet N के सारे 37 words drain करने के बाद, `XLlFifo_iRxGetLen()` का next call फिर 37 return करता है — पर packet N+1 के लिए।
3. RC interrupt asserted रहता है जब तक FIFO में at least एक complete packet हो।

### 11.5 Important: Packet Boundaries Preserved हैं

अगर FIFO में multiple packets queued भी हों, वो mix नहीं होते:

```
FIFO contents (multi-packet):

  ┌─[Packet N]──────────────┐ ┌─[Packet N+1]────────────┐ ┌─[Packet N+2]
  │w1 w2 w3 ... w36 w37 [end]│ │w1 w2 w3 ... w36 w37 [end]│ │w1 w2 ...
  └──────────────────────────┘ └──────────────────────────┘ └────────

XLlFifo_iRxGetLen() 37 return करता है (packet N की length)।
XLlFifo_RxGetWord() x 37 → packet N drains।
XLlFifo_iRxGetLen() फिर 37 return करता है (packet N+1 की length)।
XLlFifo_RxGetWord() x 37 → packet N+1 drains।
```

PG080 FIFO में internal logic है track करने के लिए कि हर packet boundary कहाँ है। Software developer के रूप में, आप बस `iRxGetLen()` और `RxGetWord()` call करते रहो जब तक occupancy 0 न हो जाए।

### 11.6 अगर FIFO Fill Up हो जाए Software Drain से पहले?

अगर controller इतना slow है कि RX FIFO multiple packets से fill हो जाए software के catch करने से पहले:

1. New incoming words backpressure start करते हैं (`tready=0`)।
2. Aurora का internal small buffer fills।
3. अगर हम **loopback mode** में हैं, pattern generator का `m_axis_tready` 0 हो जाता है → pause → no data lost (बस delayed)।
4. अगर हम **real RTDS** से connected हैं, RTDS भेजता रहता है। Aurora RTDS को push back नहीं कर सकता → words dropped या CRC errors fire होते हैं।
5. Software ये detect करता है `commGlitchCtr++` (length mismatches) और `Rx_Done` overrun checks (sets `intOverrunFlag`) से।

### 11.7 `intOverrunFlag` Check

`main.c` `FifoHandler` में:
```c
if (Rx_Done) {
    intOverrunFlag = 1;  // previous packet अभी process नहीं हुआ!
}
FifoRecvHandler(InstancePtr);
```

ये एक flag set करता है अगर new RC interrupt पिछले `runControlCycle()` के finish होकर `Rx_Done` clear करने से पहले आ जाए। ये software-side timing-overrun warning है।

### 11.8 Race Conditions जिनकी चिंता करनी है

| Race | क्या गलत हो सकता | Mitigation |
|------|-------------------|------------|
| Software read जब hardware write कर रहा | `iRxGetLen()` mid-packet length return करता है | PG080 hardware atomicity — length register सिर्फ complete packets reflect करता है। Safe। |
| Interrupt fires जब CPU handler में हो | Recursive interrupt | GIC handler चलने तक interrupt mask करता है (priority/preemption disabled) — handler exit पर re-enters। Safe। |
| Multiple CPUs same FIFO read कर रहे | Concurrency hazard | हमारा system सिर्फ CPU0 use करता है। Configuration से safe। |
| Software ACK drain से पहले | Premature interrupt clear | Code handler के start पर RC flag clear करता है (main.c line 432), फिर drains। अगर drain के दौरान new packet आए, RC re-asserts → `while(Pending)` loop में handled। Safe। |

### 11.9 Whole Chain में Backpressure

हमारे loopback test में, chain है:
```
PatternGen ── m_axis ──► TX FIFO ── m_axis ──► Aurora TX ──► loopback ──► Aurora RX ──► RX FIFO ──► CPU
```

Backpressure BACKWARDS propagate होता है:
- अगर RX FIFO full है → ये अपना `s_axis_tready` Aurora RX को deassert करता है।
- Aurora RX का tiny internal buffer fills (at most कुछ words)।
- Aurora link के through आसानी से push back नहीं कर सकता — loopback के लिए, ये Aurora TX (same IP) pause करता है।
- Aurora TX अपना `s_axi_tx_tready` deassert करता है → TX FIFO drain नहीं कर सकता।
- TX FIFO fills, `s_axis_tready` deasserts → pattern generator pauses।

**End result:** loopback में, अगर CPU slow है तो pattern generator naturally pauses। No data lost. Self-throttling।

### 11.10 Whole Chain के लिए Train Analogy

```
[Train Driver]──pauses──► [Loading Platform fills]──pauses──► [Engine TX]──pauses──► 
   ──► [Track]──► [Engine RX]──pauses──► [Arrival Platform fills]──tready=0──► 
   [Station Master overwhelmed]
```

जब station master busy है, "STOP" flag train driver तक पूरा propagate होता है, जो depot पर pauses। जब master wagons clear करता है, "STOP" lifts, traffic resumes।

<a id="part-12"></a>
## Part 12: पूरा Closed-Loop Cycle — Counter → Controller → Switches → वापस

यहाँ हम सब कुछ एक साथ put करते हैं। एक piece of data की पूरे system से journey trace करो।

### 12.1 Cycle (One Iteration)

```
1. Pattern Gen 37 words produce करता है → packet N
2. Packet N → TX FIFO → Aurora TX → loopback → Aurora RX → RX FIFO
3. RX FIFO RC interrupt assert करता है
4. A53 CPU FifoHandler → FifoRecvHandler → axiReceive पर jump करता है
5. axiReceive 37 words पढ़ता है, type/scale conversion apply करता है, store करता है:
     PEBB_Control_U.feedback[0..10]   (currents, voltages)
     P_CMD, Q_CMD, V_CMD               (power commands)
     run, RXI_chil, seq                (control flags)
     etc.
6. runControlCycle call होता है:
     - run==1, ctrlOn==1, ReceiveLength==37 checks
     - PEBB_Control_step() call करता है:
         PEBB_Control_U inputs पढ़ता है
         compute करता है:
           PEBB_Control_Y.switch_1[0..2]    (3-phase duty cycles)
           PEBB_Control_Y.enable
           PEBB_Control_Y.lowside_contact
           ...etc.
     - TxSend() → axiSend() call करता है
7. axiSend 23 words pack करता है, XLlFifo_TxPutWord से TX FIFO में लिखता है
8. axiSend XLlFifo_iTxSetLen(23 * 4 = 92 bytes) call करता है → Aurora TX trigger करता है
9. 23 words travel करते हैं: TX FIFO → Aurora TX → fiber/loopback → ?
10. Software while(1) में return करता है, next interrupt का wait
```

### 12.2 23 TX Words का क्या होता है?

यहाँ loopback tricky हो जाता है।

**Case A — Pure PMA loopback (एक Aurora link):**

23-word TX packet loopback path से जाता है और SAME Aurora के RX पर वापस आता है। RX FIFO एक packet देखता है `ReceiveLength = 23` के साथ, 37 नहीं।

- `axiReceive()` चलता है।
- `if (ReceiveLength != RECEIVE_LENGTH)` → `Warning: receive Length mismatch. Received 23 words. Expected 37.` print होता है।
- `commGlitchCtr` increments। 10 such glitches के बाद → `CHIL_error |= 1`।

**ये failure mode है जिससे आपको aware होना है।** Pure loopback controller loop को उस तरह close नहीं करता जैसे RTDS करता।

**Case B — दो separate Aurora links (Part 8.7 का Option A):**

- Aurora #0 (RX path) pattern gen के 37-word packets receive करता है।
- Aurora #1 (TX path) controller के 23-word output को separate sink (जैसे terminated, या दूसरे ILA) पर भेजता है।
- कोई interference नहीं। Software सिर्फ Aurora #0 से 37-word packets देखता है।

**Case C — RTDS Emulator block (Part 8.7 का Option B):**

एक custom Verilog block:
- Aurora के TX (controller के 23-word output) सुनता है।
- एक simulated power-system state maintain करता है।
- Next 37-word packet generate करता है जिसकी values उस state को reflect करती हैं।
- New 37-word packet को फिर SAME Aurora के TX में push करता है RX के रूप में loop back करने के लिए।

Complex पर RTDS के बिना true closed-loop testing है।

### 12.3 इस Project के लिए Recommended Lab Topology

```
Phase 1 testing के लिए (ये document):

  [Pattern Gen 37w] ──► TX FIFO_a ──┐
                                    │ multiplexer (एक time पर एक source)
  [Software TX 23w] ──► TX FIFO_b ──┘
                                    │
                                    ▼
                              [Aurora TX] ──► PMA loopback ──► [Aurora RX]
                                                                      │
                                                                      ▼
                                          [RX FIFO with packet filter]
                                                                      │
                                                                      ▼
                                                                    [A53]

Filter rule: A53 को सिर्फ 37-word packets forward करो। 23-word packets discard करो
            (या ILA में capture करो verification के लिए)।
```

पर इसके लिए extra logic चाहिए। Lab के लिए सबसे **simplest "good enough" approach**:

```
Phase 1 testing के लिए (simple):

  [Pattern Gen 37w] ──► TX FIFO ──► Aurora TX ──► PMA loopback ──► Aurora RX ──► RX FIFO ──► A53

  [Software TX] ──► (कहीं नहीं; A53 अभी भी axiSend call करता है, पर वो बस /dev/null जाता है)
                    OR: Software TX → second TX FIFO → second Aurora TX → सिर्फ ILA
                    OR: UART से ctrlOn=0 set करके software के axiSend को disable करो
```

`main.c` में आप controller call (और इसलिए axiSend) को disable कर सकते हो `ctrlOn = 0` छोड़कर (UART command से controlled)। फिर `runControlCycle` `PEBB_Control_step` और `TxSend` skip कर देता है। Pattern gen → axiReceive fine काम करता है, no TX traffic link के लिए compete करता है।

### 12.4 Cycle काम कर रहा है Verify करना

एक checklist बनाओ:

| Stage | कैसे verify करें | Expected result |
|-------|--------------|-----------------|
| Pattern gen data produce कर रहा | Pattern gen के `m_axis_*` पर ILA | per packet 37 valid words देखो, word 37 पर TLAST |
| TX FIFO accepting | `m_axis_tready` पर ILA (pattern gen को वापस) | tready high रहता है (कभी block नहीं) |
| Aurora link up | `channel_up` पर ILA | Boot के बाद ~1 second post steady high |
| Loopback round trip | `m_axi_rx_*` पर ILA | Same 37 words वापस आते हैं, TLAST के साथ |
| RX FIFO interrupt | `interrupt` signal probe | हर packet पर pulses |
| A53 interrupt receive | FifoHandler में UART log `xil_printf` | हर interrupt पर print (temporarily add) |
| axiReceive succeeds | UART log में कोई "Length mismatch" warnings नहीं | Quiet log = 37 words received |
| Variables populated | `PEBB_Control_U.feedback[0]` value का UART log | 1500.0 होना चाहिए (= 1.5 * 1000 scale) |
| Controller runs | UART log "controllerStarted = 1" | ~1 second के बाद True |
| TX 23 words | Aurora TX `s_axi_tx_*` पर ILA | हर controller step के बाद TLAST के साथ 23 words |

### 12.5 Variables कहाँ End Up होते हैं — 1.5 kA Tracking

आपने pattern gen word 2 = `0x3FC00000` (= float में 1.5) configure किया।

```
Pattern Gen → 0x3FC00000
    ↓ AXI Stream (TDATA)
TX FIFO → 0x3FC00000
    ↓
Aurora TX (64B/66B serial bits में encodes)
    ↓ loopback
Aurora RX (32 bits में वापस decodes)
    ↓
RX FIFO slot में 0x3FC00000 store करता है
    ↓ interrupt fires
A53 XLlFifo_RxGetWord() से पढ़ता है → RxWord = 0x3FC00000
    ↓
axiReceive: rxData[1].axiType=float, localType=double, scale=1000
RxValue = *(float*)&RxWord = 1.5f
*((double*)rxData[1].ptr) = 1.5f * 1000 = 1500.0
    ↓
PEBB_Control_U.feedback[0] = 1500.0 (अब RAM में double के रूप में)
    ↓
PEBB_Control_step() feedback[0] पढ़ता है
"current 1500A है. नए switch duty cycles compute करो"
    ↓
PEBB_Control_Y.switch_1[0] = 0.75 (कोई computed duty)
    ↓
axiSend: txData[0] switch_1[0] को point करता है, axiType=float, scale=1
float txWord = (float)0.75 * 1.0 = 0.75 = 0x3F400000
XLlFifo_TxPutWord(0x3F400000)
    ↓
TX FIFO → Aurora TX → SFP+ pin से बाहर (या loopback)
```

Full data lifecycle है:
- IEEE 754 bytes → float के रूप में reinterpret → scaled → double के रूप में stored
- Double controller पढ़ता है → computed → float में cast back → bytes के रूप में reinterpret → sent

### 12.6 Closed-Loop Behavior (reference के लिए, इस doc के scope में implement नहीं)

अगर आप RTDS emulator (Option B) build करो, cycle ये हो जाता है:

```
Cycle 0: Emulator i_high = 0 A वाला packet generate करता है
Cycle 1: A53 controller compute करता है: "no current, duty 0.5 तक ramp up करो"
         23 words वापस भेजता है, switch_1[0] = 0.5
Cycle 2: Emulator simulated state update करता है: "0.5 duty से current 0.5 kA हो जाता है"
         new packet i_high = 0.5 kA के साथ भेजता है
Cycle 3: A53 controller: "current 500A, target 1000A, 0.7 तक बढ़ाओ"
         ...और steady state तक
```

यहाँ आप देखेंगे controller actually अपना काम कर रहा है। Smart counter pattern generator के पास ये state-update logic नहीं है — वो हमेशा same values भेजता है। अगर true closed-loop चाहिए, emulator separately build करो।

<a id="part-13"></a>
## Part 13: Step-by-Step Vivado Implementation Guide

ये original 17-step guide है, अब explanations के साथ expanded। Order में follow करो।

### Step 1 — Vivado Project Create करो KR260 Targeting

1. Vivado open करो।
2. **Create Project** → next।
3. Project name (जैसे `kr260_aurora_test`)।
4. Project type: **RTL Project**, "Do not specify sources at this time"।
5. Default Part: **Boards** tab click → search "KR260" → SOM + carrier card select करो।
6. Finish।

**क्यों:** Board (बस chip नहीं) select करने से आपको board files मिलती हैं SFP+, GTH ref clocks, etc. के लिए pre-defined pin maps के साथ।

### Step 2 — Pattern Generator RTL File Create करो

1. Flow Navigator → **PROJECT MANAGER** → **Add Sources** → **Add or Create Design Sources** → next।
2. **Create File** → File type: Verilog, File name: `axis_pattern_generator.v`।
3. Finish। Vivado new file open करता है।
4. [Part 5.4](#part-5) से Verilog code paste करो।
5. Save।

### Step 3 — RTL को Reusable IP के रूप में Package करो

1. Tools → **Create and Package New IP** → Next।
2. **Package your current project** choose करो → Next।
3. IP location set करो (default fine) → Next → Finish।
4. Packager opens। Verify करो:
   - **Compatibility:** check "Zynq UltraScale+" included है।
   - **Customization Parameters:** `PACKET_LENGTH` और `INTER_PKT_GAP` exposed हैं।
   - **Ports and Interfaces:** `m_axis_*` signals को auto-group होना चाहिए एक interface में `m_axis` जो type `xilinx.com:interface:axis_rtl:1.0` का है।
5. **Review and Package** → "Package IP" click करो।
6. Packager close करो।

**क्यों:** Packaging RTL block को IP Integrator (Block Design) में reusable बनाता है — आप इसे किसी भी Xilinx IP की तरह drag कर सकते हो।

### Step 4 — AXI Stream Interface Naming Verify करो

IP packager के "Ports and Interfaces" tab में, आपको देखना चाहिए:
- एक interface जिसका नाम `m_axis` है (individual signals नहीं)।
- इसके अंदर, standard AXIS signals: `TDATA`, `TVALID`, `TREADY`, `TLAST`।

अगर Vivado ने उन्हें auto-group नहीं किया, आपके port names गलत हैं। Step 2 पर वापस जाओ और names exactly match करने के लिए fix करो: `m_axis_tdata`, `m_axis_tvalid`, `m_axis_tready`, `m_axis_tlast`।

### Step 5 — Block Design Create करो

1. Flow Navigator → **IP Integrator** → **Create Block Design**।
2. Name: `system`। OK।
3. Empty BD canvas opens।

### Step 6 — Required IPs Add करो

BD canvas में, "Add IP" ("+" icon) click करो और इनमें से हर एक एक बार में add करो:

| IP Name | Search term | Add करने के बाद Configuration |
|---------|-------------|----------------------------|
| Zynq UltraScale+ MPSoC | "zynq" | Run Block Automation; PL Clock 0 enable करो (100 MHz); PL Reset 0 enable |
| `axis_pattern_generator` | (वो IP जो आपने अभी package किया) | Default config |
| AXI4-Stream Data FIFO | "axis data fifo" | "Independent Clocks" set करो अगर clock domains cross कर रहे हो |
| Aurora 64B/66B | "aurora 64b66b" | Configure: AXI4-Stream user interface, 1 lane, 64B/66B; line rate जो match करे (जैसे start के लिए 5.0 Gbps) |
| AXI4-Stream FIFO (PG080) | "axi-stream fifo" (NOTE: वो जो AXI-Lite control वाला है, simpler axis Data FIFO नहीं) | Default; ensure करो "Use TX Data" checked है अगर TX भी चाहिए |
| ILA | "system ila" | Number of probes ~12, depth 1024 |
| Processor System Reset | "proc sys reset" | Per clock domain एक — 2 add करो |

> **Important:** Xilinx में दो different "FIFO" IPs हैं:
> - **AXI4-Stream Data FIFO** = simple, no AXI-Lite, no interrupt। Inter-block buffering (जैसे PatternGen और Aurora के बीच) के लिए use करो।
> - **AXI4-Stream FIFO (PG080)** = full-featured, AXI-Lite control है, interrupt output है। RX path के लिए use करो जो CPU drains।

### Step 7 — Pattern Generator को TX FIFO से Connect करो

BD canvas में:
1. `axis_pattern_generator/m_axis` port पर click करो → `axis_data_fifo/S_AXIS` तक wire drag करो।

Vivado auto-route कर सकता है। एक clean connection verify करो।

### Step 8 — TX FIFO को Aurora TX से Connect करो

1. `axis_data_fifo/M_AXIS` पर click → `aurora_64b66b/S_AXI_TX` पर drag।

### Step 9 — Aurora Loopback Configure करो (PMA Near-End)

दो तरीके:

**Method A — Hard-coded:** Aurora IP customization में (double-click), "Loopback" parameter ढूँढो और "Near-End PMA Loopback" set करो। Re-customize करो।

**Method B — Pin-driven:** Loopback parameter default पर छोड़ो। फिर:
- IP catalog से एक "Constant" IP add करो। Configure: width=3, value=2 (binary 010 = PMA loopback)।
- Constant.dout → `aurora_64b66b/loopback` connect करो।

> Method B preferred है — बाद में real fiber connect करने पर off करना easy।

### Step 10 — RX FIFO (PG080) Add करो

1. Step 6 से RX FIFO IP BD में पहले से होना चाहिए।
2. `aurora_64b66b/M_AXI_RX` → `axi_fifo_mm_s/AXI_STR_RXD` (PG080 FIFO पर AXI Stream RX input) connect करो।

### Step 11 — RX FIFO को PS से Wire करो Software Access के लिए

PG080 FIFO में THREE interfaces हैं:
- `S_AXI` — AXI-Lite control register interface (CPU `XLlFifo_*` driver से read/write)
- `AXI_STR_RXD` — Aurora से RX data stream
- `interrupt` — GIC को output

Connections:
1. `axi_fifo_mm_s/S_AXI` → PS `M_AXI_HPM0_LPD` (या PS से कोई भी AXI master) से connect करो `axi_smartconnect` block से।
2. `axi_fifo_mm_s/interrupt` → PS `pl_ps_irq0[0]` (या कोई भी free PL→PS interrupt) से connect।
3. `axi_fifo_mm_s/AXI_STR_RXD` → Aurora के M_AXI_RX से (Step 10 में पहले से किया)।

AXI-Lite paths को automatically wire करने के लिए **Connection Automation** run करो।

### Step 12 — Clock Connections (Detailed)

[Part 9](#part-9) refer करो। Connections का summary:

| Source clock | Destination |
|--------------|-------------|
| `Zynq.pl_clk0` | Pattern Generator `aclk` |
| `Zynq.pl_clk0` | TX Data FIFO `s_axis_aclk` |
| Aurora `user_clk_out` | TX Data FIFO `m_axis_aclk` (clock crossing!) |
| Aurora `user_clk_out` | Aurora `s_axi_tx_aclk` (IP की own AXIS TX clock; IP check करो — कभी-कभी same wire है) |
| Aurora `user_clk_out` | RX FIFO PG080 `axis_aclk` (streaming side) |
| `Zynq.pl_clk0` | RX FIFO PG080 `s_axi_aclk` (AXI-Lite control side) |
| `Zynq.pl_clk0` | सारे `proc_sys_reset.slowest_sync_clk` (PS-clock domain instance के लिए) |
| Aurora `user_clk_out` | Second `proc_sys_reset.slowest_sync_clk` (user-clock domain instance के लिए) |
| Board से External 156.25 MHz | Aurora `gt_refclk1_p/n` (board flow automatically connect करता है अगर Aurora को SFP+ board interface से map किया हो) |

### Step 13 — Reset Connections

| `proc_sys_reset` instance | Outputs जाते हैं |
|---------------------------|---------------|
| `rst_pl_clk0` | `axis_pattern_generator/aresetn`, `axis_data_fifo/s_axis_aresetn`, `axi_fifo_mm_s/s_axi_aresetn`, AXI-Lite interconnect |
| `rst_aurora_user` | `axis_data_fifo/m_axis_aresetn`, Aurora का `s_axi_tx_aresetn`, `axi_fifo_mm_s/s_axi_aclk_2_aresetn` (streaming side के लिए जो भी इसे कहा जाता है) |

दोनों `proc_sys_reset` instances `Zynq.pl_resetn0` को `ext_reset_in` के रूप में लेते हैं।

### Step 14 — ILA Probes

ILA probe inputs को wire करो:
- probe0 (32 bits) = `aurora_64b66b/m_axi_rx_tdata`
- probe1 (1 bit) = `aurora_64b66b/m_axi_rx_tvalid`
- probe2 (1 bit) = `aurora_64b66b/m_axi_rx_tlast`
- probe3 (1 bit) = `aurora_64b66b/channel_up`
- probe4 (1 bit) = `axi_fifo_mm_s/interrupt`
- ... ([Part 10.4](#part-10) से ज़्यादा add करो जैसी ज़रूरत)

ILA का `clk` उस clock पर set करो जिस पर probed signals चलते हैं (AXI Stream signals के लिए Aurora `user_clk`)।

### Step 15 — Design Validate करो

1. **Tools → Validate Design** (या F6)।
2. Errors fix करो। Common issues:
   - Floating clock या reset → unconnected pin warning। Connect करो।
   - Address map missing → AXI-Lite slaves के लिए "Assign Address" (toolbar button) run करो।
   - FIFO पर clock domain mismatch → ensure FIFO "Independent Clocks" configure है अगर input/output clocks differ करते हैं।

### Step 16 — HDL Wrapper और Bitstream Generate करो

1. Sources panel में → अपने BD पर right-click → **Create HDL Wrapper** → Let Vivado manage → OK।
2. Wrapper को top set करो (right-click → "Set as Top")।
3. **Synthesis → Implementation → Generate Bitstream** run करो।
4. Wait करो (PC पर 5–30 minutes)।
5. Bitstream succeed होने के बाद, **File → Export → Export Hardware** → include bitstream → XSA save।

### Step 17 — Hardware Debug

1. KR260 को USB-UART से और JTAG से connect करो (या KR260 पर single-cable JTAG-over-USB use)।
2. Vivado **Hardware Manager** open करो (Flow Navigator)।
3. Device से auto-connect करो।
4. FPGA target पर right-click → Program Device → अपनी `.bit` file choose करो।
5. ILA core auto-detect होना चाहिए। Trigger set करो (जैसे `m_axi_rx_tlast == 1`)।
6. Capture के लिए **Run Trigger Immediate**।
7. Verify:
   - `channel_up = 1`
   - `m_axi_rx_tvalid` toggles
   - `m_axi_rx_tdata` आपका expected pattern show करता है (1.5 = 0x3FC00000, etc.)
   - `m_axi_rx_tlast = 1` active stream में हर 37 cycles में एक बार

<a id="part-14"></a>
## Part 14: KR260 पर Bring-Up और Testing (शुरू से)

### 14.1 Prerequisites Checklist

Board on करने से पहले:

- [ ] KR260 board एक known-good power supply के साथ
- [ ] USB-UART cable connected (`xil_printf` console के लिए)
- [ ] JTAG cable (या carrier card से USB-JTAG)
- [ ] Vivado 2022.2 या later installed (BSP version से matching जिससे आपका software build हुआ)
- [ ] Vitis IDE 2022.2 installed
- [ ] (Sirf external loopback के लिए) एक SFP+ optical transceiver और fiber jumper

### 14.2 Phase A — FPGA Bitstream Bring-Up

**Goal:** Verify करना FPGA design load होता है और Aurora link up आता है।

1. KR260 power on करो।
2. Vivado Hardware Manager open करो → connect।
3. आपने generate किया bitstream program करो।
4. ILA open करो, `channel_up` signal देखो:
   - Expected: programming के बाद ~1 second में `1` तक rises।
   - अगर 0 पर stuck → Aurora reference clock, loopback configuration, reset sequence check करो।

### 14.3 Phase B — Pattern Generator Output Verify करो

**Goal:** Confirm करना pattern generator 37-word packets produce करता है।

1. ILA अभी भी running के साथ, trigger set करो: pattern generator output पर `m_axis_tlast == 1`।
2. Trigger run।
3. Waveform inspect:
   - `m_axis_tvalid` 37-word sequence के दौरान high होना चाहिए।
   - `m_axis_tdata` per word expected hex values show करना चाहिए।
   - `m_axis_tlast` सिर्फ word 37 पर high होना चाहिए।

### 14.4 Phase C — Loopback Round Trip Verify करो

**Goal:** Confirm करना TX पर sent data RX पर correctly वापस आता है।

1. ILA trigger set: Aurora RX output पर `m_axi_rx_tlast == 1`।
2. Trigger run।
3. Inspect:
   - `m_axi_rx_tdata` पर 37 words appear।
   - Values match करते हैं जो आपने `m_axis_tdata` पर sent।
   - `m_axi_rx_tlast` word 37 पर high।

अगर values corrupted → loopback या Aurora encoding broken। अगर values right → physical link काम करता है।

### 14.5 Phase D — Software Bring-Up

**Goal:** A53 interrupt receive करे और packet process करे।

1. Vitis में, XSA से application project create करो।
2. Existing `main.c`, `Axi_IO.c`, और PEBB CHIL source tree का बाकी use करो।
3. Build → `.elf` produce करता है।
4. Vitis debugger open करो, इनमें breakpoints set करो:
   - `FifoHandler` (main.c line 428)
   - `axiReceive` (Axi_IO.c line 604)
5. Hardware पर run। Observe:
   - `FifoHandler` में breakpoint FPGA programming के ~10 ms बाद hit होता है।
   - `axiReceive` में, 37-word loop में step करो। Check `RxWord` आपके test pattern से match करता है।
6. `axiReceive` return के बाद `PEBB_Control_U.feedback[0]` print करो। 1500.0 (= 1.5 * 1000) equal होना चाहिए।

### 14.6 Phase E — Controller Run देखो

**Goal:** PEBB controller outputs compute करता है।

1. `runControlCycle()` में `PEBB_Control_step()` के बाद `xil_printf` add करो:
```c
xil_printf("switch_1[0] = %f\r\n", PEBB_Control_Y.switch_1[0]);
```
2. Rebuild, rerun।
3. UART कुछ cycles के बाद non-zero duty cycles show करना चाहिए।

### 14.7 Phase F — TX देखो

**Goal:** Verify करना A53 23 words भेजता है।

1. ILA trigger `s_axi_tx_tlast == 1` (Aurora TX side) पर।
2. Run।
3. Verify 23 words बाहर जाते हैं। उनके content controller output reflect करते हैं।

### 14.8 Phase G — Multi-Packet Stress Test

**Goal:** Ensure system back-to-back packets full rate पर handle करता है।

1. `INTER_PKT_GAP` parameter low set करो (जैसे 10000 = 100 MHz पर ~100 µs, RTDS से थोड़ा slow)।
2. 60 seconds run।
3. UART check करो:
   - "Length mismatch" warnings → कोई हों तो link data drop कर रहा है।
   - `commGlitchCtr` value debugger से → 0 पर रहे या बहुत slowly increment।
   - `intOverrunFlag` → 0 होना चाहिए।
4. अगर कोई warnings नहीं → INTER_PKT_GAP और down करो margin test करने।

### 14.9 Common Failure Modes और Fixes

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `channel_up` 0 रहता है | Reference clock missing, या no loopback enabled | ILA से ref clock probe करो; verify `loopback=3'b010` |
| RX पर data सब zeros | Aurora कुछ receive नहीं कर रहा | Loopback wiring check; check pattern gen data produce कर रहा है (`m_axis_*` probe) |
| Data corrupted (random values) | Clock domain crossing handle नहीं | TX FIFO "Independent Clocks" configure verify; हर block correct `aclk` use करता verify |
| FIFO interrupt कभी fire नहीं | Interrupt PS से connected नहीं, या software में `XLlFifo_IntEnable` call नहीं | `interrupt` का BD wiring `pl_ps_irq*` से check; verify main.c line 281 reach होती है |
| "Length mismatch" warnings constantly | Pattern gen गलत number of words भेज रहा | Verilog recheck; ensure `tlast` सिर्फ word 37 पर |
| `CHIL_error` immediately set | Likely float words में NaN | कोई 0xFFFF... या unusual hex को valid IEEE 754 floats से replace |
| Controller नहीं चलता | `run` flag 0 है | Pattern gen में word 35 `0x00000001` verify; `ctrlOn=1` verify (UART से set) |
| UART silent | UART initialized नहीं, या गलत baud rate | Check setupUART() पहले चलता है; default baud 115200 |

<a id="part-15"></a>
## Part 15: Q&A — हर सवाल जो आएगा

### Q1: Counter generator banana hai — kya complex hai?

**जवाब:** Complex नहीं। ~80 lines of Verilog है। [Part 5.4](#part-5) में template use करो। तीन चीज़ें याद रखो:
1. Per packet 37 words (parameter PACKET_LENGTH)।
2. सिर्फ 37th word पर TLAST।
3. Packets के बीच spacing के लिए INTER_PKT_GAP।

### Q2: `m_axis` kya matlab hai exactly?

**जवाब:**
- `m_axis` = **M**aster **AXI S**tream — आपका block data PRODUCE (outputs) करता है।
- इस group के अंदर signals: `tdata`, `tvalid`, `tready`, `tlast`।
- Master output → next block पर Slave input (`s_axis_*`) से connects।
- Naming exactly `m_axis_tdata`, etc. होनी चाहिए — Vivado AXI Stream interface auto-detect करने के लिए इन names use करता है।
- [Part 4](#part-4) देखो।

### Q3: Debug zaroori hai ya skip kar sakte hain?

**जवाब:** Bring-up के दौरान ILA **strongly recommended** है — इसी से आप SEE करते हो FPGA wires पर क्या हो रहा है। सब काम करने के बाद, आप final bitstream से ILA हटा सकते हो FPGA area बचाने के लिए। [Part 10](#part-10) देखो।

First-time bring-up के लिए: ILA include करो। channel_up, AXI Stream signals, या interrupt firing verify करने का कोई दूसरा तरीका नहीं है (blind UART logging के अलावा जो बहुत slower है)।

### Q4: Transmitter to Receiver data kaise pass karte hain?

**जवाब:** AXI Stream के through। Specifically:

1. Pattern Gen का `m_axis` (master output) TX FIFO के `s_axis` (slave input) से connect।
2. TX FIFO का `m_axis` Aurora के `s_axi_tx` से connect।
3. Aurora encode करता है और GTH transceiver (TX serial pins) पर भेजता है।
4. (Loopback या fiber) — physical layer।
5. Aurora का `m_axi_rx` (decoding के बाद) RX FIFO के `s_axis_rxd` से connect।
6. RX FIFO buffers, PS को interrupt fires।
7. CPU memory-mapped registers से `XLlFifo_*` driver functions use करके पढ़ता है।

ये AXI Stream connections + Aurora encoding/decoding + एक memory-mapped final stage की chain है।

### Q5: TX kaunse pin pe connect hota hai?

**जवाब:** TX **GTH transceiver TX pins** में से एक पर जाता है, जो KR260 पर physically SFP+ cage connector पर routed हैं। Specifically:
- SFP+ cage 0 `MGTH_TX0_P/N` use करता है (GTH Quad के 4 channels में से एक)।
- SFP+ cage 1 different channel use करता है।

आप manually pin pick नहीं करते — जब Vivado में KR260 board target choose करते हो, board file Aurora को correct SFP+ connector पर map करती है। [Part 7](#part-7) देखो।

### Q6: Loopback ki tarah hota hai ya koi aur way?

**जवाब:** तीन options, जो available है उसके आधार पर pick करो:

1. **Internal PMA loopback** — GTH transceiver के अंदर wires, no external hardware। First bring-up के लिए best। `loopback=3'b010` set करो।
2. **External fiber loopback** — same board के SFP+0 TX से SFP+0 RX (या SFP+1) पर short fiber cable। Physical layer test करता है। `loopback=3'b000` set करो।
3. **Real link** — RTDS या किसी और endpoint को fiber।

[Part 8](#part-8) देखो।

### Q7: Step 9 (clock and reset) ka detailed explanation?

**जवाब:** [Part 9](#part-9) देखो। Summary:
- Multiple clock domains exist: PS clock (100 MHz), Aurora user clock (~161 MHz), GT reference clock (156.25 MHz)।
- हर block अपने signals के लिए right clock use करना चाहिए।
- TWO `proc_sys_reset` instances use करो — per clock domain एक।
- PatternGen और Aurora के बीच TX FIFO "Independent Clocks" configure होना चाहिए क्योंकि ये domains cross करता है।

### Q8: ILA kyu chahiye?

**जवाब:** ILA के बिना, आपको FPGA wires पर क्या हो रहा है इसकी कोई visibility नहीं। आप बता नहीं सकते Aurora up है, data flow कर रहा है, या interrupts fire हो रहे हैं। ILA = chip के अंदर oscilloscope। [Part 10](#part-10) देखो।

### Q9: Counter ka output 37 words ka packet hota hai — kya yeh AuroraLink format mein hai?

**जवाब:** नहीं, आपका Verilog सिर्फ **AXI Stream** (TDATA + TVALID + TLAST) produce करता है। Aurora IP उसे wire पर AuroraLink/64B-66B format में wrap करता है। तो:
- आप AXI Stream लिखते हो → Aurora इसे AuroraLink बनाता है → wire के पार → Aurora unwrap करता है → other end पर AXI Stream बाहर आता है।

आपके counter को बस clean 37-word AXI Stream packets produce करने हैं। Aurora बाकी handle करता है। [Part 6](#part-6) देखो।

### Q10: Counter generator should generate data verifiable by Axi_IO.c — how?

**जवाब:** [Part 5.2](#part-5) में table refer करो जो हर word का expected type list करता है। फिर [Part 5.4](#part-5) में Verilog template use करो जिसमें सारे IEEE 754 hex codes pre-computed हैं।

Key requirements:
- Word 35 = 1 (run flag)।
- Floats valid IEEE 754 (NaN नहीं) होने चाहिए।
- Word 37 = sequence number 1..1000।
- Sequence number per packet increments।

Software का `axiReceive()` फिर:
- 37 words parse करेगा।
- Scale factors apply करेगा (kA→A के लिए 1000, MW→W के लिए 1e6)।
- `PEBB_Control_U` और दूसरे globals में store।
- `runControlCycle()` outputs compute करेगा और `axiSend()` call करेगा।

### Q11: Buffer/FIFO — agar data already hai aur next set aata hai aur previous abhi tak read nahi hua, kya hoga?

**जवाब:** PG080 RX FIFO **multiple complete packets** hold करता है। जब new data आता है:
- अगर FIFO में space है → new words existing के बाद store होते हैं। हर packet की length separately track होती है।
- Software `XLlFifo_iRxGetLen()` पढ़ता है → सिर्फ CURRENT (oldest) packet की length return करता है।
- Software N calls `XLlFifo_RxGetWord()` से drain करता है।
- फिर `iRxGetLen()` next packet की length return करता है, और इसी तरह।
- अगर FIFO FULL → backpressure `tready` deassert करता है, जो Aurora से source तक propagate होता है। Loopback में, pattern gen safely pauses। Real RTDS के साथ, data drop हो सकता है (link error)।

Full backpressure analysis के लिए [Part 11](#part-11) देखो।

### Q12: Closed-loop cycle kaise complete kare — counter → controller → switches → wapas?

**जवाब:** Pure PMA loopback controller loop को CLOSE नहीं करता। Controller का 23-word TX 23-word RX के रूप में loop back होगा, जो length mismatch error (37 expected, 23 received) cause करता है।

तीन workarounds:
1. **Decoupled testing (recommended):** `ctrlOn=0` set करो ताकि controller TX न करे। Pattern gen → RX → axiReceive reception verify करता है। फिर controller enable करो और सिर्फ ILA से TX observe करो।
2. **दो Aurora links:** Aurora 0 पर pattern gen, Aurora 1 पर controller TX। कोई interference नहीं।
3. **RTDS Emulator block:** Custom Verilog जो controller TX सुनता है, state simulate करता है, next 37-word packet generate करता है। True closed-loop पर complex।

[Part 8.6](#part-8) और [Part 12](#part-12) देखो।

### Q13: Kria board lend hua. Project from scratch start kaise karu?

**जवाब:**
1. Vivado + Vitis install करो (matching version, जैसे 2022.2)।
2. Kria KR260 board files download करो (Vivado के साथ auto-installs, या Xilinx board store से)।
3. New Vivado project create करो, KR260 board target करो।
4. FPGA design build करने के लिए [Part 13](#part-13) Steps 1–17 follow करो।
5. Bitstream generate, XSA export।
6. Vitis open करो, XSA से application project create। PEBB CHIL firmware का existing `src/` directory add करो।
7. Build, debug।
8. [Part 14](#part-14) bring-up phases A through G follow करो।

### Q14: SFP+ optical module zaroori hai?

**जवाब:** सिर्फ external fiber loopback या real RTDS link के लिए। Internal PMA loopback testing (recommended first phase) के लिए, कोई SFP+ module ज़रूरी नहीं। TX/RX FPGA chip कभी नहीं छोड़ता।

### Q15: Aurora line rate kya set karu?

**जवाब:** जो भी other end use करता है। Common Aurora rates: 5 Gbps, 10 Gbps। Lab testing के लिए internal loopback के साथ, कोई भी rate काम करता है (जब तक GT reference clock match करे)। RTDS-side documentation production rate बताएगी। Safety के लिए 5 Gbps से start; बाद में bump up करो।

### Q16: Pattern generator `INTER_PKT_GAP` kya rakhu shuruaat mein?

**जवाब:** बहुत slow से start करो (1 packet per second = `INTER_PKT_GAP = 100_000_000`)। ये आपको UART output पढ़ने, debugger में single-step करने, ILA waveforms observe करने का time देता है overwhelmed हुए बिना। सब काम होने के बाद, RTDS-realistic 125 µs (`INTER_PKT_GAP = 12_500`) तक ramp down करो।

### Q17: `xil_printf` doesn't work — what's wrong?

**जवाब:** Common causes:
- कोई `xil_printf` से पहले `setupUART()` call नहीं हुआ। ये `main.c` के line 178 पर call होता है — make sure वो path चलता है।
- BD में UART pins expose नहीं। Verify Zynq IP में UART0/UART1 enabled और routed है।
- Host PC terminal पर गलत baud rate। Default = 115200 8N1।
- गलत COM port। USB-UART COM port identify करने के लिए Device Manager (Windows) use करो।

### Q18: Kya hum software update kiye bina counter ka pattern change kar sakte?

**जवाब:** हाँ। Counter के payload values Verilog ROM (`payload[]`) में store हैं। Values change करने के लिए:
1. `axis_pattern_generator.v` में `initial begin` block edit करो।
2. Re-run synthesis + implementation + bitstream।
3. FPGA reprogram (software rebuild नहीं चाहिए)।

Runtime-changeable patterns के लिए, आपको generator IP में AXI-Lite slave register add करना होगा और software से लिखना होगा। ये future enhancement है।

### Q19: Multi-packet test — kaise verify karu controller correctly chal raha?

**जवाब:** `runControlCycle()` में `PEBB_Control_step()` के बाद `xil_printf` add करो:
```c
xil_printf("seq=%d, run=%d, switch_1[0]=%f, feedback[0]=%f\r\n",
           seq, run, PEBB_Control_Y.switch_1[0], PEBB_Control_U.feedback[0]);
```
ये per cycle एक बार print करता है। Pattern gen fixed values भेज रहा है और `seq` incrementing, आप देखेंगे:
- `seq` 1, 2, 3, ... incrementing।
- `feedback[0]` हमेशा = 1500.0 (वो value जो हमने भेजी)।
- `switch_1[0]` controller के `enabled` होने पर non-zero duty cycle होना चाहिए।

### Q20: Verilog mein floating-point math zaroori hai?

**जवाब:** नहीं — और आपको AVOID करना चाहिए। अपने सारे float values Python में pre-compute करो (या hex calculator), उन्हें Verilog ROM में `32'h....` constants के रूप में put करो। FPGA fabric में real-time float math करने के लिए DSP/FP IP cores चाहिए, जो static test pattern के लिए overkill है। [Part 5.3](#part-5) देखो।

### Q21: KR260 carrier card different SOMs ko support karta hai — kya difference padta hai?

**जवाब:** KR260 specifically K26 SOM के लिए designed है। Exact UltraScale+ part `XCK26-SFVC784-2LV-C` है। Vivado की KR260 board file इस part को target करती है। Board file rebuild किए बिना K24 या अन्य SOMs substitute मत करो।

### Q22: Reset signal high or low — kaunsa active?

**जवाब:**
- ज़्यादातर AXI signals **active-LOW** reset use करते हैं, trailing `n` से denoted: `aresetn`। (1 = normal operation, 0 = reset)
- कुछ IPs (older Aurora flavors, कुछ debug IPs) **active-HIGH** reset use करते हैं।
- हमेशा IP documentation check करो। `proc_sys_reset` IP दोनों polarities output करता है (`peripheral_aresetn` active-low है, `peripheral_reset` active-high है)।

### Q23: Power consumption / thermal concerns on KR260?

**जवाब:** GTH transceivers noticeable power consume करते हैं (~0.5 W per active channel)। Loopback testing के लिए ये concern नहीं। Multi-Aurora-link production setups के लिए, thermal management consider करो — KR260 में एक fan है जो Zynq config में enable होना चाहिए।

### Q24: Vitis se build error: "undefined reference to XLlFifo_*"

**जवाब:** अपनी BSP में `axi4-stream-fifo` (या `xllfifo` library — exact name varies) add करो। Vitis में: BSP settings open → Libraries → `xllfifo` enable करो। Re-build BSP, फिर re-build app।

### Q25: Aurora "channel_up = 0" forever — kya troubleshoot karu?

**जवाब:** Step-by-step:
1. ILA से `gt_pll_lock` probe। 1 होना चाहिए। अगर 0 → reference clock present नहीं या गलत frequency। Check करो `gt_refclk1_p/n` actually board पर 156.25 MHz source से wired है।
2. `pma_init`, `reset_pb` probe। Init के बाद दोनों 0 (deasserted) होने चाहिए। अगर high stuck → reset connection broken।
3. `loopback = 3'b010` (PMA loopback) set करो। अगर `channel_up` rises → physical fiber/SFP problem है। अगर अभी भी 0 → IP configuration issue।
4. Aurora IP customization open, verify line rate, lane count, reference clock frequency आपके design से match करते हैं।
5. Make sure `init_clk` connected और ticking है।

### Q26: Software stuck in `while(1)` — interrupt never fires. Kya gadbad?

**जवाब:** Step-by-step:
1. ILA से RX FIFO का `interrupt` output probe। क्या ये pulse करता है?
   - हाँ: hardware interrupt fire कर रहा है, पर software receive नहीं कर रहा। PS को IRQ wiring check, `XScuGic_Connect()` succeeded check, `Xil_ExceptionEnable()` called check।
   - नहीं: interrupt generate नहीं हो रहा। Check करो TLAST=1 वाला data actually RX FIFO तक पहुँचता है। Verify `XLlFifo_IntEnable(InstancePtr, XLLF_INT_ALL_MASK)` call हुआ है (main.c line 281)।
2. Verify GIC interrupt ID BD से exported वाले से match करता है। Vitis में, `xparameters.h` में `XPAR_*_INTR` macro check करो और ensure `main.c` में `FIFO_INTR_ID` match करता है।

### Q27: Sequence number wrap (1000 → 1) edge case

**जवाब:** जब `seq` 1000 से 1 तक wraps, software का `SEQ_INCREMENT` check (अगर enabled) wraparound `% SEQ_MAX` से handle करता है। हमारी default configuration में, `SEQ_INCREMENT` commented out है (Axi_IO.h line 30), तो कोई check perform नहीं होता। बस counter properly wrap करता रखो।

### Q28: Mujhe Verilog testbench likhna hai pattern generator ke liye?

**जवाब:** Strongly recommended। ऐसा एक छोटा testbench आपको hardware जाने से पहले simulate करने देता है:

```verilog
module tb_pattern_gen;
    reg aclk = 0;
    reg aresetn = 0;
    wire [31:0] tdata;
    wire        tvalid, tlast;
    reg         tready = 1;

    axis_pattern_generator #(.INTER_PKT_GAP(100)) u (
        .aclk(aclk), .aresetn(aresetn),
        .m_axis_tdata(tdata),
        .m_axis_tvalid(tvalid),
        .m_axis_tready(tready),
        .m_axis_tlast(tlast)
    );

    always #5 aclk = ~aclk;  // 100 MHz

    initial begin
        #100 aresetn = 1;
        #100000 $finish;
    end

    always @(posedge aclk) begin
        if (tvalid && tready)
            $display("t=%t  word=%h  last=%b", $time, tdata, tlast);
    end
endmodule
```

Vivado simulator में run → check printed words आपके expected pattern से match करते हैं। Hardware-only debugging से घंटों बचाता है।

### Q29: Buffer size — kitni rakhu RX FIFO ki?

**जवाब:** Default PG080 size काफी है। Typically 512 से 4096 words। हमारा 1 packet 148 bytes (37 words × 4) है — किसी भी reasonable depth से बहुत कम। अगर software lag expect करते हो (जैसे long debug breakpoints), drop किए बिना ज़्यादा packets buffer करने के लिए 4096 तक increase करो।

### Q30: "System works" declare करने से पहले Final checklist:

- [ ] `channel_up = 1` consistently
- [ ] ILA per RX packet 37 words received, word 37 पर TLAST दिखाता है
- [ ] Full rate पर कम से कम 60 seconds के लिए `commGlitchCtr` 0 पर रहता है
- [ ] `intOverrunFlag` कभी set नहीं
- [ ] `feedback[0]` expected scaled value equal (1.5 kA input के लिए 1500.0)
- [ ] `controllerStarted = 1` और `PEBB_Control_step` हर cycle चलता है
- [ ] TX FIFO per controller cycle 23 words exiting देखता है
- [ ] `CHIL_error == 0` (no errors flagged)
- [ ] UART log में कोई NaN warnings नहीं
- [ ] Sequence number `seq` correctly increments

अगर सब check, link + software + controller end-to-end काम कर रहे हैं। Real RTDS connection के लिए ready।

---

## Glossary

| Term | मतलब |
|------|---------|
| AXI Stream | Internal-FPGA simple data streaming protocol TDATA, TVALID, TREADY, TLAST के साथ |
| Aurora | Xilinx serial-link IP जो 64B/66B (या 8B/10B) line coding use करता है |
| BD | Block Design (Vivado IP Integrator) |
| BSP | Board Support Package (Vitis app में use होने वाले drivers + headers) |
| CHIL | Controller Hardware-In-the-Loop |
| FIFO | First-In-First-Out buffer |
| GIC | General Interrupt Controller (PS में) |
| GTH | Zynq UltraScale+ में Multi-gigabit serial transceiver |
| ILA | Integrated Logic Analyzer (Xilinx debug IP) |
| IP | Intellectual Property block (एक reusable hardware module) |
| KR260 | Kria Robotics 260 board (K26 SOM + carrier) |
| Loopback | TX output को RX input पर route करना (internal या external) |
| PG080 | Xilinx Product Guide 080 — AXI4-Stream FIFO |
| PL | Programmable Logic (FPGA fabric) |
| PMA | Physical Medium Attachment (lower-level analog/serializer block) |
| PS | Processing System (Zynq पर A53 + cortex-R5 + peripherals) |
| RC | Receive Complete (PG080 FIFO में interrupt flag) |
| RTDS | Real-Time Digital Simulator |
| SFP+ | Small Form-factor Pluggable Plus — fiber optic connector standard |
| SOM | System-on-Module |
| XSA | Xilinx Shell Archive (Vitis के लिए Vivado से hardware export) |

---

*Guide समाप्त। Software-side / firmware-side details (`main.c`, `Axi_IO.c` flow) के लिए, `detailed_explaination.md` देखो (या इसका Hindi version `detailed_explaination_hindi.md`)।*
