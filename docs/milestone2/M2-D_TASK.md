# M2-D — Bridge application architecture (bidirectional dataflow)

**Status:** Not started  
**Deliverable:** [M2-D_application_design.md](M2-D_application_design.md) (create when complete)

## Summary

Design the **mediator application**: Aurora (AXI/FIFO) ↔ translation ↔ EGD (libEGD), aligned with [DESIGN_INTENT.md](../../DESIGN_INTENT.md).

## Activities

- [ ] Module split: Aurora adapter, translation core, EGD adapter ([BRIDGE_SOFTWARE_DESIGN.md](../BRIDGE_SOFTWARE_DESIGN.md))
- [ ] Compare scheduling: single-thread round-robin vs multi-thread event-driven vs sidecar process
- [ ] Define buffers (latest-value, timestamps, VALID/STALE/INVALID)
- [ ] Per-boundary sync vs async decision table
- [ ] Thread/ISR diagram and failure modes

## Key questions

1. How is **bidirectional** flow scheduled without starving either side?
2. Where are **blocking** vs **non-blocking** operations allowed?
3. **ISR / FIFO handler** vs worker thread boundaries?
4. How to reconcile **event-driven Aurora** with **cyclic EGD**?

## Acceptance criteria

- [ ] Deliverable with architecture diagram (mermaid or ASCII)
- [ ] Explicit threading model recommendation + rationale
- [ ] Data-transfer table per boundary
- [ ] Open risks and M3 implementation tasks listed
- [ ] Consistent with M2-A/B/C findings

## Depends on

Draft findings from **M2-A**, **M2-B**, and constraints from **M2-C**.

## References

- [DESIGN_INTENT.md](../../DESIGN_INTENT.md)
- [BRIDGE_SOFTWARE_DESIGN.md](../BRIDGE_SOFTWARE_DESIGN.md)
