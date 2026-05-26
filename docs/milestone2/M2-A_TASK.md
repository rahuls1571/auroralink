# M2-A — AuroraLink protocol & IP-core behavior

**Status:** Not started  
**Deliverable:** [M2-A_auroralink_findings.md](M2-A_auroralink_findings.md) (create when complete)

## Summary

Gain a working understanding of how AuroraLink moves data between FPGA and the A53 application, including whether software **polls** shared memory/FIFOs or is **notified** by hardware events.

## Activities

- [ ] Study **RTDS / CHIL reference code** in `RTDS_Aurora_Link/Kria_CHIL_Firmware/PEBB_CHIL/xllfifo_interrupt_example_1/` (`Axi_IO.c`, `main.c`, FIFO interrupt handlers)
- [ ] Read **RTDS Simulator** AuroraLink documentation (add manual URL to deliverable)
- [ ] Review **Xilinx Aurora** docs (PL link layer vs PS responsibilities)
- [ ] Map **IP-core configuration** (interrupt thresholds, FIFO depth, length registers, AXI addresses, clocks)
- [ ] Use AI-assisted walkthroughs; **cite sources** for every conclusion

## Key questions

1. How does AuroraLink work end-to-end on Kria (remote partner → Aurora IP → fabric → PS)?
2. **Push vs pull:** Does the IP push into PS-visible memory, or does software poll AXI regions? Distinguish **stream push (PL)** vs **CPU read pull (PS)** vs **interrupt notification**.
3. What signals **packet / frame boundaries** to software?
4. What are the **configuration knobs** for the IP core and FIFO bridge?

## Acceptance criteria

- [ ] Deliverable exists (≤ 3 pages)
- [ ] Push/pull answered in one explicit sentence (vocabulary from [BRIDGE_SOFTWARE_DESIGN.md §2.3](../BRIDGE_SOFTWARE_DESIGN.md))
- [ ] Reference path documented: `FifoRecvHandler` → `axiReceive`, `XLlFifo_*`
- [ ] IP configuration checklist table with sources
- [ ] All claims cited (repo path, RTDS doc, or Xilinx PG)

## References

- [BRIDGE_SOFTWARE_DESIGN.md](../BRIDGE_SOFTWARE_DESIGN.md) §2–2.3
- In-repo: `RTDS_Aurora_Link/`
