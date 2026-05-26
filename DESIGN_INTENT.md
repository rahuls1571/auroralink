# Design Intent — EMS Controller Translation Bridge

## Purpose

The purpose of this system is to enable interoperability between controller platforms exposing data over Aurora high-speed serial links and existing EMS infrastructure relying on EGD-based communication.

The bridge provides a bidirectional translation boundary between the Aurora interface and the EGD interface without requiring modification to either side.

---

## System Overview

The bridge is structured into three logical layers:

```text
Aurora Interface
      ↕
Translation Layer
      ↕
EGD Interface
```

* The Aurora interface layer handles Aurora-specific communication concerns.
* The EGD interface layer handles EGD message construction and transmission.
* The translation layer maps and synchronizes data between the two domains.

The intent is to keep protocol-specific logic separated and avoid tightly coupling Aurora behavior to EGD implementation details.

---

## Runtime Dataflow Model

The bridge uses a producer/consumer model to decouple Aurora and EGD communication behavior.

Both interfaces may act as producers or consumers depending on data direction.

```text
Aurora RX
    ↓ writes
Shared Translation State / Buffers
    ↓ reads
EGD TX Cycle

EGD RX
    ↓ writes
Shared Translation State / Buffers
    ↓ reads
Aurora TX
```

This model separates the timing behavior of the two communication domains and prevents one side from blocking the other.

---

## Aurora → EGD Flow

When an Aurora frame is received:

1. validate frame
2. parse frame
3. translate into internal representation
4. write latest valid state into shared buffer
5. update timestamp and validity state

```text
Aurora Frame Received
        ↓
Validate / Parse
        ↓
Translate
        ↓
Write Latest State
```

On each EGD transmission cycle:

1. read latest valid state from shared buffer
2. verify freshness/validity
3. construct EGD message
4. transmit EGD update

```text
EGD Publish Tick
        ↓
Read Latest State
        ↓
Validate Freshness
        ↓
Construct EGD Message
        ↓
Transmit
```

---

## EGD → Aurora Flow

When an EGD update is received:

1. validate EGD message
2. parse/update internal representation
3. write translated state into shared buffer
4. update timestamp and validity state

```text
EGD Message Received
        ↓
Validate / Parse
        ↓
Translate
        ↓
Write Latest State
```

Aurora transmission logic reads the latest valid translated state and constructs Aurora-compatible output frames.

```text
Aurora TX Trigger
        ↓
Read Latest State
        ↓
Construct Aurora Frame
        ↓
Transmit
```

---

## Buffer Design Intent

The initial implementation uses a simple latest-value buffer model.

The shared buffers maintain:

* latest translated state
* timestamp
* validity state

The intent is to prioritize:

* deterministic behavior
* simplicity
* observability

before introducing advanced optimization strategies.

Initial implementation avoids:

* zero-copy optimization
* lock-free structures
* deep queueing mechanisms

unless runtime measurements demonstrate they are necessary.

---

## Buffer State Model

The shared buffer maintains simple state transitions:

```text
EMPTY
VALID
STALE
INVALID
```

Meaning:

* **EMPTY** — no valid data received
* **VALID** — latest data is usable
* **STALE** — data exceeds freshness threshold
* **INVALID** — received frame/message failed validation or translation

Example transitions:

```text
EMPTY  → VALID
VALID  → STALE
VALID  → INVALID
STALE  → VALID
INVALID → VALID
```

---

## Timing Intent

Aurora and EGD operate under different timing models:

| Aurora          | EGD                  |
| --------------- | -------------------- |
| event-driven    | cyclic               |
| streaming       | structured updates   |
| high-throughput | periodic publication |

The bridge absorbs these differences by:

* updating internal state on incoming events
* publishing outgoing messages on interface-specific timing requirements

The bridge maintains the latest valid synchronized state between both domains.

---

## Platform Intent

The Kria platform acts as:

* Aurora integration point
* runtime execution environment
* hardware/software boundary for the bridge application

Platform bring-up includes:

* Kria initialization
* Linux runtime setup
* Aurora IP integration
* runtime validation

---

## Translation Intent

The translation layer is responsible for:

* mapping Aurora-originated data into EGD-compatible structures
* mapping EGD-originated data into Aurora-compatible structures
* maintaining protocol isolation
* handling synchronization between producer and consumer domains

The translation layer operates on an internal canonical representation rather than directly coupling Aurora frames to EGD packet structures.

---

## Design Principles

* Keep protocol concerns separated
* Prefer deterministic behavior over premature optimization
* Decouple Aurora timing from EGD timing
* Keep buffering and synchronization explicit
* Prioritize observability and repeatability
* Maintain predictable degraded-state behavior
* Avoid unnecessary framework complexity

---

## Success Intent

The system is successful when:

* Aurora-originated data is translated into valid EGD messages
* EGD-originated data is translated into valid Aurora-compatible frames
* EMS systems consume translated data without modification
* the bridge operates predictably under normal and degraded conditions
* the implementation remains understandable and maintainable without excessive complexity
