# 🎬 Complete Scene-by-Scene Code Explanation
## PEBB CHIL Firmware — main.c + Axi_IO.c

---

> [!NOTE]
> Think of the whole system as a **Railway Station**:
> - **RTDS** = Train (sends data every 125 microseconds)
> - **Aurora fiber link** = Railway track
> - **AXI FIFO** = Platform (data waits here in a queue)
> - **Interrupt** = Station bell (rings when train fully arrives)
> - **CPU (A53)** = Station Master (processes the train after the bell)
> - **main.c** = Station Master's instruction book
> - **Axi_IO.c** = Goods manifest (what goes in/out and where)

---

## 🏗️ Scene 1 — SETUP (Program Starts — Runs Only ONCE)

### What happens here?
When the Kria board boots up and your `.elf` file starts running, `main()` is called.
The job in Scene 1 is: **Get everything ready before any data arrives.**

> [!NOTE]
> **`.elf` file kya hai?**
> `.elf` = Executable and Linkable Format — tumhara compiled C code (main.c + Axi_IO.c + sab files) ek single binary mein.
> Vitis IDE compile karta hai → `Debug/xllfifo_interrupt_example_1.elf` banta hai (666 KB).
> Ye JTAG cable se Kria ki RAM mein load hota hai. CPU phir `main()` se start karta hai.
>
> ```
> main.c  ──┐
> Axi_IO.c ─┤
> UART.c   ─┤──► Vitis IDE (cross-compile) ──► xllfifo_interrupt_example_1.elf
> codegen.c ┘                                   ↓
>                                        JTAG cable se Kria RAM mein
>                                               ↓
>                                        A53 CPU → main() → Scene 1 start!
> ```

---

### Step 1A — `main()` calls `setupUART()`

```c
// main.c  Line 177–184
int main()
{
    int Status;

    // setup UART (must be done before any prints)
    if (setupUART() == XST_FAILURE) {
        return XST_FAILURE;
    }

    xil_printf("--- Entering main() ---\n\r");
```

**Plain English:**
- UART = Serial port = Console terminal (like a keyboard/screen connection)
- `setupUART()` configures it so we can print debug messages
- Without this, `xil_printf()` would crash or not work
- Think of it like: **"Turn on the intercom system before announcing anything."**

---

### Step 1B — `main()` calls `XLlFifoInterruptExample()`

```c
// main.c  Line 188–193
while (1) {
    Status = XLlFifoInterruptExample(&FifoInstance, FIFO_DEV_ID);

    if (Status != XST_SUCCESS) break;
}
```

**Plain English:**
- This calls the BIG setup function
- `&FifoInstance` = a pointer to our FIFO hardware object (defined at Line 132)
- `FIFO_DEV_ID` = hardware ID number — tells the driver WHICH FIFO chip to use
- The `while(1)` wraps it so if FIFO fails+returns, it tries again (but normally runs forever inside)

---

### Step 1C — Inside `XLlFifoInterruptExample()` — Hardware Initialization

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

**Plain English:**
- `XLlFfio_LookupConfig(DeviceId)` → Looks up the hardware address table for this FIFO
  - Think: "What is the memory address of the FIFO chip registers?"
  - Returns a `Config` struct with base address, etc.
- `XLlFifo_CfgInitialize(...)` → Uses that config to initialize/reset the FIFO hardware
  - Resets all registers to default
  - Maps the physical hardware address so software can talk to it

> [!IMPORTANT]
> The FIFO hardware is in the **PL (FPGA) side**. The CPU accesses it through **memory-mapped registers**.
> That means reading/writing to a specific memory address IS reading/writing to the FIFO hardware!

---

### Step 1D — Clear Old Interrupts

```c
// main.c  Line 261–269
Status = XLlFifo_Status(InstancePtr);
XLlFifo_IntClear(InstancePtr, 0xffffffff);  // clear ALL interrupt flags
Status = XLlFifo_Status(InstancePtr);
if(Status != 0x0) {
    xil_printf("\n ERROR : Reset value of ISR0 : 0x%x\t"
               "Expected : 0x0\n\r",
               XLlFifo_Status(InstancePtr));
    return XST_FAILURE;
}
```

**Plain English:**
- Before we start, we clear any stale interrupt flags left over from a previous run
- `0xffffffff` = all ones in binary = clears EVERY interrupt flag at once
- Then we check: "Is the status register now zero (clean)?" If not → error

> [!NOTE]
> Think of this as: **"Before opening the station, make sure no old bells are still ringing."**

---

### Step 1E — Setup Interrupt System ← **THE MOST IMPORTANT STEP**

```c
// main.c  Line 275–279
Status = SetupInterruptSystem(&Intc, InstancePtr, FIFO_INTR_ID);
if (Status != XST_SUCCESS) {
    xil_printf("Failed intr setup\r\n");
    return XST_FAILURE;
}
```

This calls `SetupInterruptSystem()` which does 4 things internally:

#### Sub-step i — Initialize the GIC (Interrupt Controller)
```c
// main.c  Line 601–610  (inside SetupInterruptSystem)
IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
// Find the GIC hardware address

Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
                IntcConfig->CpuBaseAddress);
// Initialize the GIC driver
```

- GIC = **General Interrupt Controller** — the hardware chip that manages all interrupts
- This finds the GIC's memory address and initializes it
- Think: **"Wake up the station bell manager and get him ready"**

#### Sub-step ii — Set Priority for FIFO interrupt
```c
// main.c  Line 612
XScuGic_SetPriorityTriggerType(IntcInstancePtr, FifoIntrId, 0xA0, 0x3);
```

- `0xA0` = priority level (lower number = higher priority)
- `0x3` = rising-edge triggered (interrupt fires on voltage going LOW→HIGH)
- Think: **"Tell the bell manager: FIFO bell is important, listen for it going HIGH"**

#### Sub-step iii — Register `FifoHandler` as the callback ← **KEY LINE**
```c
// main.c  Line 619–621
Status = XScuGic_Connect(IntcInstancePtr, FifoIntrId,
                (Xil_InterruptHandler)FifoHandler,
                InstancePtr);
```

- This tells the GIC: **"When FIFO interrupt fires → call `FifoHandler()` function"**
- `FifoHandler` is a **function pointer** — the address of our handler function
- `InstancePtr` = the FIFO instance pointer, passed as argument to FifoHandler when called
- Think: **"Tell the bell manager: when the FIFO bell rings, call THIS person (FifoHandler)"**

#### Sub-step iv — Enable the interrupt
```c
// main.c  Line 626
XScuGic_Enable(IntcInstancePtr, FifoIntrId);
```

- Without this, the GIC would know about the interrupt but silently ignore it
- Think: **"Turn on the FIFO bell in the bell panel"**

#### Sub-step v — Register with CPU exception table
```c
// main.c  Line 637–644
Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
    (Xil_ExceptionHandler)INTC_HANDLER,
    (void *)IntcInstancePtr);

Xil_ExceptionEnable();
```

- The A53 CPU has an **exception vector table** — a list of "what to do when X happens"
- We tell the CPU: "When ANY interrupt comes in, call `INTC_HANDLER` (the GIC handler)"
- The GIC then figures out WHICH interrupt and calls the right callback
- Think: **"Tell the Station Master: if ANY bell rings, check with the bell manager first"**

---

### Step 1F — Enable FIFO Interrupts

```c
// main.c  Line 281
XLlFifo_IntEnable(InstancePtr, XLLF_INT_ALL_MASK);
```

- This enables ALL types of FIFO interrupts (TX complete, RX complete, errors)
- `XLLF_INT_ALL_MASK` = bitmask with all bits set
- Think: **"Tell the FIFO hardware: you are now allowed to ring any bell"**

---

### Step 1G — Initialize Controller + Enter the Waiting Loop

```c
// main.c  Line 284–294
setPEBBLRU();                    // initialize LRU count
PEBB_Control_initialize(initTime); // initialize the controller model
initPEBB();                      // initialize PEBB hardware

xil_printf("Paused. Please Start RTDS model now. \n\r");

while(1) {  // ← CPU SITS HERE WAITING
    // background tasks (UART read, error printing, etc.)
    readUART();
    // ...
}
```

**Plain English:**
- `PEBB_Control_initialize()` = Initialize the PLECS/Simulink generated control model
- `initPEBB()` = Initialize hardware (gate drivers, contactors, etc.)
- After setup → CPU enters **infinite loop** (`while(1)`)
- In this loop, the CPU is just doing small background tasks (checking UART, printing errors)
- **The real work happens in interrupts!** The CPU is interrupted whenever data arrives

> [!IMPORTANT]
> **Scene 1 Summary:** Program starts → UART setup → FIFO hardware init → GIC registers `FifoHandler` → interrupts enabled → controller init → CPU enters infinite wait loop.
> **After this, the CPU just waits. Everything else is triggered by the FIFO interrupt.**

---

## ⚡ Scene 2 — RTDS DATA ARRIVES — INTERRUPT FIRES!

### What happens here?
RTDS simulator sends a 37-word packet every **125 microseconds** (8 kHz).
The packet travels: RTDS → fiber cable → Aurora IP (FPGA) → AXI Stream → AXI FIFO (in PL).

When the **last word** of the packet arrives (TLAST signal = 1), the FIFO hardware:
1. Sets the **RC (Receive Complete) flag** in its interrupt register
2. Sends an **electric signal** (wire goes HIGH) to the GIC

```
┌─────────────────────────────────────────────────────────────────┐
│  HARDWARE SIDE (PL - FPGA)          SOFTWARE SIDE (PS - CPU)   │
│                                                                 │
│  RTDS sends 37 words                                            │
│       ↓                                                         │
│  Aurora IP receives via fiber                                   │
│       ↓                                                         │
│  AXI FIFO stores words [1..37]                                  │
│       ↓                                                         │
│  Word 37 arrives with TLAST=1                                   │
│       ↓                                                         │
│  FIFO sets RC flag in ISR register                              │
│       ↓                                                         │
│  Electric signal → wire goes HIGH ──────────────────► GIC      │
│                                                         ↓       │
│                                                     GIC sees    │
│                                                     FIFO intr   │
│                                                         ↓       │
│                                              CPU interrupted!   │
│                                              (while loop paused)│
│                                                         ↓       │
│                                              FifoHandler()      │
│                                              is called!         │
└─────────────────────────────────────────────────────────────────┘
```

**Plain English:**
- The CPU was sitting in `while(1)` doing `readUART()`
- Suddenly the GIC taps the CPU on the shoulder: **"FIFO interrupt! Go run FifoHandler!"**
- The CPU saves its current state (registers, program counter) and jumps to `FifoHandler()`
- This happens **automatically by hardware** — no polling, no checking in the loop

> [!NOTE]
> This is the whole point of interrupts: **the CPU doesn't waste time checking "has data arrived yet?"**
> The hardware tells the CPU **exactly when** to act. Very efficient for real-time systems!

---

## 🔔 Scene 3 — `FifoHandler()` — Which Bell Rang?

### What happens here?
`FifoHandler()` is called by the GIC. It needs to figure out **WHAT TYPE** of interrupt fired.
The FIFO can generate multiple types of interrupts:
- **RC** = Receive Complete (data arrived) ← the one we care about
- **TC** = Transmit Complete (our data was sent)
- **Error** = Something went wrong

```c
// main.c  Line 428–456
static void FifoHandler(XLlFifo *InstancePtr)
{
    u32 Pending;

    // Step 1: Ask the FIFO: "Which interrupt flag is set?"
    Pending = XLlFifo_IntPending(InstancePtr);

    while (Pending) {   // loop in case multiple flags are set at once

        if (Pending & XLLF_INT_RC_MASK) {
            // ← RC flag is set → Data received!
            XLlFifo_IntClear(InstancePtr, XLLF_INT_RC_MASK); // acknowledge/clear it

            if (Rx_Done) {
                intOverrunFlag = 1; // WARNING: previous cycle not done yet!
            }
            FifoRecvHandler(InstancePtr); // ← Go handle the received data
        }
        else if (Pending & XLLF_INT_TC_MASK) {
            // ← TC flag → Transmit complete
            XLlFifo_IntClear(InstancePtr, XLLF_INT_TC_MASK);
            if (Tx_Done) {
                intOverrunFlag = 1;
            }
            FifoSendHandler(InstancePtr); // ← Handle TX complete
        }
        else if (Pending & XLLF_INT_ERROR_MASK) {
            // ← Error happened
            FifoErrorHandler(InstancePtr, Pending);
            XLlFifo_IntClear(InstancePtr, XLLF_INT_ERROR_MASK);
        }
        else {
            XLlFifo_IntClear(InstancePtr, Pending); // clear unknown flags
        }

        // Check again — more flags pending?
        Pending = XLlFifo_IntPending(InstancePtr);
    }
}
```

### Breaking it down line by line:

| Code | What it does | Analogy |
|------|-------------|---------|
| `XLlFifo_IntPending()` | Read the ISR (Interrupt Status Register) of FIFO | Station master reads the "which bell rang" board |
| `Pending & XLLF_INT_RC_MASK` | Bitwise AND to check if RC bit is set | "Is the RX bell specifically ringing?" |
| `XLlFifo_IntClear(...RC_MASK)` | Write 1 to RC bit in ISR to clear/acknowledge it | "Acknowledge the bell so it stops ringing" |
| `if (Rx_Done) intOverrunFlag=1` | Previous data wasn't processed yet — timing issue! | "Previous train's goods still on platform!" |
| `FifoRecvHandler(InstancePtr)` | Call the receive handler | "Go unload this train's goods" |

> [!IMPORTANT]
> **Why must we clear the interrupt flag BEFORE processing?**
> If we don't clear it, the FIFO will keep generating the interrupt over and over.
> It's like: the bell keeps ringing until you press the "OK, I heard it" button.

> [!WARNING]
> **`intOverrunFlag`** is set when `Rx_Done` is still `1` (meaning the PREVIOUS cycle's data
> hasn't been fully processed) but a NEW interrupt fires. This means the system is running
> slower than the 8 kHz data rate — a **timing overrun** problem!

---

## 📦 Scene 4 — `FifoRecvHandler()` — Trigger the Data Processing

### What happens here?
This is a short function that orchestrates three things:

```c
// main.c  Line 470–480
static void FifoRecvHandler(XLlFifo *InstancePtr)
{
    // 1. Read all 37 words from FIFO into variables
    axiReceive(InstancePtr);   // ← Goes to Axi_IO.c !

    // 2. Mark that receive is done
    Rx_Done = 1;

    // 3. Update how many LRUs are connected (based on received data)
    setPEBBLRU();

    // 4. Run the control algorithm + transmit response back
    if (runControlCycle(InstancePtr) == XST_FAILURE) {
        CHIL_error |= 1; // set error flag
    }
}
```

### Breaking it down:

#### `axiReceive(InstancePtr)` — The Big One
- This is in **Axi_IO.c** — it actually reads the 37 words from FIFO
- We explain this in Scene 5 in full detail

#### `Rx_Done = 1`
```c
volatile int Rx_Done;   // Line 121 in main.c
```
- `volatile` means: "This variable can change at any time (by interrupt), don't optimize it away"
- Setting to `1` signals: "receive cycle is done"
- Checked in `while(1)` loop: `if (Error && Rx_Done) { ... }`
- Reset to `0` at end of `runControlCycle()`

#### `setPEBBLRU()`
- LRU = Line Replaceable Unit (a hardware module)
- Based on data received from RTDS, updates how many LRUs are active

#### `runControlCycle(InstancePtr)` — The Controller + TX

```c
// main.c  Line 354–387
int runControlCycle(XLlFifo *InstancePtr)
{
    // --- STOP COMMAND CHECK ---
    if (!run && controllerStarted) {
        controllerStopped = 1;
        PEBB_Control_U.ForceFault = 1; // put in fault/freewheeling mode
    }

    // --- RUN CONTROLLER ---
    if ((ctrlOn && run && ReceiveLength == RECEIVE_LENGTH) || controllerStopped) {
        controllerStarted = 1;

        if (ctrlCtr % PEBB_RATE == 0) {  // PEBB_RATE = 1, so runs every cycle
            PEBB_Control_step();  // ← Run one step of the PLECS controller model
        }

        ctrlCtr++;
        if (ctrlCtr == CTR_MAX) { // CTR_MAX = 8
            ctrlCtr = 0;
        }
    }

    // --- TRANSMIT RESPONSE BACK ---
    if (TxSend(InstancePtr) != XST_SUCCESS) {
        xil_printf("Transmission of Data failed\n\r");
        return XST_FAILURE;
    }

    // --- DUMP ON STOP ---
    if (run == 0 && runPrev == 1 && dumpOnStop == 1) {
        dumpFlag = 1; // request a debug data dump
    }

    runPrev = run;
    Rx_Done = Tx_Done = 0; // ← Reset flags for next cycle
    return XST_SUCCESS;
}
```

**Plain English breakdown:**

| Variable | Meaning |
|----------|---------|
| `run` | 1 = RTDS told us to run the controller |
| `ctrlOn` | 1 = manually enabled from UART console |
| `ReceiveLength == RECEIVE_LENGTH` | Correct number of words received (37) |
| `controllerStarted` | 1 once controller has run at least once |
| `controllerStopped` | 1 if run went 1→0 after starting |
| `PEBB_Control_step()` | One timestep of the auto-generated PEBB controller |
| `ctrlCtr % PEBB_RATE` | Rate divider — run controller every N interrupts |

> [!NOTE]
> `TxSend()` calls `axiSend()` in Axi_IO.c, which puts 23 words into the FIFO TX buffer
> and tells the FIFO: "Send these now!" This is how data goes BACK to RTDS.

---

## 📖 Scene 5 — `axiReceive()` — Reading All 37 Words (READ PTR Moves Here!)

This is the **heart of Axi_IO.c**. Let's go line by line.

```c
// Axi_IO.c  Line 604–684
void axiReceive(XLlFifo *InstancePtr)
{
    int i;
    u32 RxWord;
    seq_prev = seq;  // save previous sequence number for checking

    // ─────────────────────────────────────────
    // STEP 1: Check if FIFO has data
    // ─────────────────────────────────────────
    if (XLlFifo_iRxOccupancy(InstancePtr)) {
        // "How many words are in RX FIFO?" > 0 → yes, has data

        // ─────────────────────────────────────────
        // STEP 2: Read packet length
        // ─────────────────────────────────────────
        ReceiveLength = (XLlFifo_iRxGetLen(InstancePtr)) / WORD_SIZE;
        // XLlFifo_iRxGetLen() returns length in BYTES
        // WORD_SIZE = 4 bytes
        // So ReceiveLength = number of 32-bit words
        // Expected: 37 words

        // ─────────────────────────────────────────
        // STEP 3: Handle WRONG SIZE packet
        // ─────────────────────────────────────────
        if (ReceiveLength != RECEIVE_LENGTH) {
            // Wrong size! Drain the FIFO but don't use the data
            for (i = 0; i < ReceiveLength; i++) {
                RxWord = XLlFifo_RxGetWord(InstancePtr); // drain (READ PTR moves!)
            }
            seq = 0; // invalidate sequence number
            xil_printf("Warning: receive Length mismatch...");
            commGlitchCtr++;
            if (commGlitchCtr > COMM_GLITCH_LIMIT) {
                CHIL_error |= 1; // raise comm error after 10 glitches
            }

        // ─────────────────────────────────────────
        // STEP 4: Handle CORRECT SIZE packet
        // ─────────────────────────────────────────
        } else {
            type_ thisAxiType;
            type_ thisLocalType;
            float unitScale;
            float RxValue;
            int isNan;

            for (i = 0; i < RECEIVE_LENGTH; i++) {  // i = 0 to 36
                // ← READ WORD i FROM FIFO (READ PTR ADVANCES BY 1 HERE!)
                RxWord = XLlFifo_RxGetWord(InstancePtr);

                // Look up what type this word is and where to store it
                thisAxiType  = rxData[i]->axiType;    // float_? s32_? u32_?
                thisLocalType= rxData[i]->localType;   // double_? s32_?
                unitScale    = rxData[i]->scaleFactor;  // 1? 1000?

                isNan = 0;

                // Check for NaN (Not-a-Number) — corrupted float data
                if (thisAxiType == float_) {
                    RxValue = (*(float *) &RxWord);
                    if (RxValue != RxValue) { // NaN check: NaN != NaN is always true!
                        isNan = 1;
                    }
                }

                if (isNan) {
                    // Bad data — log error but don't store
                    CHIL_error |= 1;
                    NanRxCtr++;
                    if (printNaNCtr == 1) {
                        xil_printf("Error: NaN received for Aurora word 0d%d.", 1+i);
                    }

                // Case 1: Same type on both sides, no scaling needed — direct copy
                } else if ((thisAxiType == u32_ || thisAxiType == s32_ || thisAxiType == float_)
                        && thisLocalType == thisAxiType && unitScale == 1) {
                    u32* rxPtr = (u32 *) rxData[i]->ptr;
                    *rxPtr = RxWord;  // copy raw 32-bit word directly

                // Case 2: float on wire → float in RAM, but needs scaling
                } else if (thisAxiType == float_ && thisLocalType == float_) {
                    float* rxPtr = (float*) rxData[i]->ptr;
                    RxValue = (*(float *) &RxWord);
                    *rxPtr = RxValue * unitScale;

                // Case 3: float on wire → double in RAM (most common case!)
                } else if (thisAxiType == float_ && thisLocalType == double_) {
                    double* rxPtr = (double*) rxData[i]->ptr;
                    RxValue = (*(float *) &RxWord); // reinterpret bytes as float
                    *rxPtr = (double) RxValue * unitScale; // cast to double, scale
                }
            }
        }

    } else {
        xil_printf("Error: RX interrupt with no RX FIFO Occupancy");
        CHIL_error |= 1;
    }
}
```

### Let's understand HOW READ PTR moves:

```
FIFO Memory (before axiReceive):
┌──────┬──────┬──────┬──────┬──────┬──────┐
│Word 1│Word 2│Word 3│ ...  │Word36│Word37│
└──────┴──────┴──────┴──────┴──────┴──────┘
↑                                          ↑
READ PTR (=0)                         WRITE PTR (=37)

After XLlFifo_RxGetWord() call #1:
READ PTR moves to 1 → Word 1 is consumed

After XLlFifo_RxGetWord() call #2:
READ PTR moves to 2 → Word 2 is consumed

... 37 calls later ...

READ PTR = 37, WRITE PTR = 37
→ FIFO is empty!
```

**Why does `XLlFifo_RxGetWord()` move the READ PTR?**
This is a Xilinx library function. Internally it:
1. Reads the data from the FIFO's RX data register
2. The FIFO hardware automatically advances the read pointer
3. You never have to manually update the pointer

---

### The `rxData[]` Array — "The Address Book"

Now let's understand where each word goes. In Axi_IO.c there are 37 `axiData` structs:

```c
// Axi_IO.h  Line 40–45
typedef struct {
    void* ptr;          // WHERE to store this data (memory address)
    type_ localType;    // HOW it's stored locally (double, s32, etc.)
    type_ axiType;      // HOW it comes in on the AXI bus (float, s32, etc.)
    float scaleFactor;  // MULTIPLY by this after receiving
} axiData;
```

**Example — Word 2 (high-side current, i_high):**
```c
// Axi_IO.c  Line 87–92
axiData rxData2 = {
    .ptr = (void*) &PEBB_Control_U.feedback[0],  // store here
    .axiType = float_,     // comes in as 32-bit IEEE float
    .localType = double_,  // store as 64-bit double
    .scaleFactor = UNIT_SCALE  // multiply by 1000 (kA → A)
};
```

**Walkthrough for Word 2:**
```
FIFO Word 2 (raw bytes) = 0x3F800000 (= 1.0 in IEEE 754 float)
          ↓
XLlFifo_RxGetWord() → RxWord = 0x3F800000
          ↓
thisAxiType = float_, thisLocalType = double_
          ↓
RxValue = *(float*) &RxWord = 1.0f  (reinterpret bytes as float)
          ↓
*rxPtr = (double) 1.0f * 1000.0 = 1000.0  (scale kA → A)
          ↓
PEBB_Control_U.feedback[0] = 1000.0  ✓
```

**RTDS sends in kA/kV, but the controller expects A/V — that's what `UNIT_SCALE = 1000` is for!**

---

### All 37 RX Words — What Goes Where

```c
// Axi_IO.c  Line 573–578
volatile axiData *rxData[RECEIVE_LENGTH] = {
    &rxData1,  // Word 1:  key (read/ignore key from RTDS)
    &rxData2,  // Word 2:  feedback[0] = i_high phase A current
    &rxData3,  // Word 3:  feedback[4] = vhigh B phase AC voltage
    &rxData4,  // Word 4:  feedback[3] = vhigh A phase AC voltage
    &rxData5,  // Word 5:  feedback[7] = v_dcc DC capacitor voltage
    &rxData6,  // Word 6:  feedback[6] = i_low current
    &rxData7,  // Word 7:  dummyDoubleRx (unused)
    &rxData8,  // Word 8:  PEBB_NEXT_STATE_CMD
    &rxData9,  // Word 9:  P_CMD (power command in MW→kW)
    &rxData10, // Word 10: LOWSIDE_CONTACTOR_CMD
    &rxData11, // Word 11: PEBB_POWER_LOWER_LIMIT (MW²→W)
    &rxData12, // Word 12: PEBB_POWER_UPPER_LIMIT
    &rxData13, // Word 13: dummy
    &rxData14, // Word 14: dummy
    &rxData15, // Word 15: ctr (substep counter)
    &rxData16, // Word 16: dummy
    &rxData17, // Word 17: dummy
    &rxData18, // Word 18: dummy
    &rxData19, // Word 19: dummy (was PEBB curr_ff, now unused)
    &rxData20, // Word 20: V_CMD (voltage command)
    &rxData21, // Word 21: ForceFault
    &rxData22, // Word 22: dummy
    &rxData23, // Word 23: dummy
    &rxData24, // Word 24: dummy
    &rxData25, // Word 25: dummy
    &rxData26, // Word 26: EN_GRID_SUPPORT
    &rxData27, // Word 27: Q_CMD (reactive power command in MVA→kVA)
    &rxData28, // Word 28: Q_LIMIT
    &rxData29, // Word 29: feedback[1] = i_high_B
    &rxData30, // Word 30: feedback[2] = i_high_C
    &rxData31, // Word 31: feedback[5] = v_high_c
    &rxData32, // Word 32: feedback[8] = v_stator_A
    &rxData33, // Word 33: feedback[9] = v_stator_B
    &rxData34, // Word 34: feedback[10] = v_stator_C
    &rxData35, // Word 35: run (1=run controller, 0=stop)
    &rxData36, // Word 36: RXI_chil (close loop flag)
    &rxDataSeq // Word 37: seq (sequence number for packet verification)
};
```

---

### Type Conversion — The Tricky Bit

The most confusing line in `axiReceive`:

```c
RxValue = (*(float *) &RxWord);
```

**What does this mean step by step?**

```
RxWord is u32 (unsigned 32-bit integer)
e.g. RxWord = 0x3F800000

&RxWord        → memory address of RxWord (u32*)
(float *)&RxWord → treat that same address as if it points to a float
*(float *)&RxWord → read the bytes at that address AS a float
= 1.0f  (because 0x3F800000 is IEEE 754 representation of 1.0)
```

This is called **"type punning"** — we don't convert the value, we just reinterpret the raw bytes.
RTDS sends floats as their raw 32-bit IEEE 754 bit pattern, so we read them back the same way.

---

## 📤 Bonus: What `axiSend()` Does (TX back to RTDS)

After `axiReceive()` and `PEBB_Control_step()`, the controller has computed outputs.
`axiSend()` packs 23 words and sends them back:

```c
// Axi_IO.c  Line 702–754
void axiSend(XLlFifo *InstancePtr)
{
    packedInt = packBits(); // pack 8 binary flags into one u32 word

    for(i=0; i < TRANSMIT_LENGTH; i++) {  // 23 words
        if (XLlFifo_iTxVacancy(InstancePtr)) { // is there space in TX FIFO?

            // Case: double → float (most common)
            if (thisAxiType == float_ && thisLocalType == double_) {
                float txWord = unitScale * (float)(*(double*)txData[i]->ptr);
                XLlFifo_TxPutWord(InstancePtr, *(u32*)&txWord);
                // ↑ convert double to float, scale, reinterpret as u32, put in TX FIFO
            }
        }
    }

    // Tell FIFO: "Send everything you have (23 words × 4 bytes)"
    XLlFifo_iTxSetLen(InstancePtr, (TRANSMIT_LENGTH * WORD_SIZE));
}
```

### The `packBits()` function — 8 binary signals in 1 word

```c
// Axi_IO.c  Line 770–782
u32 packBits()
{
    u32 pack = 0;
    for (i = 0; i < NUM_PACKED_BITS; i++) {   // 8 bits
        if ((bits[i]->boolPtr != NULL && *(bits[i]->boolPtr) != 0) ||
                (bits[i]->doublePtr != NULL && *(bits[i]->doublePtr) != 0)) {
            pack = ((1 << bits[i]->bitPos) | pack);
            // set bit at position bitPos in pack
        }
    }
    return pack;
}
```

**Example — the 8 packed bits in `txData3`:**

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

All 8 signals packed into ONE 32-bit word → saves Aurora bandwidth!

---

## 🗺️ Complete Flow — All 5 Scenes Together

```
PROGRAM START
     │
     ▼
[SCENE 1] main() → XLlFifoInterruptExample()
     │  Initialize FIFO hardware
     │  Initialize GIC
     │  Register FifoHandler with GIC
     │  Enable FIFO interrupts
     │  Initialize PEBB controller
     │
     ▼
     while(1) {          ← CPU waits here
         readUART();
         check errors;
     }
     │
     │  ← 125µs later, RTDS sends 37 words
     │
[SCENE 2] HARDWARE: FIFO full → TLAST → RC flag → GIC fires → CPU interrupted
     │
     ▼
[SCENE 3] FifoHandler() called by GIC
     │  Read ISR register: which flag?
     │  RC flag set → clear it → call FifoRecvHandler()
     │
     ▼
[SCENE 4] FifoRecvHandler()
     │  call axiReceive()
     │  set Rx_Done=1
     │  call runControlCycle()
     │      └─ PEBB_Control_step() ← controller runs
     │      └─ TxSend() → axiSend() ← 23 words back to RTDS
     │      └─ Rx_Done = Tx_Done = 0
     │
     ▼
[SCENE 5] axiReceive() in Axi_IO.c
     │  Check FIFO occupancy
     │  Read packet length (37 words)
     │  Loop 37 times:
     │      XLlFifo_RxGetWord() → READ PTR +1
     │      Look up rxData[i] → type + destination address
     │      Convert type if needed (float→double, scale ×1000)
     │      Store in correct variable
     │
     ▼
     Back to while(1) — wait for next interrupt
```

---

## 🔑 Key Concepts Summary

| Concept | Simple Explanation |
|---------|-------------------|
| **Interrupt** | Hardware way of saying "hey CPU, something happened!" — CPU stops what it's doing and runs the handler |
| **GIC** | The "dispatcher" — knows which handler to call for which interrupt |
| **FifoHandler** | First function called on interrupt — figures out what type it is |
| **RC Flag** | "Receive Complete" — FIFO sets this when a full packet arrives |
| **READ PTR** | Moves forward every time `XLlFifo_RxGetWord()` is called — tracks how much of FIFO was read |
| **axiData struct** | Blueprint for each word: where to store it, what type, what scale factor |
| **Type punning** | Treating the same bytes as a different type (u32 bytes read as float) |
| **volatile** | Tells compiler: "this can change any time (by interrupt), don't cache it in a register" |
| **UNIT_SCALE = 1000** | RTDS sends in kV/kA, controller uses V/A, so multiply by 1000 |
| **packBits()** | Squeezes 8 on/off signals into 1 integer (bit 0, bit 1, ... bit 7) |
| **.elf file** | Compiled binary of all C files — Vitis banaata hai, JTAG se Kria mein jaata hai |
| **CHIL** | Controller Hardware-In-the-Loop — real controller + fake (simulated) power system |
| **RX = 37, TX = 23** | RTDS bhejta hai zyada data (sensors + commands), Kria bhejta hai sirf controller outputs |
| **PEBB_Control_step()** | Auto-generated PLECS/Simulink controller — ek timestep chalaata hai |
| **runControlCycle()** | Read data → run controller → send response → reset flags |

---

## ❓ Q&A — Frequently Asked Questions

---

### Q1: `.elf` file kahan se aati hai?

```
Tumhare PC pe source files:
├── src/main.c
├── src/Axi_IO.c
├── src/UART.c
└── src/codegen.h
         │
         │  Vitis IDE → Build Project
         │  (arm cross-compiler: aarch64-none-elf-gcc)
         ▼
Debug/xllfifo_interrupt_example_1.elf   ← 666 KB binary
         │
         │  JTAG cable se Kria Board pe
         │  (ya: jtag_boot.tcl script se)
         ▼
Kria RAM mein load → CPU main() se start karta hai
```

**ELF = Executable and Linkable Format** — Linux/embedded ka `.exe` jaisa samjho.
Windows pe `.exe` hota hai, Kria (Linux ARM) pe `.elf` hota hai.

---

### Q2: `runControlCycle()` exactly kya karta hai?

**Simple story:** RTDS ne 37 words bheje. Controller ne padhey. Ab:
1. Kya run = 1 hai? (RTDS ne chalne bola?)
2. Kya 37 words sahi aayi? (valid packet?)
3. Agar haan → controller ek timestep chalao
4. Controller ke output pakdo → RTDS ko wapas bhejo
5. Flags reset karo → next cycle ke liye ready

```c
// main.c Line 354–387
int runControlCycle(XLlFifo *InstancePtr)
{
    // ── STEP 1: STOP CHECK ───────────────────────────────────
    if (!run && controllerStarted) {
        // RTDS ne run=0 bheja after running → STOP!
        controllerStopped = 1;
        PEBB_Control_U.ForceFault = 1; // safe freewheeling mode
    }

    // ── STEP 2: CONTROLLER CHALAO ────────────────────────────
    //    3 conditions zaroori hain:
    //    1. ctrlOn = 1     (UART se manually enabled)
    //    2. run = 1        (RTDS ne chalane bola — word 35)
    //    3. ReceiveLength == 37 (valid packet size)
    if ((ctrlOn && run && ReceiveLength == RECEIVE_LENGTH) || controllerStopped) {
        controllerStarted = 1;

        if (ctrlCtr % PEBB_RATE == 0) { // PEBB_RATE=1 → har baar
            PEBB_Control_step();
            // ↑ PLECS se auto-generated controller
            // Reads:  PEBB_Control_U.feedback[] (currents, voltages)
            // Reads:  P_CMD, Q_CMD, V_CMD (power commands)
            // Writes: PEBB_Control_Y.switch_1[] (duty cycles)
            // Writes: PEBB_Control_Y.enable, not_fault, etc.
        }

        ctrlCtr++;
        if (ctrlCtr == CTR_MAX) ctrlCtr = 0; // CTR_MAX=8, 0–7 cycle
    }

    // ── STEP 3: RTDS KO WAPAS BHEJO ─────────────────────────
    TxSend(InstancePtr);  // → axiSend() → 23 words TX FIFO mein

    // ── STEP 4: FLAGS RESET ──────────────────────────────────
    Rx_Done = Tx_Done = 0;  // next cycle ke liye ready
    return XST_SUCCESS;
}
```

**Variable meanings:**

| Variable | Kya hai | Kahan set hota hai |
|---|---|---|
| `run` | 1=chalao, 0=roko | rxData35 → Word 35 from RTDS |
| `ctrlOn` | Manual enable/disable | UART console se |
| `ReceiveLength` | Aaye hue words ki count | axiReceive() mein |
| `controllerStarted` | Pehli baar chala? | runControlCycle mein |
| `controllerStopped` | run 1→0 hua? | runControlCycle mein |
| `PEBB_Control_step()` | Controller ek step | PLECS auto-generated |
| `Rx_Done = 0` | Reset for next cycle | runControlCycle end |

---

### Q3: Data convert karke KAHAN store hota hai, aur phir kya use karta hai?

**Poora pipeline — Word 2 (i_high, phase A current) ke liye:**

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
STEP 1 — RTDS ne kya bheja?
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
RTDS sends: 1.5 kA (kilo-Ampere)
IEEE 754 float bytes: 0x3FC00000
FIFO mein ye raw 32-bit word store hai

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
STEP 2 — axiReceive() mein kya hota hai?
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
RxWord = XLlFifo_RxGetWord()   → 0x3FC00000 (raw u32)

rxData[1] = &rxData2
  .ptr         = &PEBB_Control_U.feedback[0]  ← YAHAN STORE KARO
  .axiType     = float_                        ← wire pe float
  .localType   = double_                       ← RAM mein double chahiye
  .scaleFactor = 1000                          ← kA → A

RxValue = *(float*)&RxWord          → 1.5f   (bytes ko float maano)
double* rxPtr = &PEBB_Control_U.feedback[0]
*rxPtr = (double)1.5f * 1000        → 1500.0  (Amperes!)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
STEP 3 — Variable ab kahan use hota hai?
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
PEBB_Control_U.feedback[0] = 1500.0  ← stored here!
         │
         ▼
PEBB_Control_step() reads feedback[0]
"Current = 1500A, Reference = 2000A"
"Error = 500A → duty cycle increase karo"
         │
         ▼
PEBB_Control_Y.switch_1[0] = 0.75   ← output (75% duty)
         │
         ▼
axiSend() → txData1 → Word 1 of TX packet → RTDS ko bhejo
```

**One-liner chain:**
```
FIFO raw bytes
  → reinterpret as float
  → scale ×1000
  → store as double in PEBB_Control_U.feedback[]
  → PEBB_Control_step() reads it
  → controller computes switch duty cycles
  → axiSend() sends duty cycles back to RTDS
```

---

### Q4: RX = 37 words, TX = 23 words — different kyun?

Dono **alag packets** hain, alag lengths ke saath. Ye RTDS ke Aurora I/O configuration se decide hota hai.

```
┌──────────────────────────────────┬──────────────────────────────────┐
│  RTDS → Kria  (RX = 37 words)   │  Kria → RTDS  (TX = 23 words)   │
├──────────────────────────────────┼──────────────────────────────────┤
│ 3-phase currents (A,B,C)         │ 3-phase duty cycles              │
│ 3-phase voltages                 │ 8 binary flags (packed in 1 word)│
│ DC voltages                      │ State (current + next)           │
│ Power commands (P, Q, V)         │ Error flag (CHIL_error)          │
│ Contactor commands               │ Counter (ctr)                    │
│ Power limits                     │ Sequence number                  │
│ ForceFault flag                  │ Dummy/spare words                │
│ run flag (word 35)               │                                  │
│ Sequence number (word 37)        │                                  │
│ Many dummy/spare words           │                                  │
├──────────────────────────────────┼──────────────────────────────────┤
│ RTDS = complex power system      │ Kria = just controller outputs   │
│ → many sensor readings + cmds    │ → only switching decisions       │
└──────────────────────────────────┴──────────────────────────────────┘
```

```c
// Axi_IO.h
#define RECEIVE_LENGTH  37   // RTDS → Kria
#define TRANSMIT_LENGTH 23   // Kria → RTDS
```

> [!IMPORTANT]
> RTDS side pe bhi **exactly 37 RX aur 23 TX** configure hona chahiye Aurora I/O list mein.
> Mismatch hone pe `axiReceive()` "Warning: receive Length mismatch" print karega.

---

### Q5: Hum RTDS ko data kyun bhejte hain? (CHIL concept)

**Ye CHIL = Controller Hardware-In-the-Loop testing hai.**

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Real World mein kya hota:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Real Power Grid          Controller (PEBB)
  (Actual hardware)        (Real hardware)
       │                         │
       │◄── switch commands ─────│
       │──── sensor values ─────►│

Controller real switches ON/OFF karta hai
Grid react karta hai → naye sensor values

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CHIL Testing mein kya hota:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  RTDS                     Kria Board
  (FAKE power grid          (REAL controller
   in software!)             hardware!)
       │                         │
       │◄── 23 words TX ─────────│  switch commands
       │──── 37 words RX ───────►│  sensor values

RTDS = Power system software simulation
Kria = Real PEBB controller running real code
```

**Kyun zaroori hai TX:**

```
RTDS sochta hai:
  "Main ek power grid hoon.
   Mujhe batao — tumne switches kaise lagaye?
   Tab main calculate karoonga:
   'Is switching state mein current/voltage kya hogi?'"
              │
              │ Kria bhejta hai: switch_1[0] = 0.75 (75% duty)
              ▼
  RTDS simulate karta hai:
  "Agar phase A switch 75% ON hai to...
   naya i_high_A = 523A hoga
   naya v_dc = 10980V hoga"
              │
              │ RTDS next cycle mein ye naye values bhejta hai
              ▼
  Kria ko updated sensor data milta hai → next control step
```

**Agar Kria TX na kare:**
- RTDS ko pata nahi switches kaise hain
- RTDS simulate nahi kar sakta aage ka state
- **Loop toot jaata — simulation freeze!**

```
Every 125 microseconds (8000 baar per second):

    RTDS                              KRIA
      │                                │
      │─── 37 words: sensor data ─────►│ axiReceive()
      │    (currents, voltages,         │    ↓
      │     commands, run=1)            │ PEBB_Control_step()
      │                                │    ↓ (compute)
      │                                │ switch_1[0] = 0.75
      │◄── 23 words: outputs ──────────│ axiSend()
      │    (duty cycles, states,        │
      │     error flags)                │
      │    (simulate next state...)     │
      │─── 37 words: NEW sensor data ──►│ (next cycle)
      │    (updated i=523A, v=10980V)   │
```

**Ye loop 8000 baar per second chalta hai — real-time control!**

> [!NOTE]
> **CHIL ka fayda:** Real power grid pe test karna dangerous aur expensive hai.
> CHIL mein RTDS ek safe simulation hai — galti hone pe sirf software crash hota hai,
> koi actual hardware damage nahi! Isliye real deployment se pehle CHIL testing ki jaati hai.
