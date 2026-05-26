# 🔬 M2-A Deep Dive — हर सवाल का जवाब (Hindi में)
## RTDS → Aurora → AXI Stream → FIFO → Interrupt — पूरी Journey

---

## 🎯 तुम्हारे सवाल (एक-एक करके जवाब दूँगा):

1. क्या RTDS सारा data एक साथ भेजता है? (जैसे एक file?)
2. Aurora hardware कितना data एक साथ AXI stream में convert करता है?
3. AXI Stream क्या है?
4. FIFO कैसा होता है — queue में एक जगह कितना data?
5. Interrupt signal कब भेजता है?
6. "Complete Packet" क्या होता है?

---

# 🎭 पहले एक नई Story — "Train और Platform"

> **RTDS** = एक Train Driver जो हर 1ms (millisecond) में एक **Train (packet)** भेजता है।  
> यह train हमेशा **same size** की होती है — exactly **37 डिब्बे** (words)।
>
> **Aurora IP** = Railway Track + Signal System। Train को fiber optic track पर receive करता है।
>
> **AXI FIFO** = Railway Platform + Waiting Area। Train आती है, platform पर रुकती है।
>
> **Interrupt** = Platform पर लगी **घंटी** — "ट्रेन आ गई! अब passengers उतर सकते हैं!"
>
> **A53 CPU** = Passengers (data) को unload करने वाला worker।

---

# ❓ सवाल 1: क्या RTDS "सारा data एक साथ" भेजता है?

## जवाब: हाँ — RTDS एक बार में एक **पूरा packet** भेजता है।

### यह "file" नहीं है — यह "snapshot" है

RTDS एक **Real-Time Power System Simulator** है।  
यह electricity के circuits को real-time simulate करता है।

**हर 50 microseconds या 1ms में**, RTDS एक नया **simulation step** calculate करता है:
- Voltage = कितना? → ek number
- Current = कितना? → ek number
- State command = क्या? → ek number
- ... और ऐसे 36 values

इन **सभी values को एक साथ pack करके** वो एक **packet** बनाता है।  
यह packet हमेशा **fixed size** का होता है — **37 × 4 bytes = 148 bytes**।

### 🚂 Train analogy:
```
RTDS हर 1ms में एक "Train" भेजता है
Train में हमेशा exactly 37 डिब्बे होते हैं:
  डिब्बा 1  = Key (ignore करो)
  डिब्बा 2  = i_high (current A phase) = 1.25 kA
  डिब्बा 3  = vhigh (voltage B phase) = 11.0 kV
  डिब्बा 4  = vhigh (voltage A phase) = 11.0 kV
  ...
  डिब्बा 35 = run command (0 या 1)
  डिब्बा 36 = RXI_chil flag
  डिब्बा 37 = sequence number (1-1000)
```

### Code proof — `Axi_IO.h` line 23:
```c
#define RECEIVE_LENGTH 37  // 37 words हमेशा आते हैं — fixed!
```

---

# ❓ सवाल 2: Aurora Hardware — कितना data एक साथ convert करता है?

## जवाब: Aurora **एक पूरा packet** receive करता है और **एक complete AXI transaction** के रूप में FIFO में देता है।

### Step-by-step Aurora का काम:

```
Fiber Optic Cable
     │
     │  (Serial bits — एक के बाद एक bits आते हैं)
     │  010110001011100010...
     │
     ▼
┌─────────────────────────────────┐
│   Aurora 64B/66B IP Core (PL)   │
│                                  │
│  1. Serial bits को decode करो   │
│  2. 8B/10B या 64B/66B encoding  │
│     हटाओ (यह Aurora का protocol)│
│  3. Actual data निकालो          │
│  4. AXI4-Stream format में दो   │
└─────────────────────────────────┘
     │
     │  AXI4-Stream (parallel data — organized)
     ▼
```

### Aurora कितना data एक साथ process करता है?

Aurora IP core typically **32-bit या 64-bit wide** होता है।  
यानी हर **clock cycle** में **4 bytes (1 word) या 8 bytes (2 words)** process होते हैं।

लेकिन तुम्हारे लिए जो important है वो यह है:

> **Aurora IP का काम है बस bits को flip करना** — serial → parallel।  
> जो RTDS ने "एक packet" भेजा, वो **Aurora उसे AXI-Stream में convert करके FIFO में डाल देता है।**  
> तुम्हें individual clock cycles की चिंता नहीं करनी।

---

# ❓ सवाल 3: AXI Stream क्या है?

## जवाब: AXI-Stream एक "Highway" है जिस पर data chips के अंदर travel करता है।

### पहले — AXI क्या है?

**AXI = Advanced eXtensible Interface**  
यह Xilinx/ARM का एक **standard protocol** है।  
जैसे USB एक standard है — कोई भी USB device किसी भी computer से connect होती है।  
ऐसे ही — कोई भी AXI device किसी भी AXI bus से connect होती है।

### AXI-Stream specifically:

```
                    Data flow (एक direction में)
Aurora IP  ────────────────────────────────────►  AXI FIFO
           
           हर clock cycle में एक "beat" जाती है:
           
           Beat 1: [DATA=0x3F800000] [VALID=1] [LAST=0]  ← बीच में है
           Beat 2: [DATA=0x40000000] [VALID=1] [LAST=0]  ← बीच में है
           Beat 3: [DATA=0x3E000000] [VALID=1] [LAST=0]  ← बीच में है
           ...
           Beat 37:[DATA=0x00000064] [VALID=1] [LAST=1]  ← आखिरी! LAST=1
```

### तीन important signals:

| Signal  | मतलब                               |
| ------- | ---------------------------------- |
| `DATA`  | 32-bit actual data (1 word)        |
| `VALID` | "यह data valid है" — sender कह रहा है |
| `LAST`  | "यह आखिरी beat है" — packet खत्म!      |

### 🏎️ Highway analogy:
```
AXI-Stream = एक one-way highway

हर second एक car निकलती है (हर clock cycle एक word)
Car की number plate पर DATA लिखा है
Car की windshield पर VALID/LAST flag है

जब LAST=1 वाली car आती है → "convoy खत्म!"
                             → FIFO को interrupt signal मिलता है
```

### यह "stream" है — इसलिए:
- Data **रुकता नहीं** — continuously flow करता है
- **कोई address नहीं** — बस sequential words
- **Master → Slave** direction (Aurora = master, FIFO = slave)

---

# ❓ सवाल 4: FIFO कैसा होता है? Queue में कितना data?

## जवाब: FIFO एक "Line/Queue" है — पहले आया, पहले जाएगा।

### FIFO की Physical Structure:

```
FIFO अंदर से ऐसा दिखता है (एक पानी की टंकी की तरह):

        ┌─────────────────────────┐
        │                         │
IN ──►  │  [slot 1] word  = 0x3F  │
        │  [slot 2] word  = 0x40  │
        │  [slot 3] word  = 0x3E  │
        │  [slot 4] word  = 0x41  │
        │  [slot 5] word  = 0x00  │
        │  ...                    │
        │  [slot N] word  = ????  │  ← खाली slots
        │                         │
        └──────────┬──────────────┘
                   │
               OUT ▼
            (CPU यहाँ से पढ़ता है)
```

### FIFO की Size कितनी होती है?

**AXI4-Stream FIFO (PG080)** — इसकी depth configurable होती है।  
Common values: **512, 1024, 4096 words (32-bit each)**

हमारे case में:
- हर packet = **37 words = 148 bytes**
- FIFO depth = कम से कम **512 words** होगी
- यानी FIFO में एक बार में **~13-14 packets** आ सकते हैं store होने के लिए

> लेकिन practically — हमारा system हर packet के साथ process करता है,  
> तो FIFO में हमेशा **सिर्फ 1 packet** होता है (37 words)।

### FIFO में दो Pointer होते हैं:

```
Write Pointer (WR_PTR) ──►  Aurora यहाँ data लिखता है
Read Pointer  (RD_PTR) ──►  CPU यहाँ से data पढ़ता है

WR_PTR आगे बढ़ता है जब data आता है
RD_PTR आगे बढ़ता है जब CPU पढ़ता है

जब WR_PTR > RD_PTR → FIFO में data है (Occupancy > 0)
जब WR_PTR = RD_PTR → FIFO खाली है
```

### Code में देखो — `Axi_IO.c` line 610:
```c
if (XLlFifo_iRxOccupancy(InstancePtr)) {
//   ↑ यह check करता है: "क्या FIFO में कुछ है?"
//   अगर WR_PTR > RD_PTR → true → data है
```

### FIFO में "एक जगह" कितना data?

```
हर "slot" = 1 word = 32 bits = 4 bytes
FIFO depth = typically 512 words
Total capacity = 512 × 4 = 2048 bytes = 2 KB

हमारा 1 packet = 37 words = 148 bytes
इसलिए FIFO overflow नहीं होता।
```

---

# ❓ सवाल 5: Interrupt signal कब भेजता है? (सबसे Important!)

## जवाब: जब RTDS का **एक complete packet का आखिरी word** FIFO में आ जाता है।

### यह कैसे detect होता है?

AXI4-Stream FIFO IP core internally यह track करता है:

```
Aurora IP ──► AXI-Stream ──► FIFO IP Core
                                │
                         LAST=1 आया?
                                │
                          ┌─────▼──────┐
                          │  YES!      │
                          │  Packet    │
                          │  Complete! │
                          └─────┬──────┘
                                │
                         RC (Receive Complete)
                         interrupt flag SET होता है
                                │
                                ▼
                         A53 CPU को signal जाता है
                         "Data ready hai! Aa jao!"
```

### RTDS "LAST" signal कैसे देता है?

RTDS हमेशा **37 words** भेजता है।  
Aurora IP जानता है packet का end कब है — AXI-Stream का `TLAST` signal यही बताता है।

```
Aurora → FIFO: Beat 1  (TLAST=0) → word 1 आया
Aurora → FIFO: Beat 2  (TLAST=0) → word 2 आया
Aurora → FIFO: Beat 3  (TLAST=0) → word 3 आया
...
Aurora → FIFO: Beat 37 (TLAST=1) → word 37 आया ← PACKET COMPLETE!
                                                    INTERRUPT FIRE!
```

### Timeline देखो:

```
Time ──────────────────────────────────────────────►

RTDS भेजता है:   [pkt1 start]...[pkt1 end] [pkt2 start]...[pkt2 end]
                                  │                           │
                                  ▼                           ▼
FIFO INTERRUPT:               [INT!]                      [INT!]
                                  │                           │
A53 CPU:              [idle] ► [FifoRecvHandler]  [idle] ► [FifoRecvHandler]
                               [reads 37 words]            [reads 37 words]
                               [runs controller]           [runs controller]
                               [sends TX data]             [sends TX data]
                               [goes idle again]           [goes idle again]
```

### Code में interrupt setup — `main.c` line 281:
```c
XLlFifo_IntEnable(InstancePtr, XLLF_INT_ALL_MASK);
// ↑ यह line FIFO को enable करती है interrupt देने के लिए
// जब TLAST=1 आएगा → RC interrupt → FifoHandler call होगा
```

### Code में interrupt check — `main.c` line 434:
```c
if (Pending & XLLF_INT_RC_MASK) {
//              ↑ RC = Receive Complete
//   यह tab true होता है जब एक complete packet FIFO में आ जाता है
    FifoRecvHandler(InstancePtr);  // ← तब यह call होता है
}
```

---

# ❓ सवाल 6: "Complete Packet" क्या होता है?

## जवाब: एक Complete Packet = एक RTDS simulation step का पूरा data।

### Packet की Definition:

```
┌─────────────────────────────────────────────────────┐
│                 ONE COMPLETE PACKET                 │
│                 = 37 words = 148 bytes              │
├──────┬────────────────────────────────────────────  │
│ Word │ Content                                      │
├──────┼────────────────────────────────────────────  │
│  1   │ Key (security/sync key)                      │
│  2   │ i_high — Phase A current (float, kA)        │
│  3   │ vhigh B — Phase B voltage (float, kV)       │
│  4   │ vhigh A — Phase A voltage (float, kV)       │
│  5   │ v_dcc — DC capacitor voltage                │
│  6   │ i_low — Low side current                    │
│  7   │ (dummy — unused)                            │
│  8   │ PEBB_NEXT_STATE_CMD (float)                 │
│  9   │ P_CMD — Active power command (float, MW)    │
│  10  │ LOWSIDE_CONTACTOR_CMD                       │
│  11  │ PEBB_POWER_LOWER_LIMIT                      │
│  12  │ PEBB_POWER_UPPER_LIMIT                      │
│13-14 │ (dummies)                                   │
│  15  │ ctr — Sub-step counter (s32)               │
│16-19 │ (dummies)                                   │
│  20  │ V_CMD — Voltage command                     │
│  21  │ ForceFault command                          │
│22-25 │ (dummies)                                   │
│  26  │ EN_GRID_SUPPORT                             │
│  27  │ Q_CMD — Reactive power command (MVA)        │
│  28  │ Q_LIMIT                                     │
│  29  │ i_high_B — Phase B current                 │
│  30  │ i_high_C — Phase C current                 │
│  31  │ v_high_c — Phase C voltage                 │
│  32  │ v_stator_A                                  │
│  33  │ v_stator_B                                  │
│  34  │ v_stator_C                                  │
│  35  │ run — Run/Stop command (s32: 0 या 1)       │
│  36  │ RXI_chil — Loop closure flag               │
│  37  │ seq — Sequence number (1 to 1000)          │
└──────────────────────────────────────────────────── ┘
```

### "Complete" का मतलब:

**Incomplete packet (गलत) :**
```
RTDS ने word 1-20 भेजे → connection टूट गई → word 21-37 नहीं आए
FIFO में सिर्फ 20 words हैं
यह "incomplete packet" है → DISCARD करो!
```

**Complete packet (सही) :**
```
RTDS ने word 1-37 भेजे → सभी आए → TLAST=1 मिला
FIFO में exactly 37 words हैं
यह "complete packet" है → PROCESS करो!
```

### Code में यही check — `Axi_IO.c` line 614:
```c
ReceiveLength = (XLlFifo_iRxGetLen(InstancePtr)) / WORD_SIZE;

if (ReceiveLength != RECEIVE_LENGTH) {
//  ↑ अगर 37 words नहीं आए → bad packet!
    // Buffer को empty करो, error count बढ़ाओ
    commGlitchCtr++;
} else {
    // 37 words आए → process करो!
    for (i = 0; i < RECEIVE_LENGTH; i++) {
        RxWord = XLlFifo_RxGetWord(InstancePtr);
        // ...store करो...
    }
}
```

---

# 🎬 पूरी Story एक बार फिर — अब ज्यादा Detail के साथ

```
STEP 1: RTDS simulation चल रही है
        हर 1ms में एक नया step → 37 values calculate होती हैं
        ये सब pack होकर एक packet बनता है (148 bytes)

STEP 2: RTDS Aurora protocol में encode करता है
        Bits serial हो जाती हैं → fiber optic cable पर भेजता है
        010110001011110001...010010... (serial bit stream)

STEP 3: Kria के FPGA में Aurora IP receive करता है
        Serial bits → decode → 32-bit words बनाता है
        फिर AXI4-Stream format में convert करता है:
        
        Word 1: [DATA=0x00000001] [TLAST=0] → FIFO में
        Word 2: [DATA=0x3FA00000] [TLAST=0] → FIFO में
        Word 3: [DATA=0x41300000] [TLAST=0] → FIFO में
        ...
        Word 37:[DATA=0x00000064] [TLAST=1] → FIFO में ← LAST!

STEP 4: FIFO IP Core को TLAST=1 मिला
        "Packet complete!" → RC Interrupt flag set होता है
        A53 CPU को signal जाता है: "DATA READY!"

STEP 5: A53 CPU (जो idle था) INTERRUPT handle करता है
        FifoHandler() → FifoRecvHandler() → axiReceive()
        
        axiReceive() FIFO से एक-एक word पढ़ता है:
        XLlFifo_iRxOccupancy() → "हाँ data है"
        XLlFifo_iRxGetLen()    → "37 words हैं"
        
        Loop: i=0 to 36:
          RxWord = XLlFifo_RxGetWord()  ← FIFO से word निकालो
          rxData[i]->ptr पर store करो  ← सही variable में डालो
          (type conversion + scale factor apply)

STEP 6: Controller चलता है
        PEBB_Control_step() — calculations होती हैं
        Switch duty cycles, state commands calculate होते हैं

STEP 7: TX data FIFO में डालो → RTDS को वापस भेजो
        axiSend() → 23 words → Aurora TX → Fiber → RTDS
        
STEP 8: वापस IDLE — अगले RTDS packet का इंतज़ार
```

---

# 🧩 Bonus: तुम्हारे specific sub-questions के जवाब

### ❓ "RTDS 10 packets एक साथ भेज सकता है?"

**नहीं।** RTDS एक **real-time system** है।  
यह हर 1ms में exactly 1 packet भेजता है। Buffer नहीं करता।  
RTDS का design ही ऐसा है कि — "every step, one packet."

### ❓ "Line by line भेजता है?"

**Technically हाँ** — Aurora over fiber optic पर bits serial आते हैं।  
लेकिन **logically नहीं** — Aurora IP उन्हें एक complete packet की तरह handle करता है।  
FIFO को TLAST तक का wait करना होता है।

### ❓ "FIFO में एक जगह = कितना data?"

```
एक "slot" = 1 word = 32 bits = 4 bytes

FIFO depth examples:
  - 512 word depth  = 2 KB total
  - 1024 word depth = 4 KB total
  - 4096 word depth = 16 KB total

हमारा 1 packet = 37 words = 148 bytes
हमारी FIFO depth = हम config से जानेंगे (xparameters.h में होगा)
लेकिन clearly 37 >> FIFO size है — overflow का कोई risk नहीं
```

### ❓ "Complete Packet = किसी length का हो सकता है?"

**RTDS के case में — नहीं।** हमेशा 37 words।  
लेकिन AXI4-Stream protocol में theoretically packet कोई भी length का हो सकता है।  
TLAST signal packet boundary बताता है — 1 word का packet भी हो सकता है, 10000 का भी।

---

# 📌 Summary Table — एक नज़र में सब कुछ

| Concept           | Detail                                                      |
| ----------------- | ----------------------------------------------------------- |
| RTDS packet size  | **37 words = 148 bytes** (fixed, हमेशा)                       |
| RTDS packet rate  | **हर ~1ms** में 1 packet                                      |
| Aurora job        | Serial bits → 32-bit words → AXI4-Stream                    |
| AXI Stream        | FPGA के अंदर data transfer का highway (TDATA + TVALID + TLAST) |
| FIFO type         | AXI4-Stream FIFO (Xilinx IP, PG080)                         |
| FIFO slot size    | 1 word = 32 bits = 4 bytes                                  |
| FIFO depth        | 512+ words (config में set)                                   |
| Interrupt trigger | जब TLAST=1 आता है → RC (Receive Complete) interrupt           |
| Interrupt handler | `FifoHandler` → `FifoRecvHandler` → `axiReceive`            |
| Data reading      | `XLlFifo_RxGetWord()` को 37 बार call करो                       |
| Complete packet   | Exactly 37 words, TLAST=1 के साथ आया हो                         |

---

## अब तुम क्या कर सकते हो?

1. यह doc पढ़ लो।
2. `docs/milestone2/M2-A_auroralink_findings.md` लिखो।
3. ऊपर के answers directly use करो — Push/Pull explanation, packet structure table, interrupt flow।
4. Source cite करो: `Axi_IO.h` (constants), `Axi_IO.c` (axiReceive), `main.c` (FifoHandler/FifoRecvHandler).
