# Validation results

Executed in the development environment with Verilator Python package 5.48.0, GNU C++, and Make. Verilator's own version banner reports `rev vUNKNOWN-built20260516-4e853d8`; the package version is recorded separately for reproducibility.

`make test` passed warning-fatal `-Wall` lint, compilation, all enabled assertions, and three regression seeds:

| Seed | Transaction scenarios | Clock cycles | Downstream writes | Downstream reads |
| --- | ---: | ---: | ---: | ---: |
| 1 | 2,085 | 75,262 | 384 | 563 |
| 42 | 2,085 | 75,703 | 397 | 548 |
| 2026 | 2,085 | 75,999 | 384 | 576 |

A scenario can contain a read, write, or concurrent read and write. Counts are scenarios, not individual bus transfers. Each run contains 2,000 randomized scenarios plus directed scenarios; setup/configuration/reset-only checks are not included in that count.

## Executed checks

- Default deny after reset, including reset clearing previously locked permissions.
- AW before W, W before AW, and simultaneous AW/W.
- Independent randomized downstream AWREADY/WREADY/ARREADY.
- Randomized response latency and upstream BREADY/RREADY backpressure.
- Concurrent read/write activity and simultaneous read/write violations.
- Read-only, write-only, disabled, reversed-range, and explicit deny regions.
- Lowest-index overlap priority.
- Region boundaries, full-word containment, unaligned rejection, top-of-address-space arithmetic.
- All 16 byte-strobe values; subsequent readback.
- Preservation of AWPROT/ARPROT, addresses, data, and strobes downstream.
- Propagation of an injected downstream SLVERR.
- Denied reads return zero data.
- Denials produce no downstream request handshakes; allowed requests produce exactly one expected handshake per channel.
- Violation increments, sticky IRQ, single-event metadata, write priority on simultaneous denials.
- Configuration rejected during a buffered write address and after lock.
- Reset with an unmatched AW, an unmatched W, and stalled denied B/R responses.
- Assertions for stable payload/VALID under stalls on downstream AW/W/AR and upstream B/R, plus lock persistence.
- `make waves` generates a nonempty VCD and completes the seed-1 regression.

## Limits of this evidence

This is simulation testing, not a proof of all legal AXI behaviors or official protocol compliance. Random seeds are reproducible, but they are not functional coverage metrics. The suite assumes a protocol-compliant source and target. It does not inject arbitrary unknown/X values, exhaustively check counter saturation, or test every reset timing against a physically separate target. No formal verification, synthesis, place-and-route, hardware timing, or board testing was performed.

The counter saturation logic is present in RTL but the tests do not generate billions of violations or force the counter near overflow. Reset during downstream partial transactions requires the coordinated-reset integration contract documented in ARCHITECTURE.md.
