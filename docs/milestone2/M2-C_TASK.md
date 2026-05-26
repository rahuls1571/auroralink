# M2-C — Cross-compile libEGD for Cortex-A53 (Kria PS)

**Status:** Not started  
**Deliverable:** [M2-C_cross_compile.md](M2-C_cross_compile.md) (create when complete)

## Summary

Establish a repeatable build of **libEGD** for **ARM64 Linux** on the Zynq UltraScale+ **A53** (Ubuntu on Kria) — not R5 or FPGA.

## Activities

- [ ] Inventory build system (`CMakeLists.txt`, Docker builder, `hack/docker/Dockerfile.builder`)
- [ ] Choose approach: cross-compile (`aarch64-linux-gnu`), native on Kria, or BSP/SDK
- [ ] Document sysroot, linker flags, packages for Ubuntu on Kria
- [ ] Build at least one example binary and run on A53
- [ ] Document install layout (`libegd.so`, headers, pkg-config)

## Acceptance criteria

- [ ] Deliverable with exact commands and toolchain versions
- [ ] Artifact path or install prefix documented
- [ ] Smoke test on A53 (command + expected output)
- [ ] Known gaps / blockers listed

## Depends on

Rough picture from **M2-B** (can start once build files are identified).

## References

- `libEGD-master/CMakeLists.txt`, `Makefile`, Docker files
