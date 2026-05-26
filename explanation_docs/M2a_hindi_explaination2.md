# 📚 M2-A: AuroraLink Protocol — पूरी समझ (Hindi में)
### Abhishek ke liye — Step by Step, Story ke Saath

---

## 🎭 पहले एक कहानी — "राजू का Power Plant"

> **कल्पना करो:** एक बड़ा Power Plant है जिसमें दो कमरे हैं।
>
> - **कमरा 1 — RTDS Simulator (बड़ा Computer):** यहाँ एक scientist बैठा है जो बिजली के circuits को simulate करता है। वो हर millisecond में नए readings भेजता है — जैसे voltage, current, commands।
>
> - **कमरा 2 — Kria Board (हमारा FPGA+Processor):** यहाँ एक smart controller है जो उन readings को लेकर real decisions लेता है — जैसे switches on/off करना।
>
> **Problem:** ये दोनों कमरे अलग-अलग हैं। इनके बीच data कैसे जाएगा?
> **Solution:** एक special fiber optic cable — जिसका नाम है **AuroraLink** 🌟

---

## 📦 Part 1: AuroraLink क्या है? (End-to-End समझो)

### 🔑 Key Concept: तीन "खिलाड़ी" हैं

```
RTDS Simulator          Kria Board की FPGA (PL)        Kria Board का Processor (PS)
[Big Computer]    ──►   [Aurora IP + AXI FIFO]    ──►   [A53 CPU — हमारा Software]
  "Data भेजो"           "Data पकड़ो, Store करो"          "Data पढ़ो, Use करो"
```

### 🏠 Story से जोड़ो:
- **RTDS** = दूसरे शहर में बैठा **दोस्त** जो WhatsApp पर message भेजता है
- **Aurora IP (PL/FPGA)** = तुम्हारा **router/modem** — internet पकड़ता है, घर में WiFi देता है
- **A53 CPU (PS)** = तुम्हारा **phone** — जो actually message पढ़ता है

**Aurora का काम:** Fiber optic से आने वाले raw bits को पकड़ो, उन्हें proper AXI data में convert करो, और FIFO (queue) में रख दो।

---

## 📦 Part 2: AXI-FIFO क्या है? (FIFO को समझो)

### 🔑 FIFO = "First In, First Out" = Line में लगो

**Story:**  
> Railway Station की ticket window पर लोग line में लगते हैं। जो पहले आया, उसकी ticket पहले बनती है। Aurora data भी ऐसे ही FIFO queue में जाता है।

```
Aurora से आया Data
        │
        ▼
┌──────────────────┐
│   AXI-FIFO       │  ← यह PL (FPGA) में है
│  [word1]         │
│  [word2]         │  ← 37 words आते हैं (RECEIVE_LENGTH = 37)
│  [word3]         │
│  [...]           │
│  [word37]        │
└────────┬─────────┘
         │
         ▼
    A53 CPU (PS) पढ़ता है
```

**Code में देखो** → `Axi_IO.h` line 23:
```c
#define RECEIVE_LENGTH 37  // RTDS से 37 words आते हैं
```

---

## 📦 Part 3: Push vs Pull vs Interrupt — सबसे Important सवाल! 🎯

यही M2-A का **सबसे बड़ा सवाल** है। चलो तीनों को story से समझते हैं।

---

### 🔴 Option A: PULL (Software polling) — "खुद जा के चेक करो"

**Story:**  
> तुम हर 5 मिनट में घर के बाहर जाकर देखते हो — "क्या डाकिया letter लेकर आया?"  
> अगर आया तो लेते हो, नहीं आया तो वापस आ जाते हो।  
> **बेकार तरीका!** CPU का time waste होता है।

```c
// POLLING का example (हमारा code यह नहीं करता)
while(1) {
    if (data_available()) {
        read_data();  // अगर data है तो पढ़ो
    }
    // वरना फिर loop करो — CPU waste होती है
}
```

---

### 🟡 Option B: PUSH (Hardware DMA) — "Hardware खुद memory में डाल दे"

**Story:**  
> डाकिया सीधे तुम्हारी almari में letter रख देता है।  
> तुम्हें कुछ नहीं करना। Letter magically appear हो जाता है।

यह DMA (Direct Memory Access) जैसा होता है — hardware खुद RAM में data डाल देती है।

---

### 🟢 Option C: INTERRUPT — "घंटी बजाओ, फिर मैं आता हूँ" ✅ यही हमारा System है!

**Story:**  
> डाकिया door bell बजाता है।  
> तुम जो भी काम कर रहे थे वो रोको, दरवाज़ा खोलो, letter लो, वापस काम करो।  
> **Best तरीका!** CPU तब तक free रहती है जब तक data नहीं आता।

---

### 🎯 हमारा System क्या करता है?

**Answer (एक sentence में):**  
> **PL (Aurora IP) hardware की तरफ से stream push होती है FIFO में, और जब एक complete packet FIFO में आ जाता है, तो hardware एक interrupt signal देता है A53 CPU को — CPU तब `FifoRecvHandler` call करता है जो `axiReceive` से FIFO को pull करके पढ़ता है।**

यानी: **Stream Push (PL) + Interrupt Notification + CPU Read/Pull (PS)** — तीनों मिलकर काम करते हैं!

---

## 📦 Part 4: Code Walk-through — Real Code में देखो 🔍

### Step 1: System शुरू होना — `main.c`

```c
// main.c line 281
XLlFifo_IntEnable(InstancePtr, XLLF_INT_ALL_MASK);
//   ↑ FIFO को बताया: "जब भी data आए, interrupt दो"
```

**Story:** यह वैसे है जैसे तुमने घर की doorbell ON की। अब कोई भी आएगा तो bell बजेगी।

---

### Step 2: RTDS से Data आता है → Interrupt Fire होती है → `FifoHandler` call होता है

```c
// main.c line 428-456
static void FifoHandler(XLlFifo *InstancePtr)
{
    u32 Pending;
    Pending = XLlFifo_IntPending(InstancePtr);
    //  ↑ "कौन सी घंटी बजी?" — RX? TX? Error?
    
    while (Pending) {
        if (Pending & XLLF_INT_RC_MASK) {  // RC = Receive Complete
            XLlFifo_IntClear(InstancePtr, XLLF_INT_RC_MASK);
            FifoRecvHandler(InstancePtr);  // ← Data receive करो!
        }
        // ...
    }
}
```

**Story:** Security guard (FifoHandler) देखता है — "कौन आया? Delivery wala (RX)? Ya courier pick-up (TX)? Ya कोई error?"

---

### Step 3: `FifoRecvHandler` — असली काम शुरू

```c
// main.c line 470-480
static void FifoRecvHandler(XLlFifo *InstancePtr)
{
    axiReceive(InstancePtr);  // ← FIFO से data पढ़ो
    Rx_Done = 1;              // ← Flag set करो "data मिल गया"
    setPEBBLRU();             // ← LRU count update करो
    
    if (runControlCycle(InstancePtr) == XST_FAILURE) {
        CHIL_error |= 1;
    }
}
```

**Story:** Security guard ने delivery आने का signal दिया। अब receiver (FifoRecvHandler) आता है, package लेता है (`axiReceive`), और controller को चलाता है।

---

### Step 4: `axiReceive` — असली FIFO reading

यह `Axi_IO.c` का सबसे important function है:

```c
// Axi_IO.c line 604-684
void axiReceive(XLlFifo *InstancePtr)
{
    // Step A: क्या FIFO में कुछ है?
    if (XLlFifo_iRxOccupancy(InstancePtr)) {
        
        // Step B: कितने words हैं?
        ReceiveLength = (XLlFifo_iRxGetLen(InstancePtr)) / WORD_SIZE;
        
        // Step C: क्या size सही है? (37 words होने चाहिए)
        if (ReceiveLength != RECEIVE_LENGTH) {
            // गलत packet — खाली करो, error flag लगाओ
            commGlitchCtr++;
        } else {
            // Step D: एक-एक word पढ़ो और सही जगह store करो
            for (i = 0; i < RECEIVE_LENGTH; i++) {
                RxWord = XLlFifo_RxGetWord(InstancePtr);
                // Type check → Scale convert → Store in rxData[i]->ptr
            }
        }
    }
}
```

**Story (detailed):**
> Postman (FIFO) के paas bag है।  
> Step A: "क्या bag में कुछ है?" ✓  
> Step B: "कितने letters हैं?" → 37  
> Step C: "क्या यह सही count है?" → हाँ, 37 ✓  
> Step D: "हर letter निकालो और सही बन्दे को दो" → rxData1 = voltage, rxData2 = current...

---

### Step 5: Data कहाँ Store होता है? — `rxData[]` Array

```c
// Axi_IO.c — rxData structures
axiData rxData2 = {          // Word #2 = i_high (current)
    .ptr = (void*) &PEBB_Control_U.feedback[0],  // ← यहाँ store होगा
    .axiType = float_,        // RTDS float भेजता है
    .localType = double_,     // हम double में store करते हैं
    .scaleFactor = UNIT_SCALE // RTDS kA भेजता है, हम A में चाहते हैं (×1000)
};
```

**Story:**  
> हर word का एक "पता" है। जैसे:
> - Word 1 → Security key (ignore करो)
> - Word 2 → Current reading (i_high) → feedback[0] में जाएगा
> - Word 8 → Next state command → PEBB_NEXT_STATE_CMD में जाएगा
> - Word 35 → Run/Stop signal → `run` variable में जाएगा

---

### Step 6: Packet का "अंत" कैसे पता चलता है?

**Answer:** RTDS हर बार exactly **37 words** भेजता है। जब FIFO को `RECEIVE_LENGTH` (37) words मिल जाते हैं, तो वो interrupt देता है। Software `ReceiveLength == RECEIVE_LENGTH` check करता है।

```c
// Axi_IO.c line 614
if (ReceiveLength != RECEIVE_LENGTH) {
    // WRONG packet size — discard!
    commGlitchCtr++;
}
```

**Bonus:** Sequence number (rxDataSeq) भी होता है:
```c
// Axi_IO.c line 335
axiData rxDataSeq = {
    .ptr = (void*) &seq,  // sequence number यहाँ जाएगा
    .axiType = s32_,
    ...
};
```
अगर sequence number skip हुई → `"Warning: missing packet"` print होती है।

---

## 📦 Part 5: TX Path — हम RTDS को data कैसे भेजते हैं?

**Flow:**
```
runControlCycle()
    → PEBB_Control_step()   // Controller calculate करता है
    → TxSend()
        → axiSend()         // FIFO में data डालो
```

```c
// Axi_IO.c line 702-754
void axiSend(XLlFifo *InstancePtr)
{
    packedInt = packBits();  // Bit flags को pack करो (8 bits → 1 word)
    
    for(i = 0; i < TRANSMIT_LENGTH; i++) {
        XLlFifo_TxPutWord(InstancePtr, ...);  // हर word FIFO में डालो
    }
    
    // TX length set करो — यही "send" trigger करता है
    XLlFifo_iTxSetLen(InstancePtr, (TRANSMIT_LENGTH * WORD_SIZE));
}
```

**TX में 23 words जाते हैं** (TRANSMIT_LENGTH = 23):
- Duty cycles (switch signals)
- Current state, Next state
- Error flags
- Packed bit flags (lowside contact, enable, fault, etc.)

---

## 📦 Part 6: IP-Core Configuration — "Knobs" क्या हैं?

| Parameter           | Value          | कहाँ Define है        | Purpose                           |
| ------------------- | -------------- | ------------------ | --------------------------------- |
| `RECEIVE_LENGTH`    | 37             | `Axi_IO.h` line 23 | Kitne words RTDS भेजता है            |
| `TRANSMIT_LENGTH`   | 23             | `Axi_IO.h` line 22 | Kitne words हम RTDS को भेजते हैं       |
| `WORD_SIZE`         | 4 bytes        | `Axi_IO.h` line 21 | एक word = 4 bytes (32-bit)        |
| `UNIT_SCALE`        | 1000           | `Axi_IO.h` line 26 | kA/kV → A/V conversion            |
| `SEQ_MAX`           | 1000           | `Axi_IO.h` line 27 | Max RTDS sequence number          |
| `NUM_PACKED_BITS`   | 8              | `Axi_IO.h` line 24 | Packed firing bits count          |
| `CTR_MAX`           | 8              | `main.c` line 96   | Controller timing divisor         |
| `PEBB_RATE`         | 1              | `main.c` line 97   | Run controller every N interrupts |
| `COMM_GLITCH_LIMIT` | 10             | `Axi_IO.c` line 59 | Max allowed bad packets           |
| `FIFO_DEV_ID`       | Hardware param | `main.c` line 75   | FIFO hardware base address        |

---

## 🗺️ Part 7: पूरी Journey — एक नज़र में

```
RTDS Simulator
     │
     │  (Fiber Optic Cable — Aurora Protocol)
     ▼
┌─────────────────────────────────────┐
│  FPGA / PL (Programmable Logic)     │
│  ┌─────────────┐  ┌──────────────┐  │
│  │ Aurora IP   │→ │  AXI-FIFO   │  │
│  │(Link Layer) │  │ (Buffer)    │  │
│  └─────────────┘  └──────┬───────┘  │
│                           │ Interrupt │
└───────────────────────────┼─────────┘
                            │
                            ▼
              ┌─────────────────────────┐
              │  A53 CPU / PS           │
              │                         │
              │  FifoHandler()          │ ← Interrupt आई!
              │    └─ FifoRecvHandler() │
              │         └─ axiReceive() │ ← FIFO से 37 words पढ़े
              │              └─ Store in rxData[] →  Controller
              │                                        Variables
              │  runControlCycle()      │
              │    └─ PEBB_Control_step()│ ← Control compute
              │    └─ TxSend()          │
              │         └─ axiSend()   │ ← FIFO में 23 words डाले
              └─────────────────────────┘
                            │
                            │ (Aurora TX path)
                            ▼
                       RTDS Simulator
                    (Receives our data back)
```

---

## ✅ M2-A Acceptance Criteria — तुम्हें क्या deliver करना है

### Checklist:

- [ ] **`docs/milestone2/M2-A_auroralink_findings.md`** बनाना है (3 pages से कम)
- [ ] **Push/Pull question** — एक sentence में answer (ऊपर Part 3 देखो)
- [ ] **FifoRecvHandler → axiReceive → XLlFifo_*** calls describe करो
- [ ] **IP config table** — ऊपर Part 6 देखो (sources: `Axi_IO.h`, `main.c`)
- [ ] **हर claim के साथ source** (file path + line number)

### Push/Pull का Exact Answer (deliverable के लिए):
> "The PL (Aurora IP core) streams data into an AXI-FIFO via hardware push; when a complete packet arrives, the FIFO hardware asserts an interrupt to the A53 PS, which then executes `FifoRecvHandler` → `axiReceive` to pull each word from the FIFO using `XLlFifo_RxGetWord` — making this an **interrupt-driven, software-pull** model from the PS perspective."

---

## 🧠 Summary — 5 Lines में याद करो

1. **RTDS** → Fiber Optic → **Aurora IP** (FPGA में) → **AXI-FIFO** (queue में store)
2. FIFO भर जाने पर → **Interrupt** → A53 CPU जागती है
3. CPU → `FifoRecvHandler` → `axiReceive` → **37 words पढ़े** → rxData[] में store
4. Controller calculate करता है → `axiSend` → **23 words TX FIFO में डाले** → RTDS को भेजे
5. **यह Push+Interrupt+Pull का combination है** — pure polling नहीं, pure DMA नहीं

---

## 🎯 अगला Step: M2-A File लिखना

अब तुम्हें `docs/milestone2/M2-A_auroralink_findings.md` बनानी है।  
इस document की knowledge use करो।  
Sources cite करना मत भूलो:
- `RTDS_Aurora_Link/.../src/Axi_IO.c` — axiReceive, axiSend
- `RTDS_Aurora_Link/.../src/Axi_IO.h` — configuration constants
- `RTDS_Aurora_Link/.../src/main.c` — FifoHandler, FifoRecvHandler, interrupt setup
- Xilinx PG080 (AXI4-Stream FIFO product guide)

