# M2-B — libEGD protocol & API usage

**Status:** Not started  
**Deliverable:** [M2-B_libegd_findings.md](M2-B_libegd_findings.md) (create when complete)

## Summary

Understand how **EGD over UDP** is sent and received via **libEGD**: which functions to call for TX/RX, typical payload sizes, and sync vs async behavior.

## Activities

- [ ] Deep-read `libEGD-master/` (README, examples, client code)
- [ ] Compare with `EgdSend.c` / `EgdTest.h` (PDU type 13, port **18246**, **1400** byte cap in-tree)
- [ ] Trace one **publish** and one **subscribe** path (application call → socket I/O)
- [ ] Document **JsonClient** vs **EgdClient** (default JsonClient unless profiling says otherwise)
- [ ] Incorporate official **EGD specification** when available

## Key questions

1. How are messages **encoded** for production and **decoded** on consumption?
2. Which functions **send** data — what happens inside?
3. Which functions **receive** data — callback, block, or poll?
4. **Typical EGD data size** (exchange `DataLength`, UDP limits, library caps)?
5. **Sync vs async** — blocking calls, internal threads, or event loop?

## Acceptance criteria

- [ ] Deliverable exists (≤ 3 pages)
- [ ] API cheat sheet: function → TX/RX → sync model → notes
- [ ] Call-flow diagram or numbered steps for publish and subscribe
- [ ] Payload size limits with sources
- [ ] JsonClient vs EgdClient recommendation for this bridge

## References

- `libEGD-master/README.md`, `src/examples/`
- `EgdSend.c`, `EgdTest.h`
