# Architecture and integration contract

## Placement and trust boundary

Connect the untrusted initiator to the `s_*` interface, and the protected target to `m_*`. All access paths to the protected target must pass through the firewall. A trusted boot controller drives `cfg_*`; the monitored master must not control those pins. The violation outputs connect to a trusted status block or interrupt controller.

This module uses one clock and an active-low **synchronous reset**. Reset must be asserted across a rising edge. Reset the master, firewall, and target together, or otherwise quiesce/isolate the bus: resetting only the firewall after a downstream write address has transferred can leave an orphan transaction in the target. Reset cannot undo completed writes.

## Policy

Each region has a 32-bit inclusive base, 32-bit inclusive limit, enable bit, read permission, and write permission. Scanning starts at index zero. The first enabled region containing the starting address determines access; permission failure does not fall through to a later region. This permits explicit deny overlays.

The v1 policy supports aligned 32-bit words only. An address must be divisible by four and its complete word (`address` through `address+3`) must fit inside the winning region. The addition is widened to 33 bits to avoid wraparound at the top of the address space. Unaligned accesses return DECERR. Partial writes still require whole-word permission, regardless of WSTRB. A zero-strobe write is forwarded only if its address is permitted. A reversed base/limit matches nothing.

PROT bits pass through unchanged but are not used to authorize requests. No identity or privilege claims are inferred from them. Per-master identity enforcement, bursts, full AXI4 IDs, cache coherency, and clock-domain crossing are outside v1.

## Trusted configuration interface

| Signal | Meaning |
| --- | --- |
| `cfg_valid` / `cfg_ready` | A configuration write commits on a rising edge when both are high |
| `cfg_index[1:0]` | Region index 0 through 3 |
| `cfg_base`, `cfg_limit` | Inclusive bounds |
| `cfg_enable`, `cfg_read`, `cfg_write` | Region enable and permissions |
| `cfg_lock` | Commit this region and permanently lock all configuration until reset |
| `locked` | Lock state |

Hold all configuration fields stable until the handshake. `cfg_ready` is high only when unlocked and both transaction paths are empty, including pending responses and unmatched write address/data. While `cfg_valid` is asserted, new upstream AXI requests are held off; already accepted requests continue. Deassert `cfg_valid` after a handshake. Do not hold it high after locking: the trusted configuration requester can otherwise stall new traffic. There is no AXI CSR interface in this version; a SoC integration can wrap these pins in a trusted register block.

Configure regions before releasing untrusted masters, and lock the final write. Updates are atomic per region, not atomic across all four regions. For a multi-region update, keep masters quiescent until all updates are complete. A master that stops halfway through a write can prevent idle-only reconfiguration.

## Write transaction

1. Capture AW address/PROT and compute authorization at the AW handshake.
2. Capture W data/strobes independently, even when W arrives before AW.
3. Wait for both buffers. A denied write generates one upstream DECERR and one violation; neither downstream AW nor W is asserted.
4. An allowed write asserts downstream AWVALID and WVALID independently. Each remains asserted with stable payload until its own READY handshake.
5. After both handshakes, accept the target's B response and store it upstream until BREADY.
6. Only then accept another write. This conservative implementation inserts bubbles rather than maximizing throughput.

An authorized address cannot be reinterpreted under a new policy while its data is pending, because configuration cannot commit while either write buffer is occupied.

## Read transaction

At AR handshake, compute authorization. A denied read generates zero data and DECERR locally. An allowed read buffers address/PROT, issues AR, captures the downstream R response, and holds it until RREADY. Reads and writes can progress concurrently, but the firewall does not impose ordering between the independent paths. Software needing cross-direction ordering must wait for completion appropriately.

## Monitoring

`violation_count` counts denied transactions (a write counts only once both AW and W have arrived). Simultaneous denied reads and writes add two. The 32-bit counter saturates at `0xFFFFFFFF`. `violation_irq` is a level asserted whenever the count is nonzero. Only reset clears the count/IRQ. There is no event FIFO or software clear register.

`last_violation_addr` and `last_violation_write` record the most recent denial; write wins if both happen in one clock. These outputs update together, but a future software-readable wrapper should provide a coherent snapshot if it reads them over multiple cycles. Allowed requests returning downstream SLVERR/DECERR are propagated and do not count as policy violations.

## Threat model and limitations

The attacker may issue any addresses, strobes, and legal AXI timing on the upstream interface. The trusted policy controller, reset, clock, and target are assumed correctly integrated. The design enforces region permissions; it does not prevent denial of service from an incomplete write, a stalled target, or an initiator holding response READY low. There is no timeout or cancellation machinery. Adding a timeout safely requires accounting for late downstream responses and partially completed writes.

Malformed protocol behavior, physical fault injection, side channels, and silicon implementation defects are outside this prototype's protection claims. The RTL is written in synthesizable SystemVerilog; the assertion block is excluded when `SYNTHESIS` is defined. Area, clock frequency, power, and synthesis compatibility have not been measured.
