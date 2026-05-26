# PEBB CHIL Firmware — पूरा Technical Explanation (Hindi में)
## System: RTDS → Aurora → AXI FIFO → A53 CPU (Kria Board)
### `main.c` + `Axi_IO.c` — Step-by-Step पूरी Guide

> **एक आसान सी कहानी (पूरे document में इसे याद रखना):**
> - **RTDS** = एक Train Driver जो हर 125 microseconds में एक train भेजता है
> - **Aurora fiber link** = वो railway track जिस पर train चलती है
> - **AXI FIFO** = वो platform जहाँ train आकर खड़ी होती है
> - **Interrupt** = platform की वो घंटी जो train पूरी आ जाने पर बजती है
> - **A53 CPU** = Station Master जो सिर्फ तभी काम करता है जब घंटी बजती है
> - **main.c** = Station Master की instruction book (काम का नियम-कानून)
> - **Axi_IO.c** = goods manifest (कौन सा सामान कहाँ जाएगा, उसकी पूरी list)

---

## Part 1: System का Overview और Architecture

### 1.1 ये System क्या है?

दो "कमरे" आपस में बात कर रहे हैं:

| Room | नाम | काम |
|------|------|------|
| Room 1 | RTDS Simulator (बड़ा PC) | एक power grid को simulate करता है — हर ~125 µs में नई voltages, currents, commands calculate करके भेजता है |
| Room 2 | Kria Board (हमारा FPGA+Processor) | असली controller hardware जो वो values पढ़ता है, control algorithm चलाता है, और switch commands वापस भेजता है |

**इनके बीच connection:** एक fiber optic cable जो **Aurora 64B/66B** serial protocol use करती है।

---

### 1.2 तीन मुख्य Players (खिलाड़ी)

```
RTDS Simulator          Kria Board — FPGA (PL)              Kria Board — Processor (PS)
[Big Computer]  ──►   [Aurora IP + AXI FIFO]           ──►   [A53 CPU — हमारा Software]
  "Data भेजो"          "Receive करो, decode करो, buffer करो"   "Data पढ़ो, controller चलाओ"
```

| Player | ये क्या है | कहाँ रहता है |
|--------|-----------|----------------|
| RTDS Simulator | Real-Time Digital Simulator — power system को simulate करता है | बाहरी PC में |
| Aurora IP Core | FPGA का एक IP block — fiber से serial bits receive करता है, AXI Stream में convert करता है | PL (Programmable Logic / FPGA) में |
| AXI-FIFO IP Core | Buffer/queue — words को तब तक store रखता है जब तक CPU ready ना हो | PL (Programmable Logic / FPGA) में |
| A53 CPU | 64-bit ARM processor — हमारा software चलाता है | PS (Processing System) में |
| main.c | Manager software — interrupt setup करता है, flow control करता है | PS (A53 पर चलता है) |
| Axi_IO.c | Data expert — FIFO को read/write करता है, type conversion करता है | PS (A53 पर चलता है) |

---

### 1.3 Kria Board का Layout — PL बनाम PS

```
┌─────────────────────────────────────────────┐
│              KRIA BOARD                     │
│                                             │
│  ┌──────────────────┐  ┌──────────────────┐ │
│  │   PL (FPGA)      │  │   PS (Processor) │ │
│  │                  │  │                  │ │
│  │  Aurora IP       │  │  main.c          │ │
│  │  AXI FIFO IP     │  │  Axi_IO.c        │ │
│  │                  │  │                  │ │
│  │  (Hardware —     │  │  (Software —     │ │
│  │   chip में        │  │   A53 CPU इसे    │ │
│  │   physically     │  │   execute करता   │ │
│  │   wired है)      │  │   है)            │ │
│  └──────────────────┘  └──────────────────┘ │
└─────────────────────────────────────────────┘
```

> **मुख्य बात:** PL = hardware (Aurora, FIFO) — ये "code" नहीं हैं, ये physically configured logic gates हैं।
> PS = software (main.c, Axi_IO.c) — ये A53 CPU पर instructions की तरह चलते हैं।

---

### 1.4 Code Board तक कैसे पहुँचता है — `.elf` का पूरा Process

```
आपका PC (Windows):
├── src/main.c
├── src/Axi_IO.c
├── src/UART.c
└── src/codegen.h
         │
         │  Vitis IDE → Build Project
         │  (aarch64-none-elf-gcc cross-compiler)
         ▼
Debug/xllfifo_interrupt_example_1.elf   ← 666 KB का binary file
         │
         │  JTAG cable → Kria Board
         │  (या: jtag_boot.tcl script)
         ▼
Kria RAM ← यहाँ .elf load होता है
         │
         ▼
A53 CPU → एक-एक instruction करके main() execute करना शुरू कर देता है
```

- `.elf` = **Executable and Linkable Format** — Linux/embedded का वो equivalent जो Windows में `.exe` होता है
- Files hard disk से नहीं चलतीं — पहले **RAM** में load होती हैं, फिर CPU वहाँ से execute करता है
- `XLlFifo_RxGetWord()` जैसे functions Xilinx की BSP (Board Support Package) libraries से आते हैं — पहले से ही compile होकर `.elf` में link हो चुके होते हैं

---

### 1.5 Files का Role एक नज़र में

| File | कहाँ चलती है | Role |
|------|-----------|------|
| `main.c` | A53 CPU (PS) | **Manager:** interrupt setup, FifoHandler, FifoRecvHandler, runControlCycle |
| `Axi_IO.c` | A53 CPU (PS) | **Data Expert:** axiReceive (37 words अंदर), axiSend (23 words बाहर), rxData[] mapping, packBits() |
| `Aurora IP` | FPGA (PL) | **Hardware:** fiber से bits receive करता है → AXI Stream में convert → FIFO में डालता है |
| `AXI FIFO IP` | FPGA (PL) | **Hardware:** words को queue में store करता है, packet पूरा होने पर interrupt देता है, READ/WRITE pointers manage करता है |

---

## Part 2: Data Transfer का तरीका — Push, Pull, या Interrupt?

यही सबसे important architectural सवाल है।

---

### 2.1 Option A: Polling (Pull) — हम इसे क्यों use नहीं करते

```c
// POLLING example — हमारा code ये नहीं करता
while(1) {
    if (data_available()) {
        read_data();
    }
    // CPU हर loop में check करता ही रहता है — बेकार है!
}
```

**Problem:** CPU अपना 100% time सिर्फ "क्या data आया?" check करने में बर्बाद कर देगा — जैसे हर 5 minute में अपने दरवाज़े पर जाकर check करना कि डाक आई या नहीं। एक real-time 8 kHz system में ये बिल्कुल काम नहीं आता।

---

### 2.2 Option B: DMA/Hardware Push — हम इसे क्यों use नहीं करते

DMA (Direct Memory Access) में hardware अपने आप data को सीधे RAM में लिख देता है, CPU की कोई ज़रूरत नहीं पड़ती।

**हम क्यों नहीं use करते:** हमारे system में CPU को actively data types convert करने पड़ते हैं, scale factors apply करने होते हैं, और packet को validate करना होता है — ये काम software logic से होते हैं, सिर्फ memory copy से नहीं।

---

### 2.3 Option C: Interrupt-Driven (हमारा System) ✅

```
CPU while(1) loop में idle बैठा है, background tasks कर रहा है
         │
         │  ← 125 µs बाद
         │
FIFO hardware को पता चलता है: last word आ गया (TLAST=1)
         │
         │  Electric signal → wire HIGH हो जाता है → GIC तक
         │
GIC (Interrupt Controller):
    "FIFO interrupt आया! जाओ FifoHandler चलाओ!"
         │
CPU अपनी state save करता है, FifoHandler() पर jump करता है
         │
FifoHandler → FifoRecvHandler → axiReceive → 37 words read करता है
         │
Interrupt handle करने के बाद CPU वापस while(1) में आ जाता है
```

**ये सबसे best क्यों है:** CPU free रहता है दूसरे कामों के लिए जब वो wait कर रहा होता है। Hardware CPU को **exactly तब** बताता है जब काम करना है — कोई cycle waste नहीं, real-time 8 kHz operation के लिए perfect।

---

### 2.4 पूरा जवाब एक sentence में

> "PL (Aurora IP core) hardware push के through data को AXI-FIFO में stream करता है; जब complete packet आ जाता है (TLAST=1), तो FIFO hardware A53 PS को एक RC interrupt देता है, जिसके बाद A53 `FifoRecvHandler` → `axiReceive` execute करके `XLlFifo_RxGetWord` से एक-एक word FIFO से pull करता है — इसलिए PS की नज़र से ये एक **interrupt-driven, software-pull** model है।"

---

## Part 3: Data Path को गहराई से समझें — Aurora → AXI Stream → FIFO

### 3.1 AXI Stream क्या है?

**AXI = Advanced eXtensible Interface** — Xilinx/ARM का standard जो chip-to-chip communication के लिए use होता है। PCB के लिए USB जैसा: कोई भी AXI device किसी भी AXI bus से बात कर सकता है।

**AXI-Stream** खासकर: एक one-directional, continuous data stream। कोई addresses नहीं — बस sequential words Aurora IP → AXI FIFO में बहते रहते हैं।

```
                    Data flow (सिर्फ एक direction में)
Aurora IP  ──────────────────────────────────────►  AXI FIFO

हर clock cycle में एक "beat" transfer होता है:

Beat 1:  [DATA=0x3F800000] [VALID=1] [TLAST=0]  ← packet के बीच में
Beat 2:  [DATA=0x40000000] [VALID=1] [TLAST=0]  ← packet के बीच में
Beat 3:  [DATA=0x3E000000] [VALID=1] [TLAST=0]  ← packet के बीच में
...
Beat 37: [DATA=0x00000064] [VALID=1] [TLAST=1]  ← LAST WORD! Packet complete!
```

---

### 3.2 AXI Stream के Signals

| Signal | मतलब |
|--------|---------|
| `TDATA` | 32-bit actual data (per beat 1 word) |
| `TVALID` | Sender बोलता है: "ये data अभी valid है" |
| `TLAST` | Sender बोलता है: "ये packet का आखिरी word है" |

जब FIFO IP core को `TLAST=1` मिलता है → **RC (Receive Complete) interrupt generate होता है** → A53 CPU जाग जाता है।

---

### 3.3 FIFO का Structure — Read और Write Pointers

```
FIFO Memory (एक पानी की tanki की तरह):

        ┌─────────────────────────┐
IN ──►  │  [slot 1]  = 0x3F8...  │  ← WRITE PTR यहाँ से शुरू होता है
        │  [slot 2]  = 0x400...  │
        │  [slot 3]  = 0x3E0...  │
        │  [slot 4]  = 0x410...  │
        │  [slot 5]  = 0x000...  │
        │  ...                    │
        │  [slot N]  = (खाली)    │
        └──────────┬──────────────┘
                   │
               OUT ▼  ← READ PTR यहाँ से शुरू होता है
          (CPU यहाँ से read करता है)
```

- **WRITE PTR** तब आगे बढ़ता है जब Aurora एक word अंदर लिखता है
- **READ PTR** तब आगे बढ़ता है जब CPU `XLlFifo_RxGetWord()` call करता है
- `WRITE PTR > READ PTR` → FIFO में data है (occupancy > 0)
- `WRITE PTR == READ PTR` → FIFO खाली है

सारे 37 words पढ़ने के बाद: `READ PTR = WRITE PTR = 37` → FIFO खाली।

---

### 3.4 FIFO की Capacity

| Attribute | Value |
|-----------|-------|
| Slot का size | 1 word = 32 bits = 4 bytes |
| Typical depth | 512–4096 words |
| Total capacity | 512 × 4 = 2 KB (minimum) |
| हमारा packet size | 37 words = 148 bytes |
| कितने packets fit होंगे | ~13–14 packets (पर हम हमेशा अगले packet से पहले process कर लेते हैं) |

> Practical में, FIFO में एक time पर exactly **1 packet (37 words)** ही होता है — 8 kHz पर अगला packet आने से पहले हम इसे process कर लेते हैं।

---

### 3.5 "Complete Packet" का क्या मतलब है?

RTDS हमेशा हर simulation step में exactly **37 words = 148 bytes** भेजता है।

```
Complete packet (सही):
RTDS words 1–37 भेजता है → सब आ जाते हैं → TLAST=1 receive हो जाता है
FIFO में exactly 37 words → इसे PROCESS करो ✓

Incomplete packet (error — connection में glitch हो तो ये हो सकता है):
RTDS words 1–20 भेजता है → connection drop → words 21–37 कभी नहीं आए
FIFO में सिर्फ 20 words → ReceiveLength != 37 → DISCARD कर दो, commGlitchCtr++
```

```c
// Axi_IO.c line 614
ReceiveLength = (XLlFifo_iRxGetLen(InstancePtr)) / WORD_SIZE;

if (ReceiveLength != RECEIVE_LENGTH) {  // != 37
    // FIFO खाली करो, error log करो, process मत करो
    commGlitchCtr++;
    if (commGlitchCtr > COMM_GLITCH_LIMIT) {
        CHIL_error |= 1;
    }
}
```

---

### 3.6 37-Word Packet का पूरा Structure

RTDS हर ~125 µs (8 kHz) में ये packet भेजता है:

| Word | Signal | Type | Scale | Description |
|------|--------|------|-------|-------------|
| 1 | Key | — | — | Security/sync key (ignore किया जाता है) |
| 2 | i_high | float | ×1000 (kA→A) | Phase A high-side current |
| 3 | vhigh_B | float | ×1000 (kV→V) | Phase B AC voltage |
| 4 | vhigh_A | float | ×1000 (kV→V) | Phase A AC voltage |
| 5 | v_dcc | float | ×1000 | DC capacitor voltage |
| 6 | i_low | float | ×1000 | Low side current |
| 7 | (dummy) | — | — | Use नहीं हो रहा |
| 8 | PEBB_NEXT_STATE_CMD | float | — | अगली state का command |
| 9 | P_CMD | float | ×1e6 (MW→W) | Active power command |
| 10 | LOWSIDE_CONTACTOR_CMD | float | — | Contactor command |
| 11 | PEBB_POWER_LOWER_LIMIT | float | ×1e6 | Power lower limit |
| 12 | PEBB_POWER_UPPER_LIMIT | float | ×1e6 | Power upper limit |
| 13–14 | (dummy) | — | — | Use नहीं हो रहा |
| 15 | ctr | s32 | — | Sub-step counter |
| 16–19 | (dummy) | — | — | Use नहीं हो रहा |
| 20 | V_CMD | float | — | Voltage command |
| 21 | ForceFault | float | — | Force fault command |
| 22–25 | (dummy) | — | — | Use नहीं हो रहा |
| 26 | EN_GRID_SUPPORT | float | — | Grid support enable |
| 27 | Q_CMD | float | ×1e6 (MVA→VA) | Reactive power command |
| 28 | Q_LIMIT | float | ×1e6 | Reactive power limit |
| 29 | i_high_B | float | ×1000 | Phase B current |
| 30 | i_high_C | float | ×1000 | Phase C current |
| 31 | v_high_c | float | ×1000 | Phase C voltage |
| 32 | v_stator_A | float | ×1000 | Stator voltage A |
| 33 | v_stator_B | float | ×1000 | Stator voltage B |
| 34 | v_stator_C | float | ×1000 | Stator voltage C |
| 35 | run | s32 | — | 1=controller चलाओ, 0=रोक दो |
| 36 | RXI_chil | s32 | — | Closed-loop flag |
| 37 | seq | s32 | — | Sequence number (1–1000) |

```c
// Axi_IO.h
#define RECEIVE_LENGTH  37   // RTDS → Kria (हमेशा fixed)
#define TRANSMIT_LENGTH 23   // Kria → RTDS (controller outputs)
#define WORD_SIZE        4   // per 32-bit word के bytes
```

---

## Part 4: Scene-by-Scene Code Flow

---

### Scene 1 — SETUP (Boot के time एक बार चलता है)

**क्या होता है:** जब `.elf` load होती है तो `main()` call होती है। कोई भी data आने से पहले सब कुछ initialize होता है।

---

#### Step 1A — `main()` `setupUART()` को call करता है

```c
// main.c  Line 177–184
int main()
{
    int Status;

    if (setupUART() == XST_FAILURE) {
        return XST_FAILURE;
    }

    xil_printf("--- Entering main() ---\n\r");
```

- UART = serial port = console terminal (keyboard/screen connection)
- सबसे पहले इसे setup करना ज़रूरी है ताकि `xil_printf()` से debug prints काम कर सकें
- आसान भाषा में: "कोई भी announcement करने से पहले intercom चालू करो"

---

#### Step 1B — `main()` `XLlFifoInterruptExample()` को call करता है

```c
// main.c  Line 188–193
while (1) {
    Status = XLlFifoInterruptExample(&FifoInstance, FIFO_DEV_ID);
    if (Status != XST_SUCCESS) break;
}
```

- `&FifoInstance` = हमारे FIFO hardware object का pointer (Line 132 पर defined)
- `FIFO_DEV_ID` = hardware ID — driver को बताता है कौन सी FIFO chip use करनी है
- `while(1)` wrapper retry allow करता है अगर FIFO fail हो जाए (normally अंदर ही हमेशा चलता रहता है)

---

#### Step 1C — `XLlFifoInterruptExample()` के अंदर — Hardware Init

```c
// main.c  Line 244–258
Config = XLlFfio_LookupConfig(DeviceId);
if (!Config) {
    xil_printf("No config found for %d\r\n", DeviceId);
    return XST_FAILURE;
}

Status = XLlFifo_CfgInitialize(InstancePtr, Config, Config->BaseAddress);
if (Status != XST_SUCCESS) {
    xil_printf("Initialization failed\n\r");
    return Status;
}
```

- `XLlFfio_LookupConfig()` → इस FIFO chip के लिए hardware address table देखता है (base address, etc. return करता है)
- `XLlFifo_CfgInitialize()` → सारे FIFO registers को default पर reset करता है, physical hardware address को map करता है ताकि software access कर सके
- FIFO **PL (FPGA)** में है। CPU इसे **memory-mapped registers** से access करता है — एक specific memory address को read/write करना ही FIFO hardware को read/write करना है।

---

#### Step 1D — पुराने Interrupts Clear करो

```c
// main.c  Line 261–269
Status = XLlFifo_Status(InstancePtr);
XLlFifo_IntClear(InstancePtr, 0xffffffff);  // सारे interrupt flags clear करो
Status = XLlFifo_Status(InstancePtr);
if(Status != 0x0) {
    xil_printf("\n ERROR : Reset value of ISR0 : 0x%x\t Expected : 0x0\n\r",
               XLlFifo_Status(InstancePtr));
    return XST_FAILURE;
}
```

- पिछली run से बचे हुए कोई भी stale interrupt flags clear करता है
- `0xffffffff` = सारे bits set = एक साथ हर flag clear कर दो
- फिर verify करता है कि status register अब zero है (clean है)
- आसान भाषा में: "Station खोलने से पहले make sure करो कि कोई पुरानी घंटी अभी भी नहीं बज रही"

---

#### Step 1E — Interrupt System Setup ← सबसे IMPORTANT STEP

```c
// main.c  Line 275–279
Status = SetupInterruptSystem(&Intc, InstancePtr, FIFO_INTR_ID);
if (Status != XST_SUCCESS) {
    xil_printf("Failed intr setup\r\n");
    return XST_FAILURE;
}
```

ये `SetupInterruptSystem()` call करता है जो 5 sub-steps करता है:

**Sub-step i — GIC (General Interrupt Controller) को Initialize करो**
```c
// main.c  Line 601–610
IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
                IntcConfig->CpuBaseAddress);
```
- GIC = वो hardware chip जो SoC के सारे interrupts manage करता है
- GIC का memory address ढूँढता है, driver को initialize करता है
- आसान भाषा में: "घंटी manager को जगाओ और ready करो"

**Sub-step ii — Priority और Trigger Type Set करो**
```c
// main.c  Line 612
XScuGic_SetPriorityTriggerType(IntcInstancePtr, FifoIntrId, 0xA0, 0x3);
```
- `0xA0` = priority level (number जितना कम, priority उतनी ज़्यादा)
- `0x3` = rising-edge triggered (signal LOW→HIGH होने पर interrupt fire होगा)
- आसान भाषा में: "घंटी manager को बताओ: FIFO बेल important है, HIGH होने पर सुनो"

**Sub-step iii — `FifoHandler` को Callback के रूप में Register करो ← KEY LINE**
```c
// main.c  Line 619–621
Status = XScuGic_Connect(IntcInstancePtr, FifoIntrId,
                (Xil_InterruptHandler)FifoHandler,
                InstancePtr);
```
- GIC को बताता है: "जब FIFO interrupt fire हो → `FifoHandler()` function को call करो"
- `FifoHandler` एक **function pointer** है — हमारे handler का memory address
- `InstancePtr` = FIFO instance pointer, जो FifoHandler call होने पर argument के रूप में pass होता है
- आसान भाषा में: "घंटी manager को बताओ: जब FIFO की घंटी बजे, तो इस specific आदमी को बुलाओ"

**Sub-step iv — GIC में Interrupt Enable करो**
```c
// main.c  Line 626
XScuGic_Enable(IntcInstancePtr, FifoIntrId);
```
- अगर ये नहीं किया तो GIC को interrupt के बारे में पता तो है पर वो silently ignore कर देगा
- आसान भाषा में: "घंटी panel में FIFO बेल का switch ON कर दो"

**Sub-step v — CPU Exception Table के साथ Register करो**
```c
// main.c  Line 637–644
Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
    (Xil_ExceptionHandler)INTC_HANDLER,
    (void *)IntcInstancePtr);

Xil_ExceptionEnable();
```
- A53 CPU में एक **exception vector table** होती है — "X होने पर क्या करना है"
- हम CPU को बताते हैं: "जब कोई भी interrupt आए, तो `INTC_HANDLER` (GIC dispatcher) को call करो"
- फिर GIC dispatcher देखता है कौन सा specific interrupt fire हुआ → registered callback call करता है
- आसान भाषा में: "Station Master को बताओ: कोई भी बेल बजे, पहले घंटी manager से पूछो"

---

#### Step 1F — FIFO Interrupts Enable करो

```c
// main.c  Line 281
XLlFifo_IntEnable(InstancePtr, XLLF_INT_ALL_MASK);
```

- सारे types के FIFO interrupts enable करता है: TX complete, RX complete, errors
- `XLLF_INT_ALL_MASK` = सारे bits set वाला bitmask
- आसान भाषा में: "FIFO hardware को बताओ: अब तुम कोई भी घंटी बजा सकते हो"

---

#### Step 1G — Controller Initialize करो + Waiting Loop में जाओ

```c
// main.c  Line 284–294
setPEBBLRU();                    // LRU count initialize करो
PEBB_Control_initialize(initTime); // controller model initialize करो
initPEBB();                      // PEBB hardware initialize करो

xil_printf("Paused. Please Start RTDS model now. \n\r");

while(1) {          // ← CPU यहाँ बैठकर WAIT करता है
    readUART();     // UART console commands check करो
    // errors check करो, diagnostics print करो...
}
```

- `PEBB_Control_initialize()` = PLECS/Simulink auto-generated control model को initialize करना
- `initPEBB()` = hardware initialize करना (gate drivers, contactors, etc.)
- फिर CPU **infinite loop** में जाता है जहाँ सिर्फ हल्के background tasks करता है
- **सारा असली काम interrupts में होता है** — इस loop में CPU कुछ भारी नहीं करता

> **Scene 1 का Summary:** Program start → UART setup → FIFO hardware init → GIC `FifoHandler` register करता है → interrupts enable → controller init → CPU infinite wait loop में। यहाँ से सब कुछ interrupt-triggered है।

---

### Scene 2 — RTDS DATA आ गया — INTERRUPT FIRE!

**क्या होता है:** RTDS हर 125 µs (8 kHz) में 37 words भेजता है। Packet का सफर:
`RTDS → fiber → Aurora IP (FPGA) → AXI Stream → AXI FIFO (PL)`

जब **37वाँ और आखिरी word** आता है (`TLAST=1` के साथ), तो FIFO hardware:
1. अपने interrupt status register में **RC (Receive Complete)** flag set करता है
2. GIC को एक **electric signal** भेजता है (wire HIGH हो जाता है)

```
┌─────────────────────────────────────────────────────────────────┐
│  HARDWARE SIDE (PL - FPGA)          SOFTWARE SIDE (PS - CPU)   │
│                                                                 │
│  RTDS 37 words भेजता है                                          │
│       ↓                                                         │
│  Aurora IP fiber से receive करता है                              │
│  (serial bits → 32-bit words → AXI Stream)                     │
│       ↓                                                         │
│  AXI FIFO words [1..37] store करता है                            │
│       ↓                                                         │
│  Word 37 TLAST=1 के साथ आता है                                   │
│       ↓                                                         │
│  FIFO ISR register में RC flag set करता है                       │
│       ↓                                                         │
│  Electric signal → wire HIGH ──────────────────► GIC            │
│                                                         ↓       │
│                                             GIC FIFO intr देखता │
│                                                         ↓       │
│                                              CPU interrupted!   │
│                                              (while loop pause) │
│                                                         ↓       │
│                                              FifoHandler()      │
│                                              call होता है!      │
└─────────────────────────────────────────────────────────────────┘
```

**Aurora packet को कैसे process करता है:**

```
Fiber Optic Cable
     │  (serial bits: 010110001011100010...)
     ▼
Aurora 64B/66B IP Core (PL):
  1. Serial bits decode करता है
  2. 64B/66B line encoding remove करता है
  3. Actual data words को reconstruct करता है
  4. AXI4-Stream beats के रूप में output करता है
     │
     │  Beat 1:  [TDATA=word1]  [TLAST=0]
     │  Beat 2:  [TDATA=word2]  [TLAST=0]
     │  ...
     │  Beat 37: [TDATA=word37] [TLAST=1]  ← PACKET COMPLETE!
     ▼
AXI FIFO
```

> **मुख्य बात:** Aurora per clock cycle एक word process करता है (32 या 64 bits wide)। पर software की नज़र से हमें सिर्फ पूरे packet के आने से मतलब है — Aurora serial-to-parallel conversion transparently handle करता है।

CPU `while(1)` में `readUART()` कर रहा था। GIC CPU को tap करता है: **"FIFO interrupt! FifoHandler चलाओ!"** CPU अपनी state save करता है (registers, program counter) और `FifoHandler()` पर jump करता है। ये पूरा काम **हार्डवेयर में होता है** — कोई polling नहीं।

---

### Scene 3 — `FifoHandler()` — कौन सा Interrupt Type?

**क्या होता है:** `FifoHandler()` को GIC call करता है। इसे determine करना होता है कि **कौन सा type** का interrupt fire हुआ। FIFO multiple interrupt types generate कर सकता है:

| Flag | मतलब |
|------|---------|
| RC | Receive Complete — data आ गया (जिसकी हमें चिंता है) |
| TC | Transmit Complete — हमारा outgoing data भेज दिया गया |
| Error | कुछ गलत हो गया (overflow, underflow, etc.) |

```c
// main.c  Line 428–456
static void FifoHandler(XLlFifo *InstancePtr)
{
    u32 Pending;

    // Step 1: FIFO से पूछो: "कौन सा interrupt flag set है?"
    Pending = XLlFifo_IntPending(InstancePtr);

    while (Pending) {   // loop में रहो अगर एक साथ multiple flags set हों

        if (Pending & XLLF_INT_RC_MASK) {
            // RC flag set है → data receive हुआ!
            XLlFifo_IntClear(InstancePtr, XLLF_INT_RC_MASK); // acknowledge/clear

            if (Rx_Done) {
                intOverrunFlag = 1; // WARNING: पिछला cycle अभी खत्म नहीं हुआ!
            }
            FifoRecvHandler(InstancePtr); // → received data handle करो

        } else if (Pending & XLLF_INT_TC_MASK) {
            // TC flag → transmit complete
            XLlFifo_IntClear(InstancePtr, XLLF_INT_TC_MASK);
            if (Tx_Done) {
                intOverrunFlag = 1;
            }
            FifoSendHandler(InstancePtr);

        } else if (Pending & XLLF_INT_ERROR_MASK) {
            // Error हुआ
            FifoErrorHandler(InstancePtr, Pending);
            XLlFifo_IntClear(InstancePtr, XLLF_INT_ERROR_MASK);

        } else {
            XLlFifo_IntClear(InstancePtr, Pending); // unknown flags clear करो
        }

        // फिर check करो — क्या और flags pending हैं?
        Pending = XLlFifo_IntPending(InstancePtr);
    }
}
```

**Line-by-line breakdown:**

| Code | क्या करता है |
|------|-------------|
| `XLlFifo_IntPending()` | ISR (Interrupt Status Register) पढ़ता है — कौन से flags set हैं वो return करता है |
| `Pending & XLLF_INT_RC_MASK` | Bitwise AND — check करता है कि specifically RC bit set है या नहीं |
| `XLlFifo_IntClear(...RC_MASK)` | ISR में RC bit पर 1 लिखकर उसे clear/acknowledge करता है |
| `if (Rx_Done) intOverrunFlag=1` | पिछला data process नहीं हुआ था — **timing overrun!** |
| `FifoRecvHandler(InstancePtr)` | Receive handler call करता है data process करने के लिए |

> **Interrupt flag को processing से पहले क्यों clear करें?** अगर हम clear नहीं करेंगे तो FIFO बार-बार interrupt trigger करता रहेगा। Hardware तब तक flag set रखता है जब तक software उसे explicitly 1 लिखकर acknowledge नहीं कर देता।

> **`intOverrunFlag`** तब set होता है जब `Rx_Done=1` (पिछला cycle नहीं हुआ था) पर नया interrupt fire हो जाए। इसका मतलब system 8 kHz से slow चल रहा है — एक **timing overrun** problem है जिसकी investigation ज़रूरी है।

---

### Scene 4 — `FifoRecvHandler()` — Processing को Orchestrate करना

**क्या होता है:** एक छोटा function जो तीन operations को sequence में करता है।

```c
// main.c  Line 470–480
static void FifoRecvHandler(XLlFifo *InstancePtr)
{
    // 1. FIFO से सारे 37 words variables में read करो
    axiReceive(InstancePtr);   // → Axi_IO.c

    // 2. Mark करो कि receive done हो गया
    Rx_Done = 1;

    // 3. Update करो कितने LRUs connected हैं
    setPEBBLRU();

    // 4. Control algorithm चलाओ + response transmit करो
    if (runControlCycle(InstancePtr) == XST_FAILURE) {
        CHIL_error |= 1;
    }
}
```

| Call | क्या करता है |
|------|-------------|
| `axiReceive()` | FIFO से 37 words read करता है, types convert करता है, controller variables में store करता है (Axi_IO.c में) |
| `Rx_Done = 1` | `volatile` flag — बताता है "इस cycle का data अब RAM में है" |
| `setPEBBLRU()` | Received data के आधार पर active LRUs (Line Replaceable Units) की count update करता है |
| `runControlCycle()` | PEBB controller चलाता है + 23 words RTDS को वापस भेजता है |

> `volatile int Rx_Done` — `volatile` keyword compiler को बताता है: "ये variable किसी भी time interrupt से change हो सकता है, इसे CPU register में cache मत करना।"

---

### Scene 5 — `axiReceive()` — सारे 37 Words Read करना (READ PTR यहाँ Move होता है!)

ये **Axi_IO.c का दिल है**। यहीं READ pointer आगे बढ़ता है।

```c
// Axi_IO.c  Line 604–684
void axiReceive(XLlFifo *InstancePtr)
{
    int i;
    u32 RxWord;
    seq_prev = seq;  // validation के लिए पिछला sequence number save करो

    // ─── STEP 1: FIFO में कोई data है? ────────────────────────────
    if (XLlFifo_iRxOccupancy(InstancePtr)) {

        // ─── STEP 2: Packet length read करो ──────────────────────
        ReceiveLength = (XLlFifo_iRxGetLen(InstancePtr)) / WORD_SIZE;
        // XLlFifo_iRxGetLen() bytes return करता है → 4 से divide करो → word count
        // Expected: 37 words

        // ─── STEP 3: गलत size packet — drain और discard ─────────
        if (ReceiveLength != RECEIVE_LENGTH) {
            for (i = 0; i < ReceiveLength; i++) {
                RxWord = XLlFifo_RxGetWord(InstancePtr); // drain (READ PTR move!)
            }
            seq = 0;
            xil_printf("Warning: receive Length mismatch...");
            commGlitchCtr++;
            if (commGlitchCtr > COMM_GLITCH_LIMIT) {
                CHIL_error |= 1;
            }

        // ─── STEP 4: सही size — सारे 37 words process करो ─────────
        } else {
            type_ thisAxiType;
            type_ thisLocalType;
            float unitScale;
            float RxValue;
            int isNan;

            for (i = 0; i < RECEIVE_LENGTH; i++) {  // i = 0 to 36

                // ← FIFO से Word i READ करो (READ PTR यहाँ 1 से आगे बढ़ता है!)
                RxWord = XLlFifo_RxGetWord(InstancePtr);

                // इस word का destination और type look up करो
                thisAxiType   = rxData[i]->axiType;    // float_? s32_? u32_?
                thisLocalType = rxData[i]->localType;  // double_? s32_?
                unitScale     = rxData[i]->scaleFactor; // 1? 1000?

                isNan = 0;

                // NaN check करो (corrupted float data)
                if (thisAxiType == float_) {
                    RxValue = (*(float *) &RxWord);
                    if (RxValue != RxValue) { // NaN: NaN != NaN हमेशा true होता है
                        isNan = 1;
                    }
                }

                if (isNan) {
                    CHIL_error |= 1;
                    NanRxCtr++;

                // Case 1: दोनों side same type, scaling नहीं → direct copy
                } else if ((thisAxiType == u32_ || thisAxiType == s32_ || thisAxiType == float_)
                        && thisLocalType == thisAxiType && unitScale == 1) {
                    u32* rxPtr = (u32 *) rxData[i]->ptr;
                    *rxPtr = RxWord;  // raw 32-bit word directly copy करो

                // Case 2: wire पर float → RAM में float, with scaling
                } else if (thisAxiType == float_ && thisLocalType == float_) {
                    float* rxPtr = (float*) rxData[i]->ptr;
                    RxValue = (*(float *) &RxWord);
                    *rxPtr = RxValue * unitScale;

                // Case 3: wire पर float → RAM में double (सबसे common!)
                } else if (thisAxiType == float_ && thisLocalType == double_) {
                    double* rxPtr = (double*) rxData[i]->ptr;
                    RxValue = (*(float *) &RxWord); // bytes को float के रूप में reinterpret
                    *rxPtr = (double) RxValue * unitScale; // double में cast, scale
                }
            }
        }

    } else {
        xil_printf("Error: RX interrupt with no RX FIFO Occupancy");
        CHIL_error |= 1;
    }
}
```

---

#### READ PTR कैसे Move होता है

```
axiReceive() से पहले FIFO Memory:
┌──────┬──────┬──────┬──────┬──────┬──────┐
│Word 1│Word 2│Word 3│ ...  │Word36│Word37│
└──────┴──────┴──────┴──────┴──────┴──────┘
↑                                          ↑
READ PTR (=0)                         WRITE PTR (=37)

XLlFifo_RxGetWord() call #1 के बाद:
  READ PTR = 1 → Word 1 consume हुआ

XLlFifo_RxGetWord() call #2 के बाद:
  READ PTR = 2 → Word 2 consume हुआ

... 37 calls बाद ...

READ PTR = 37 = WRITE PTR → FIFO EMPTY है ✓
```

`XLlFifo_RxGetWord()` एक Xilinx library function है जो:
1. FIFO के RX data register से value पढ़ता है
2. FIFO hardware **अपने आप read pointer आगे बढ़ा देता है**
3. Software कभी pointer को manually touch नहीं करता — बस function call करो और pointer आगे बढ़ जाता है

---

#### `rxData[]` Array — Address Book

37 में से हर word का एक descriptor struct है जो बताता है उसे कहाँ store करना है:

```c
// Axi_IO.h  Line 40–45
typedef struct {
    void*  ptr;          // इस data को कहाँ store करना है (memory address)
    type_  localType;    // locally कैसे store है (double, s32, float, etc.)
    type_  axiType;      // AXI bus पर कैसे आता है (float, s32, u32)
    float  scaleFactor;  // receive करने के बाद इससे MULTIPLY करो
} axiData;
```

**Example — Word 2 (i_high, Phase A current):**
```c
// Axi_IO.c  Line 87–92
axiData rxData2 = {
    .ptr         = (void*) &PEBB_Control_U.feedback[0],  // यहाँ store करो
    .axiType     = float_,    // 32-bit IEEE 754 float के रूप में आता है
    .localType   = double_,   // RAM में 64-bit double के रूप में store करो
    .scaleFactor = UNIT_SCALE // 1000 से multiply (RTDS kA भेजता है, हमें A चाहिए)
};
```

**पूरा rxData[] array — सारे 37 word destinations:**

```c
// Axi_IO.c  Line 573–578
volatile axiData *rxData[RECEIVE_LENGTH] = {
    &rxData1,  // Word 1:  key (ignored)
    &rxData2,  // Word 2:  feedback[0]  = i_high phase A current
    &rxData3,  // Word 3:  feedback[4]  = vhigh B phase AC voltage
    &rxData4,  // Word 4:  feedback[3]  = vhigh A phase AC voltage
    &rxData5,  // Word 5:  feedback[7]  = v_dcc DC capacitor voltage
    &rxData6,  // Word 6:  feedback[6]  = i_low current
    &rxData7,  // Word 7:  dummyDoubleRx (unused)
    &rxData8,  // Word 8:  PEBB_NEXT_STATE_CMD
    &rxData9,  // Word 9:  P_CMD (power command MW→kW)
    &rxData10, // Word 10: LOWSIDE_CONTACTOR_CMD
    &rxData11, // Word 11: PEBB_POWER_LOWER_LIMIT
    &rxData12, // Word 12: PEBB_POWER_UPPER_LIMIT
    &rxData13, // Word 13: dummy
    &rxData14, // Word 14: dummy
    &rxData15, // Word 15: ctr (substep counter)
    &rxData16, // Word 16: dummy
    &rxData17, // Word 17: dummy
    &rxData18, // Word 18: dummy
    &rxData19, // Word 19: dummy
    &rxData20, // Word 20: V_CMD (voltage command)
    &rxData21, // Word 21: ForceFault
    &rxData22, // Word 22: dummy
    &rxData23, // Word 23: dummy
    &rxData24, // Word 24: dummy
    &rxData25, // Word 25: dummy
    &rxData26, // Word 26: EN_GRID_SUPPORT
    &rxData27, // Word 27: Q_CMD (reactive power MVA→kVA)
    &rxData28, // Word 28: Q_LIMIT
    &rxData29, // Word 29: feedback[1]  = i_high_B
    &rxData30, // Word 30: feedback[2]  = i_high_C
    &rxData31, // Word 31: feedback[5]  = v_high_c
    &rxData32, // Word 32: feedback[8]  = v_stator_A
    &rxData33, // Word 33: feedback[9]  = v_stator_B
    &rxData34, // Word 34: feedback[10] = v_stator_C
    &rxData35, // Word 35: run (1=controller चलाओ, 0=रोक दो)
    &rxData36, // Word 36: RXI_chil (close loop flag)
    &rxDataSeq // Word 37: seq (packet verification के लिए sequence number)
};
```

---

#### Type Conversion — Type Punning समझाया गया

`axiReceive` की सबसे confusing line:

```c
RxValue = (*(float *) &RxWord);
```

Step by step:
```
RxWord एक u32 है (unsigned 32-bit integer)
उदा. RxWord = 0x3F800000

&RxWord           → RxWord का memory address (एक u32*)
(float *)&RxWord  → उसी address को ऐसे treat करो जैसे ये float pointer हो
*(float *)&RxWord → उस address पर जो bytes हैं उन्हें float के रूप में read करो
                  = 1.0f   (0x3F800000 IEEE 754 में 1.0 है)
```

ये **type punning** है — हम value को convert नहीं करते, हम same raw bytes को different type के रूप में reinterpret करते हैं। RTDS floats को उनके raw IEEE 754 32-bit bit pattern के रूप में भेजता है, इसलिए हम भी उसे वैसे ही read करते हैं।

---

### Scene 6 — `runControlCycle()` — Controller + TX

```c
// main.c  Line 354–387
int runControlCycle(XLlFifo *InstancePtr)
{
    // ── STEP 1: STOP CHECK ────────────────────────────────────────
    if (!run && controllerStarted) {
        // RTDS ने run=0 भेजा हम पहले से चल रहे थे → STOP
        controllerStopped = 1;
        PEBB_Control_U.ForceFault = 1; // safe freewheeling mode में जाओ
    }

    // ── STEP 2: CONTROLLER चलाओ ───────────────────────────────────
    //    तीन conditions एक साथ ज़रूरी हैं:
    //    1. ctrlOn = 1          (UART console से manually enable)
    //    2. run = 1             (RTDS कहता है चलाओ — word 35)
    //    3. ReceiveLength == 37 (valid packet receive हुआ)
    if ((ctrlOn && run && ReceiveLength == RECEIVE_LENGTH) || controllerStopped) {
        controllerStarted = 1;

        if (ctrlCtr % PEBB_RATE == 0) {  // PEBB_RATE=1 → हर cycle में चलता है
            PEBB_Control_step();
            // ↑ Auto-generated PLECS/Simulink controller — एक timestep
            // Reads:  PEBB_Control_U.feedback[] (currents/voltages word 2–34 से)
            // Reads:  P_CMD, Q_CMD, V_CMD (power commands words 9, 27, 20 से)
            // Writes: PEBB_Control_Y.switch_1[] (gate duty cycles)
            // Writes: PEBB_Control_Y.enable, not_fault, precharge, etc.
        }

        ctrlCtr++;
        if (ctrlCtr == CTR_MAX) ctrlCtr = 0; // CTR_MAX=8 → cycle 0–7
    }

    // ── STEP 3: RESPONSE RTDS को वापस TRANSMIT करो ───────────────
    if (TxSend(InstancePtr) != XST_SUCCESS) {
        xil_printf("Transmission of Data failed\n\r");
        return XST_FAILURE;
    }

    // ── STEP 4: STOP होने पर DUMP ──────────────────────────────────
    if (run == 0 && runPrev == 1 && dumpOnStop == 1) {
        dumpFlag = 1; // debug data dump request करो
    }

    // ── STEP 5: FLAGS RESET करो ───────────────────────────────────
    runPrev = run;
    Rx_Done = Tx_Done = 0; // ← next cycle के लिए reset
    return XST_SUCCESS;
}
```

**Variable reference:**

| Variable | मतलब | किसने set किया |
|----------|---------|--------|
| `run` | 1=controller चलाओ, 0=रोक दो | rxData35 (RTDS से Word 35) |
| `ctrlOn` | Manual enable/disable | UART console command |
| `ReceiveLength` | Receive हुए words की count | axiReceive() |
| `controllerStarted` | Controller कम से कम एक बार चल चुका है | runControlCycle() |
| `controllerStopped` | Start होने के बाद run 1→0 हुआ | runControlCycle() |
| `PEBB_Control_step()` | Auto-generated controller का एक step | PLECS/Simulink codegen |
| `ctrlCtr % PEBB_RATE` | Rate divider — हर N cycles में चलाओ | config |
| `Rx_Done = 0` | Next interrupt cycle के लिए flag reset | runControlCycle() end में |

`TxSend()` Axi_IO.c में `axiSend()` call करता है → 23 words TX FIFO में डालता है → RTDS को वापस भेजता है।

---

## Part 5: TX Path — RTDS को Data वापस भेजना

### 5.1 `axiSend()` — TX FIFO में 23 Words लिखना

```c
// Axi_IO.c  Line 702–754
void axiSend(XLlFifo *InstancePtr)
{
    packedInt = packBits(); // 8 binary flags को एक u32 word में pack करो

    for(i = 0; i < TRANSMIT_LENGTH; i++) {  // 23 words

        if (XLlFifo_iTxVacancy(InstancePtr)) { // TX FIFO में space है?

            // सबसे common case: RAM में double → wire पर float
            if (thisAxiType == float_ && thisLocalType == double_) {
                float txWord = unitScale * (float)(*(double*)txData[i]->ptr);
                XLlFifo_TxPutWord(InstancePtr, *(u32*)&txWord);
                // double→float convert, scale, u32 के रूप में reinterpret, TX FIFO में put
            }
        }
    }

    // FIFO को बताओ: "जो कुछ है सब भेज दो (23 words × 4 bytes)"
    XLlFifo_iTxSetLen(InstancePtr, (TRANSMIT_LENGTH * WORD_SIZE));
    // ↑ ये actually send को trigger करता है — TxPutWord नहीं
}
```

TX path RX का **उल्टा** है:
- RAM में `double` values हैं (controller outputs)
- `float` में cast करो, scale factor apply करो (RX scale का उल्टा)
- Float bytes को `u32` के रूप में reinterpret करो, TX FIFO में डालो
- सारे 23 words के बाद: transmission trigger करने के लिए `XLlFifo_iTxSetLen()` call करो

---

### 5.2 `packBits()` — 1 Word में 8 Binary Signals

```c
// Axi_IO.c  Line 770–782
u32 packBits()
{
    u32 pack = 0;
    for (i = 0; i < NUM_PACKED_BITS; i++) {   // 8 bits
        if ((bits[i]->boolPtr != NULL && *(bits[i]->boolPtr) != 0) ||
                (bits[i]->doublePtr != NULL && *(bits[i]->doublePtr) != 0)) {
            pack = ((1 << bits[i]->bitPos) | pack); // bitPos पर bit set करो
        }
    }
    return pack;
}
```

`txData3` में 8 packed bits:

| Bit Position | Signal | Type |
|---|---|---|
| Bit 0 | `lowside_contact` | double |
| Bit 1 | `enable` | double |
| Bit 2 | `highside_contact` | double |
| Bit 3 | `not_fault` | bool |
| Bit 4 | `dummyBoolTx` | bool |
| Bit 5 | `dummyBoolTx` | bool |
| Bit 6 | `dummyBoolTx` | bool |
| Bit 7 | `precharge_contact` | double |

सारे 8 on/off signals एक ही 32-bit word में pack हो जाते हैं → Aurora bandwidth बचती है।

---

### 5.3 CHIL Concept — Data वापस क्यों भेजना ज़रूरी है

**CHIL = Controller Hardware-In-the-Loop**

```
असली World Operation:
  Real Power Grid                 Controller (PEBB)
  (Actual hardware)               (Real hardware)
       │                                │
       │◄──── switch commands ──────────│
       │────── sensor values ──────────►│
  Grid react करता है → नए sensor values naturally follow होते हैं

CHIL Testing:
  RTDS (FAKE simulated grid)      Kria Board (REAL controller code)
       │                                │
       │◄──── 23 words TX ─────────────│  switch commands (duty cycles)
       │────── 37 words RX ───────────►│  sensor values
  RTDS simulate करता है: "इन switch states के साथ, अगली voltages/currents क्या होंगी?"
```

**TX क्यों ज़रूरी है — closed loop:**
```
हर 125 µs में:
RTDS → "i_high=1.5kA, v_dc=11kV, run=1" → Kria
                                              ↓
                                    PEBB_Control_step()
                                    "duty_cycle = 0.75"
                                              ↓
Kria → "switch_1[0]=0.75, enable=1, not_fault=1" → RTDS
                                              ↓
RTDS simulate करता है: "अगर phase A switch 75% ON है..."
"→ अगला i_high = 1.523 kA, v_dc = 10.98 kV"
                                              ↓
RTDS → "i_high=1.523kA, v_dc=10.98kV, ..." → Kria (next cycle)
```

**अगर Kria TX नहीं भेजे:** RTDS के पास simulate करने के लिए switch states नहीं होंगी। वो अगली sensor values calculate नहीं कर पाएगा। Simulation loop freeze हो जाएगा।

> **CHIL का फायदा:** Real controller को simulated power grid के against test करना safe और सस्ता है। गलतियाँ सिर्फ software crash करती हैं — कोई real hardware damage नहीं होता। ये real deployment से पहले किया जाता है।

---

## Part 6: Configuration Parameters

| Parameter | Value | कहाँ defined | Purpose |
|-----------|-------|-----------|---------|
| `RECEIVE_LENGTH` | 37 | `Axi_IO.h` line 23 | RTDS से per RX packet कितने words |
| `TRANSMIT_LENGTH` | 23 | `Axi_IO.h` line 22 | RTDS को per TX packet कितने words |
| `WORD_SIZE` | 4 bytes | `Axi_IO.h` line 21 | 1 word = 32 bits = 4 bytes |
| `UNIT_SCALE` | 1000 | `Axi_IO.h` line 26 | kA/kV → A/V conversion factor |
| `SEQ_MAX` | 1000 | `Axi_IO.h` line 27 | Max RTDS sequence number (wrap होता है) |
| `NUM_PACKED_BITS` | 8 | `Axi_IO.h` line 24 | Packed word में कितने binary flags |
| `CTR_MAX` | 8 | `main.c` line 96 | Controller step counter 0–7 cycle करता है |
| `PEBB_RATE` | 1 | `main.c` line 97 | हर N interrupts में controller चलाओ |
| `COMM_GLITCH_LIMIT` | 10 | `Axi_IO.c` line 59 | Error से पहले max कितने bad packets allow हैं |
| `FIFO_DEV_ID` | HW param | `main.c` line 75 | FIFO hardware base address identifier |

> **RTDS side पर भी same values configure होनी चाहिए:** उसकी Aurora I/O list में exactly 37 RX words और 23 TX words। Mismatch हुई → `axiReceive()` में "Warning: receive Length mismatch"।

---

## Part 7: पूरा Flow Diagram

```
PROGRAM START
     │
     ▼
[SCENE 1] main() → XLlFifoInterruptExample()
     │  setupUART()
     │  XLlFfio_LookupConfig() + XLlFifo_CfgInitialize()
     │  XLlFifo_IntClear(0xffffffff)
     │  SetupInterruptSystem():
     │     → XScuGic_CfgInitialize()
     │     → XScuGic_SetPriorityTriggerType()
     │     → XScuGic_Connect(FifoHandler)   ← CALLBACK REGISTER
     │     → XScuGic_Enable()
     │     → Xil_ExceptionRegisterHandler() + Xil_ExceptionEnable()
     │  XLlFifo_IntEnable(XLLF_INT_ALL_MASK)
     │  setPEBBLRU() + PEBB_Control_initialize() + initPEBB()
     │
     ▼
     while(1) {          ← CPU यहाँ WAIT करता है
         readUART();
         check errors;
     }
     │
     │  ← 125 µs बाद, RTDS fiber से 37 words भेजता है
     │
[SCENE 2] HARDWARE:
     │  Aurora 37 beats receive करता है (Beat 37 पर TDATA + TLAST)
     │  AXI FIFO सारे 37 words store करता है
     │  TLAST → RC flag → wire HIGH → GIC → CPU interrupted
     │
     ▼
[SCENE 3] FifoHandler() — GIC से call होता है
     │  XLlFifo_IntPending() → ISR read → RC flag set!
     │  XLlFifo_IntClear(RC_MASK) → interrupt acknowledge
     │  (अगर Rx_Done अभी भी 1 है तो intOverrunFlag check)
     │  FifoRecvHandler() call करो
     │
     ▼
[SCENE 4] FifoRecvHandler()
     │  axiReceive()          ← FIFO से 37 words read
     │  Rx_Done = 1
     │  setPEBBLRU()
     │  runControlCycle()
     │      ├─ PEBB_Control_step()    ← controller चलता है (अगर run=1, ctrlOn=1)
     │      ├─ TxSend() → axiSend()   ← 23 words RTDS को वापस
     │      └─ Rx_Done = Tx_Done = 0  ← next cycle के लिए reset
     │
     ▼
[SCENE 5] axiReceive() — Axi_IO.c में
     │  XLlFifo_iRxOccupancy() → check FIFO खाली नहीं है
     │  XLlFifo_iRxGetLen() / WORD_SIZE → ReceiveLength
     │  अगर ReceiveLength != 37 → drain + error
     │  Loop i=0 to 36:
     │      XLlFifo_RxGetWord() → RxWord  ← READ PTR +1
     │      thisAxiType  = rxData[i]->axiType
     │      thisLocalType= rxData[i]->localType
     │      unitScale    = rxData[i]->scaleFactor
     │      अगर float है तो NaN check
     │      type convert करो (float→double, u32→u32, etc.)
     │      scale करो (×1000)
     │      *rxData[i]->ptr = result  ← controller variable में store
     │
     ▼
     वापस while(1) में — अगले interrupt का wait (125 µs बाद)
```

---

## Part 8: Key Concepts का Summary

| Concept | समझ |
|---------|-------------|
| **Interrupt** | Hardware का तरीका "CPU, कुछ हुआ है!" बोलने का — CPU current task रोकता है, registered handler चलाता है, फिर वापस आता है |
| **GIC** | General Interrupt Controller — hardware dispatcher जो जानता है कौन से interrupt source के लिए कौन सा callback call करना है |
| **FifoHandler** | Interrupt पर पहला call होने वाला function — interrupt type identify करता है (RC/TC/Error) |
| **RC Flag** | "Receive Complete" — जब full packet (TLAST=1) आता है तो FIFO इसे set करता है |
| **READ PTR** | हर `XLlFifo_RxGetWord()` call पर 1 से आगे बढ़ता है — track करता है FIFO से कितना consume हुआ |
| **axiData struct** | Per-word descriptor: destination pointer, local type, AXI type, scale factor |
| **Type punning** | Same memory bytes को different type के रूप में treat करना (u32 → float without value conversion) |
| **volatile** | Compiler को बताता है: "ये किसी भी time interrupt से change हो सकता है, CPU register में cache मत करना" |
| **UNIT_SCALE = 1000** | RTDS kV/kA भेजता है, controller V/A use करता है — receive पर 1000 से multiply |
| **packBits()** | 8 on/off signals को bit positions (bit 0, bit 1, ... bit 7) use करके 1 integer में pack करता है |
| **.elf file** | सारी C files का compiled binary — Vitis IDE से build, JTAG के through Kria RAM में load |
| **CHIL** | Controller Hardware-In-the-Loop — real controller को simulated power grid के against test करना |
| **RX=37, TX=23** | RTDS कई inputs भेजता है (sensors + commands); Kria सिर्फ controller outputs भेजता है |
| **PEBB_Control_step()** | Auto-generated PLECS/Simulink model — एक control timestep execute करता है |
| **PL बनाम PS** | PL = FPGA logic (Aurora, FIFO — hardware)। PS = Processor (A53 CPU — software) |
| **Memory-mapped registers** | CPU specific memory addresses को read/write करके FPGA hardware को access करता है |

---

## Part 9: Q&A — सारे सवाल के जवाब

---

### Q1: Code files (main.c, Axi_IO.c) कहाँ रहती हैं? Memory में कैसे जाती हैं?

**जवाब:**

पहले आपके development PC पर रहती हैं, फिर compile होकर एक `.elf` binary बन जाती हैं:

```
Windows PC:
  src/main.c + src/Axi_IO.c + src/UART.c + src/codegen.h
         │
         │  Vitis IDE → Build → aarch64-none-elf-gcc cross-compiler
         ▼
  Debug/xllfifo_interrupt_example_1.elf  (666 KB binary)
         │
         │  JTAG cable (या SD card boot)
         ▼
  Kria Board RAM ← यहाँ .elf load होता है
         │
         ▼
  A53 CPU → एक-एक instruction करके main() execute करना शुरू
```

`.c` files खुद कभी "run" नहीं होतीं — वो `.elf` में machine code बन जाती हैं, जिसे CPU directly RAM से execute करता है। Operation के time वो files filesystem पर store नहीं होतीं।

Aurora IP और AXI FIFO **hardware** हैं — FPGA bitstream program होने पर वो configure होते हैं। वो software files हैं ही नहीं।

---

### Q2: Interrupt signal को कौन recognize करता है? Flow क्या है?

**जवाब — पूरी interrupt chain:**

```
HARDWARE (PL)                        SOFTWARE (PS)

AXI FIFO
  │ Word 37 (TLAST=1) आता है
  │ ISR register में RC flag set
  │
  │── electric signal (wire HIGH) ─────────────►  GIC
                                                   (General Interrupt Controller)
                                                    │
                                                    │ "FIFO interrupt आया!
                                                    │  Registered handler: FifoHandler
                                                    │  Notify: A53 CPU"
                                                    │
                                                    ▼
                                                   A53 CPU
                                                    │
                                                    │ "Current task pause (while loop)
                                                    │  State save करो (registers, PC)
                                                    │  Registered handler पर jump"
                                                    │
                                                    ▼
                                               FifoHandler()  ← main.c में
```

- **FIFO hardware (PL):** TLAST detect करता है, RC flag set करता है, interrupt line assert करता है
- **GIC (PS side का hardware):** line receive करता है, registered callback look up करता है, CPU को notify करता है
- **A53 CPU:** interrupt exception receive करता है, state save करता है, `FifoHandler()` पर jump करता है
- **FifoHandler (software):** interrupt type identify करता है, `FifoRecvHandler()` call करता है

---

### Q3: `runControlCycle()` exactly क्या करता है?

**Simple कहानी:** RTDS ने 37 words भेजे। Controller ने पढ़ लिए। अब:

1. क्या `run=1` है? (RTDS कहता है चलाओ?)
2. क्या packet valid 37 words है? (glitched नहीं?)
3. अगर हाँ → `PEBB_Control_step()` एक बार चलाओ (controller का एक timestep)
4. Controller outputs pack करो → TX FIFO → 23 words RTDS को वापस
5. `Rx_Done = Tx_Done = 0` reset करो → next cycle के लिए ready

Controller को actually चलाने के 3 conditions: `ctrlOn=1 AND run=1 AND ReceiveLength==37`।

`PEBB_Control_step()` PLECS/Simulink से auto-generated code है। ये `PEBB_Control_U` (inputs) से read करता है और `PEBB_Control_Y` (outputs) में write करता है। हम `PEBB_Control_U` को `axiReceive()` के through populate करते हैं, फिर `PEBB_Control_Y` को `axiSend()` के through read करते हैं।

---

### Q4: Data कैसे convert और store होता है? (End-to-end Word 2 के लिए)

**Word 2 (i_high — Phase A current) के लिए पूरी pipeline:**

```
STEP 1 — RTDS 1.5 kA भेजता है:
  IEEE 754 float bytes: 0x3FC00000
  ये raw 32-bit word FIFO में store होता है

STEP 2 — axiReceive() इसे process करता है:
  RxWord = XLlFifo_RxGetWord() → 0x3FC00000 (raw u32)

  rxData[1] = &rxData2:
    .ptr         = &PEBB_Control_U.feedback[0]  ← destination
    .axiType     = float_                        ← wire format
    .localType   = double_                       ← storage format
    .scaleFactor = 1000                          ← kA → A

  RxValue = *(float*)&RxWord     → 1.5f   (same bytes, float के रूप में read)
  *rxPtr  = (double)1.5f * 1000  → 1500.0 (Amperes)

STEP 3 — PEBB_Control_U.feedback[0] = 1500.0
  PEBB_Control_step() feedback[0] read करता है
  "Current = 1500A, Reference = 2000A → error = 500A → duty cycle बढ़ाओ"
  PEBB_Control_Y.switch_1[0] = 0.75

STEP 4 — axiSend() 0.75 वापस भेजता है:
  float txWord = (float)0.75 * (1/1000) [kA range में वापस scale]
  XLlFifo_TxPutWord() → TX FIFO → Aurora → Fiber → RTDS
```

एक line में summary:
```
FIFO raw bytes → float के रूप में reinterpret → ×1000 scale → double में store
→ PEBB_Control_step() read करता है → duty cycle compute → axiSend() वापस RTDS को
```

---

### Q5: RX=37 words और TX=23 words अलग क्यों हैं?

ये दो अलग packets हैं अलग purposes के लिए:

| Direction | Words | क्या contain करता है |
|-----------|-------|----------|
| RTDS → Kria (RX) | **37 words** | 3-phase currents (A,B,C), 3-phase voltages, DC voltages, power commands (P, Q, V), contactor commands, power limits, ForceFault, run flag, sequence number, dummy/spare words |
| Kria → RTDS (TX) | **23 words** | 3-phase duty cycles, 8 binary flags (1 word में packed), state (current + next), error flag, counter, sequence number, dummy/spare words |

**RX TX से ज़्यादा क्यों?** RTDS पूरा power system simulate कर रहा है — बहुत सारे sensor readings + command inputs। Kria सिर्फ एक controller है — वो सिर्फ switching decisions और status flags output करता है।

```c
// Axi_IO.h
#define RECEIVE_LENGTH  37   // RTDS → Kria
#define TRANSMIT_LENGTH 23   // Kria → RTDS
```

ये values **दोनों side exactly match** होनी चाहिए। RTDS की Aurora I/O configuration list में 37 TX words और 23 RX words configure होने चाहिए।

---

### Q6: क्या RTDS सारा data एक साथ भेजता है? क्या ये एक file है?

**जवाब:** RTDS per simulation step एक **complete packet** भेजता है — file नहीं, बल्कि एक **snapshot**।

हर ~125 µs (8 kHz) में RTDS एक simulation timestep complete करता है और calculate करता है:
- नई voltages, currents, states = 37 numbers

ये 37 numbers एक साथ pack हो जाते हैं **एक packet = 37 × 4 = 148 bytes** में और एक unit के रूप में भेजे जाते हैं।

```
RTDS simulation timeline:
  t=0:      Step 1 complete → packet 1 sent [v=11.0kV, i=1.5kA, run=1, ...]
  t=125µs:  Step 2 complete → packet 2 sent [v=11.0kV, i=1.52kA, ...]
  t=250µs:  Step 3 complete → packet 3 sent [...]
  ...
```

RTDS packets को **buffer नहीं** करता। ये एक hard real-time system है — एक step, एक packet, हमेशा।

---

### Q7: Aurora एक time पर कितना data convert करता है?

**जवाब:** Aurora IP data को **serial से parallel** convert करता है, per clock cycle एक word (32-bit)। पर software की नज़र से:

- Aurora पूरा 37-word packet serial bits के रूप में fiber पर receive करता है
- वो हर word को एक-एक करके AXI FIFO में convert और forward करता है
- FIFO सारे 37 words accumulate करता है
- जब TLAST आता है → interrupt fire

आप कभी Aurora के साथ directly interact नहीं करते — ये RTDS और FIFO के बीच fully transparent hardware है।

---

### Q8: AXI Stream क्या है और ये क्यों matter करता है?

**जवाब:** AXI-Stream FPGA के अंदर Aurora IP और AXI FIFO के बीच internal communication protocol है।

इसे chip के अंदर **one-way highway** की तरह सोचिए:

```
Aurora IP  ──────────────────────────────────────►  AXI FIFO

हर clock cycle एक "beat" travel करता है:
  Beat 1:  TDATA=word1   TVALID=1  TLAST=0   ← data valid, last नहीं
  Beat 2:  TDATA=word2   TVALID=1  TLAST=0
  ...
  Beat 37: TDATA=word37  TVALID=1  TLAST=1   ← data valid AND ये last है!
```

Beat 37 पर `TLAST=1` ही वो चीज़ है जो FIFO को:
1. Packet को "complete" mark करवाती है
2. RC interrupt flag set करवाती है
3. CPU को signal भेजवाती है

AXI Stream के बिना, Aurora और FIFO को पता ही नहीं चलता FPGA के अंदर कैसे communicate करें।

---

### Q9: FIFO कितना बड़ा है? कितना data hold करता है?

**जवाब:**

| Attribute | Value |
|-----------|-------|
| 1 slot | 1 word = 32 bits = 4 bytes |
| Typical FIFO depth | 512–4096 words |
| 512 depth पर | 512 × 4 = 2 KB total capacity |
| हमारा 1 packet | 37 words = 148 bytes |
| कितने packets fit | 512 depth पर ~13 (पर हम एक time पर 1 ही process करते हैं) |

Practical में, FIFO में हमेशा at most **1 packet (37 words)** ही होता है क्योंकि अगला packet आने (125 µs बाद 8 kHz पर) से पहले हम process और drain कर देते हैं।

हमारे project के लिए actual depth `xparameters.h` में verify की जा सकती है (Vivado/Vitis द्वारा hardware export के time auto-generated)।

---

### Q10: Interrupt exactly कब fire होता है?

**जवाब:** Interrupt precisely तब fire होता है जब complete packet का **last word FIFO में enter** होता है — यानी जब `TLAST=1` वाला AXI Stream beat store होता है।

```
Aurora → FIFO: Beat 1  (TLAST=0)  → word 1 store
Aurora → FIFO: Beat 2  (TLAST=0)  → word 2 store
Aurora → FIFO: Beat 3  (TLAST=0)  → word 3 store
...
Aurora → FIFO: Beat 37 (TLAST=1)  → word 37 store ← INTERRUPT यहाँ FIRE होता है!
```

Timeline:
```
Time ──────────────────────────────────────────────────────►

RTDS transmits:   [pkt 1: 37 words]       [pkt 2: 37 words]
                              │                         │
                              ▼                         ▼
FIFO interrupt:           [INT!]                    [INT!]
                              │                         │
A53 CPU:     [idle]──►[FifoRecvHandler]  [idle]──►[FifoRecvHandler]
                       [37 words read]               [37 words read]
                       [controller चलाया]            [controller चलाया]
                       [TX data भेजा]                [TX data भेजा]
                       [वापस idle]                    [वापस idle]
```

CPU packets के बीच `while(1)` में idle रहता है, फिर हर packet process करने के लिए ~microseconds के लिए जागता है।

---

### Q11: AXI Stream terms में "complete packet" का क्या मतलब है?

**जवाब:** AXI4-Stream protocol में, कोई भी number of words एक "packet" बन सकता है — sender को बस final beat पर `TLAST=1` assert करना होता है।

हमारे RTDS system के लिए specifically:
- Complete = `TLAST=1` receive AND `ReceiveLength == 37`
- Incomplete = `TLAST=1` पर `ReceiveLength != 37` (glitch, partial transmission)

```c
// axiReceive() दोनों conditions validate करता है:
ReceiveLength = (XLlFifo_iRxGetLen(InstancePtr)) / WORD_SIZE;

if (ReceiveLength != RECEIVE_LENGTH) {
    // गलत count — FIFO drain, error log, processing skip
} else {
    // सही count — सारे 37 words process
    for (i = 0; i < RECEIVE_LENGTH; i++) {
        RxWord = XLlFifo_RxGetWord(InstancePtr);
        // ... convert, scale, store
    }
}
```

Sequence number (Word 37, `rxDataSeq`) validation की दूसरी layer देता है:
```c
// Axi_IO.c line 335
axiData rxDataSeq = {
    .ptr = (void*) &seq,
    .axiType = s32_,
    ...
};
// axiReceive() के बाद, अगर seq != seq_prev+1 → "Warning: missing packet"
```

---

*Document समाप्त। Pattern generator explanation के लिए `kr260_aurora_fifo_pattern_generator.md` देखें (अलग document)।*
