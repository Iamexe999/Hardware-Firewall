# AXI4-Lite Hardware Firewall / Bus Monitor

A simulation-first hardware-security project in SystemVerilog. The firewall sits between an AXI4-Lite master and a peripheral and enforces address-region read/write permissions. No FPGA board is needed to run the verification suite.

## Implemented

- 32-bit address and data buses; four configurable regions.
- Default deny after reset; lowest-index matching region takes priority.
- Independent read and write paths, one outstanding transaction per direction.
- Separate buffering of write address and data (AW-first, W-first, or simultaneous).
- Blocked requests receive DECERR (`2'b11`), with zero read data and no downstream transaction.
- Allowed transactions preserve data, byte strobes, protection attributes, and downstream response codes.
- Trusted configuration interface, idle-only policy updates, and lock until reset.
- Saturating violation counter, last denied address/type, sticky interrupt.
- Backpressure assertions, C++ simulated master/peripheral, randomized timing, and VCD waveforms.

This is a working RTL prototype with simulation validation. It has not been formally proven, synthesized, timing-closed, or tested on an FPGA.

## Operating systems and requirements

The simulation needs Verilator 5.x, GNU Make, and a C++ compiler (GCC or Apple Clang). GTKWave is optional: the tests and VCD generation work without a graphical desktop or waveform viewer. No FPGA, vendor toolchain, Python test framework, or SystemC application is required.

| Operating system | Setup route | Project validation status |
| --- | --- | --- |
| CachyOS / Arch Linux | pacman packages | Instructions provided; not separately tested on this distribution |
| Ubuntu / Debian / Linux Mint | apt packages | Instructions provided; not separately tested on each distribution |
| Fedora | dnf packages | Instructions provided; not separately tested |
| Windows 10/11 | Ubuntu under WSL2 | Instructions provided; not tested on Windows |
| macOS (Apple Silicon or Intel) | Xcode Command Line Tools + Homebrew | Instructions provided; not tested on macOS |

The project regression was executed in a Linux development environment using the Verilator Python distribution 5.48.0 and GNU C++. Platform tool availability does not constitute a passing project test on every OS; run `make test` locally. The source uses C++17; newer Verilator releases can additionally require a newer compiler for their own runtime.

### CachyOS / Arch Linux

```sh
sudo pacman -Syu --needed verilator gcc make gtkwave unzip
```

If your system is already fully updated, `sudo pacman -S --needed verilator gcc make gtkwave unzip` also installs the dependencies. Avoid refreshing only the package database with `pacman -Sy` and then installing into an otherwise outdated system.

### Ubuntu / Debian / Linux Mint

```sh
sudo apt update
sudo apt install verilator build-essential gtkwave unzip
```

Check `verilator --version`. Older distribution releases may ship Verilator 4.x. Use a current distribution release or follow the [upstream installation guide](https://verilator.org/guide/latest/install.html) for a newer version. On Ubuntu, if the package is unavailable, check whether the Universe repository is enabled.

### Fedora

```sh
sudo dnf install verilator gcc-c++ make gtkwave unzip
```

These instructions target regular Fedora installations using DNF, not an immutable host image such as Silverblue. Use an appropriate development container on an immutable edition.

### Windows 10/11 using WSL2

Use a Windows release supported by Microsoft's [WSL installation instructions](https://learn.microsoft.com/en-us/windows/wsl/install). In **PowerShell as Administrator**, run:

```powershell
wsl --install -d Ubuntu
```

Restart if requested, open Ubuntu, and create the Linux user account. Check the installed distribution with `wsl -l -v` in PowerShell; use WSL version 2. All subsequent build commands run in the **Ubuntu terminal**, not PowerShell or Command Prompt:

```sh
sudo apt update
sudo apt install verilator build-essential gtkwave unzip
mkdir -p ~/projects
cd ~/projects
```

Copy the downloaded ZIP into `~/projects`. For example, substitute your Windows username and actual Downloads location:

```sh
cp "/mnt/c/Users/YOUR_WINDOWS_USERNAME/Downloads/hardware-firewall.zip" .
```

Continue with the shared build steps below. Keeping the project in the Linux home directory avoids Windows filesystem performance and path issues. A Linux account password is separate from the Windows password.

GTKWave can use [WSL GUI application support](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps) on supported systems. If a GUI is unavailable, `make test` and `make waves` still work; open the generated VCD in a separately installed Windows waveform viewer. Native MSVC/MSYS2 builds are not covered by this project's Makefile instructions.

### macOS

Install Apple's command-line developer tools, if not already installed:

```sh
xcode-select --install
```

Complete the installation dialog before continuing. Install [Homebrew](https://brew.sh/) using its official instructions, including its shell PATH setup, then run:

```sh
brew install verilator make
```

Use **`gmake`** in place of `make` for this guide on macOS. Homebrew installs GNU Make under that name. Apple's Clang supplies the C++ compiler; installing a separate GCC package is unnecessary.

```sh
unzip hardware-firewall.zip
cd hardware-firewall
gmake test
gmake waves
```

For optional waveform viewing, follow the current [GTKWave macOS installation guide](https://gtkwave.github.io/gtkwave/install/mac.html). The viewer may need additional setup on recent macOS releases; it is not needed to build or test the firewall. Once installed, open `build/firewall.vcd` from the viewer, or use `gtkwave build/firewall.vcd` if the command is on your PATH.

## Build and run (Linux and WSL2)

From the directory containing the downloaded archive:

```sh
unzip hardware-firewall.zip
cd hardware-firewall
verilator --version
make --version
c++ --version
make test
```

Use a project path without spaces because the current Makefile passes source paths to the build tools without shell quoting. If you already extracted the project, enter that folder directly.

`make test` runs warning-fatal lint, compiles the RTL and C++ testbench, enables assertions, and runs seeds 1, 42, and 2026. Expect three `PASS` lines, each reporting 2,085 transaction scenarios. A scenario may contain both a read and a write. These are 6,255 scenarios total, not a formal proof or coverage percentage.

```sh
make lint                         # RTL lint only
make waves                        # Run seed 1 and generate build/firewall.vcd
gtkwave build/firewall.vcd         # Optional viewer
./build/Vaxi_lite_firewall 12345    # Run another seed
make clean                        # Remove generated build outputs
```

On macOS, replace `make` with `gmake`. Override Verilator if needed: `make VERILATOR=/absolute/path/to/verilator test`.

In GTKWave, add `clk`, `s_awvalid`, `s_awready`, `s_wvalid`, `s_wready`, `m_awvalid`, `m_wvalid`, `s_bvalid`, `s_bresp`, and `violation_count`. The opening transaction is denied: watch DECERR and the counter increment without a downstream write. A transfer occurs on a rising edge where VALID and READY are both high.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| `verilator: command not found` | Install it, reopen the shell if PATH changed, then run `verilator --version`. |
| `c++: command not found` | Install `build-essential`, `gcc-c++`, or the compiler package for your OS; on macOS complete the Xcode tools installation. |
| `make: command not found` | Install Make; use `gmake` for Homebrew GNU Make. |
| C++ compilation errors after changing toolchains | Run `make clean`, check compiler/Verilator versions, and rebuild. |
| GTKWave reports no display | Run tests without the viewer, or configure desktop/WSLg support and open the VCD afterward. |
| `build/firewall.vcd` is missing | Run `make waves`; `make test` alone does not create a waveform. |
| Unsupported SystemVerilog assertion syntax | Check for an old Verilator installation; use Verilator 5.x and rebuild cleanly. |

## Files

| File | Purpose |
| --- | --- |
| `rtl/axi_lite_firewall.sv` | Firewall, policy registers, logging, and assertions |
| `sim/test_firewall.cpp` | Master drivers, peripheral memory model, scoreboard, tests |
| `Makefile` | Lint, build, tests, waveform generation |
| `docs/ARCHITECTURE.md` | Interface contract, transaction flow, security assumptions |
| `docs/VALIDATION.md` | Executed checks and verification limits |

## First experiment

Read `test_boundaries_and_channel_order()` in `sim/test_firewall.cpp`. Region 0 denies `0x1080–0x108F`, while region 1 permits reads/writes to `0x1000–0x10FF`. Region 0 wins the overlap. Follow writes to `0x107C`, `0x1080`, and `0x1090` in the waveform, then explain why only the middle address is blocked.

Next, change region 2 from read-only to read/write and rerun. The scoreboard computes the expected permission from the configured policy. Finally, intentionally break one RTL permission check and confirm that the testbench detects an unauthorized downstream transfer; restore the RTL afterward.

## Sources

Protocol reference: [Arm AMBA AXI and ACE specification, IHI 0022H](https://documentation-service.arm.com/static/5f915bbcf86e16515cdc3b23), especially AXI channel handshakes and AXI4-Lite. This project is an original implementation, not Arm compliance certification.

Tools: [Verilator installation guide](https://verilator.org/guide/latest/install.html), [Arch Verilator package](https://archlinux.org/packages/extra/x86_64/verilator/), [Arch GTKWave package](https://archlinux.org/packages/extra/x86_64/gtkwave/).

Additional platform references: [Verilator platform FAQ](https://verilator.org/guide/latest/faq.html), [Fedora Verilator](https://packages.fedoraproject.org/pkgs/verilator/verilator/), [Fedora GTKWave](https://packages.fedoraproject.org/pkgs/gtkwave/gtkwave/), [Homebrew Verilator](https://formulae.brew.sh/formula/verilator), and [GTKWave Linux installation](https://gtkwave.github.io/gtkwave/install/unix_linux.html).
