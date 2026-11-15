#include "Vaxi_lite_firewall.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

namespace {
constexpr unsigned kOkay = 0;
constexpr unsigned kSlaveError = 2;
constexpr unsigned kDecodeError = 3;
constexpr uint32_t kErrorAddress = 0x10fc;
constexpr int kTransactionTimeout = 500;
constexpr unsigned kRandomScenarios = 2000;

unsigned peripheral_response(uint32_t address) {
  return address == kErrorAddress ? kSlaveError : kOkay;
}
unsigned expected_response(bool permitted, uint32_t address) {
  return permitted ? peripheral_response(address) : kDecodeError;
}

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(std::string("line ") +                          \
                               std::to_string(__LINE__) + ": " #x);            \
  } while (0)
struct TransactionTiming {
  int write_address_delay = 0;
  int write_data_delay = 0;
  int read_address_delay = 0;
  int response_stall = 0;
  unsigned write_strobes = 15;
};

struct RegionPolicy {
  uint32_t base = 0, limit = 0;
  bool enabled = false, read = false, write = false;
};
struct FirewallSimulation {
  Vaxi_lite_firewall dut;
  std::mt19937 random;
  std::unique_ptr<VerilatedVcdC> waveform;
  uint64_t trace_time = 0, cycle_count = 0;
  std::array<RegionPolicy, 4> regions{};
  std::array<uint32_t, 1024> memory{};
  bool pending_write_address = false, pending_write_data = false,
       write_response_pending = false, read_response_pending = false;
  uint32_t pending_address = 0, pending_data = 0, read_response_data = 0;
  unsigned pending_strobes = 0, write_response = 0, read_response = 0,
           write_response_delay = 0, read_response_delay = 0;
  unsigned downstream_write_addresses = 0, downstream_write_data_beats = 0,
           downstream_read_addresses = 0, scenario_count = 0;
  bool write_address_accepted = false, write_data_accepted = false,
       read_address_accepted = false, write_response_accepted = false,
       read_response_accepted = false;
  unsigned observed_write_response = 0, observed_read_response = 0;
  uint32_t observed_read_data = 0;
  bool expect_write = false, expect_read = false;
  uint32_t expected_write_address = 0, expected_write_data = 0,
           expected_read_address = 0;
  unsigned expected_write_strobes = 0, expected_write_protection = 0,
           expected_read_protection = 0;
  FirewallSimulation(unsigned seed, bool waves) : random(seed) {
    if (waves) {
      Verilated::traceEverOn(true);
      waveform = std::make_unique<VerilatedVcdC>();
      dut.trace(waveform.get(), 99);
      waveform->open("build/firewall.vcd");
    }
    reset();
  }
  ~FirewallSimulation() {
    dut.final();
    if (waveform)
      waveform->close();
  }
  void evaluate() {
    dut.eval();
    if (waveform)
      waveform->dump(trace_time++);
  }
  void drive_peripheral_inputs() {
    dut.m_awready = !pending_write_address && !write_response_pending &&
                    (random() % 3 != 0);
    dut.m_wready =
        !pending_write_data && !write_response_pending && (random() % 3 != 0);
    dut.m_arready = !read_response_pending && (random() % 3 != 0);
    dut.m_bvalid = write_response_pending && write_response_delay == 0;
    dut.m_bresp = write_response;
    dut.m_rvalid = read_response_pending && read_response_delay == 0;
    dut.m_rresp = read_response;
    dut.m_rdata = read_response_data;
  }
  void capture_upstream_handshakes() {
    write_address_accepted = dut.s_awvalid && dut.s_awready;
    write_data_accepted = dut.s_wvalid && dut.s_wready;
    read_address_accepted = dut.s_arvalid && dut.s_arready;
    write_response_accepted = dut.s_bvalid && dut.s_bready;
    read_response_accepted = dut.s_rvalid && dut.s_rready;
    observed_write_response = dut.s_bresp;
    observed_read_response = dut.s_rresp;
    observed_read_data = dut.s_rdata;
  }
  void update_peripheral() {
    bool accept_write_address = dut.m_awvalid && dut.m_awready,
         accept_write_data = dut.m_wvalid && dut.m_wready,
         accept_read_address = dut.m_arvalid && dut.m_arready;
    bool accept_write_response = dut.m_bvalid && dut.m_bready,
         accept_read_response = dut.m_rvalid && dut.m_rready;
    if (dut.rst_n) {
      if (write_response_delay)
        write_response_delay--;
      if (read_response_delay)
        read_response_delay--;
      if (accept_write_address) {
        CHECK(expect_write);
        CHECK(dut.m_awaddr == expected_write_address);
        CHECK(dut.m_awprot == expected_write_protection);
        CHECK(!pending_write_address);
        pending_write_address = true;
        pending_address = dut.m_awaddr;
        downstream_write_addresses++;
      }
      if (accept_write_data) {
        CHECK(expect_write);
        CHECK(dut.m_wdata == expected_write_data);
        CHECK(dut.m_wstrb == expected_write_strobes);
        CHECK(!pending_write_data);
        pending_write_data = true;
        pending_data = dut.m_wdata;
        pending_strobes = dut.m_wstrb;
        downstream_write_data_beats++;
      }
      if (accept_write_response)
        write_response_pending = false;
      if (accept_read_response)
        read_response_pending = false;
      if (pending_write_address && pending_write_data &&
          !write_response_pending) {
        write_response = peripheral_response(pending_address);
        if (write_response == kOkay)
          for (unsigned i = 0; i < 4; i++)
            if (pending_strobes & (1u << i))
              memory[(pending_address >> 2) % memory.size()] =
                  (memory[(pending_address >> 2) % memory.size()] &
                   ~(0xffu << (8 * i))) |
                  (pending_data & (0xffu << (8 * i)));
        write_response_pending = true;
        write_response_delay = random() % 12;
        pending_write_address = pending_write_data = false;
      }
      if (accept_read_address) {
        CHECK(expect_read);
        CHECK(dut.m_araddr == expected_read_address);
        CHECK(dut.m_arprot == expected_read_protection);
        downstream_read_addresses++;
        read_response_pending = true;
        read_response_delay = random() % 12;
        read_response = peripheral_response(dut.m_araddr);
        read_response_data = memory[(dut.m_araddr >> 2) % memory.size()];
      }
    }
  }
  void clock_cycle() {
    dut.clk = 0;
    // Settle combinational outputs before sampling rising-edge handshakes.
    drive_peripheral_inputs();
    evaluate();
    capture_upstream_handshakes();
    update_peripheral();
    // Peripheral updates become DUT inputs on the next call, avoiding races.
    dut.clk = 1;
    evaluate();
    cycle_count++;
  }
  void reset() {
    dut.rst_n = 0;
    dut.cfg_valid = 0;
    dut.cfg_lock = 0;
    dut.s_awvalid = 0;
    dut.s_wvalid = 0;
    dut.s_arvalid = 0;
    dut.s_bready = 0;
    dut.s_rready = 0;
    pending_write_address = pending_write_data = write_response_pending =
        read_response_pending = false;
    write_response_delay = read_response_delay = 0;
    expect_read = expect_write = false;
    for (int i = 0; i < 3; i++)
      clock_cycle();
    dut.rst_n = 1;
    clock_cycle();
    regions = {};
    CHECK(!dut.locked);
    CHECK(dut.violation_count == 0);
  }
  bool is_permitted(uint32_t address, bool write) const {
    for (const auto &region : regions) {
      if (region.enabled && address >= region.base && address <= region.limit) {
        // First match wins, even if that region explicitly denies access.
        const bool aligned = address % 4 == 0;
        const bool word_fits = uint64_t(address) + 3 <= region.limit;
        return aligned && word_fits && (write ? region.write : region.read);
      }
    }
    return false;
  }

  void configure_region(unsigned index, RegionPolicy policy,
                        bool lock = false) {
    CHECK(index < regions.size());
    dut.cfg_valid = 1;
    dut.cfg_index = index;
    dut.cfg_base = policy.base;
    dut.cfg_limit = policy.limit;
    dut.cfg_enable = policy.enabled;
    dut.cfg_read = policy.read;
    dut.cfg_write = policy.write;
    dut.cfg_lock = lock;
    dut.clk = 0;
    evaluate();
    CHECK(dut.cfg_ready);
    clock_cycle();
    dut.cfg_valid = 0;
    dut.cfg_lock = 0;
    regions[index] = policy;
    CHECK(bool(dut.locked) == lock);
  }
  // Concurrent independent read/write drivers; delays and response backpressure
  // are varied.
  void run_transaction(bool write_enabled, uint32_t write_address,
                       bool read_enabled, uint32_t read_address,
                       TransactionTiming timing = {}) {
    scenario_count++;
    bool write_address_done = !write_enabled, write_data_done = !write_enabled,
         read_address_done = !read_enabled,
         write_response_done = !write_enabled,
         read_response_done = !read_enabled;
    bool write_permitted = write_enabled && is_permitted(write_address, true),
         read_permitted = read_enabled && is_permitted(read_address, false);
    expect_write = write_permitted;
    expect_read = read_permitted;
    expected_write_address = write_address;
    expected_read_address = read_address;
    expected_write_data = random();
    expected_write_strobes = timing.write_strobes;
    expected_write_protection = random() % 8;
    expected_read_protection = random() % 8;
    uint32_t previous_violations = dut.violation_count,
             expected_read_data = memory[(read_address >> 2) % memory.size()];
    unsigned previous_write_addresses = downstream_write_addresses,
             previous_write_data_beats = downstream_write_data_beats,
             previous_read_addresses = downstream_read_addresses;
    dut.s_awaddr = write_address;
    dut.s_awprot = expected_write_protection;
    dut.s_wdata = expected_write_data;
    dut.s_wstrb = timing.write_strobes;
    dut.s_araddr = read_address;
    dut.s_arprot = expected_read_protection;
    for (int t = 0; t < kTransactionTimeout; t++) {
      dut.s_awvalid = !write_address_done && t >= timing.write_address_delay;
      dut.s_wvalid = !write_data_done && t >= timing.write_data_delay;
      dut.s_arvalid = !read_address_done && t >= timing.read_address_delay;
      dut.s_bready = t >= timing.response_stall && (random() % 4 != 0);
      dut.s_rready = t >= timing.response_stall && (random() % 4 != 0);
      clock_cycle();
      if (write_address_accepted)
        write_address_done = true;
      if (write_data_accepted)
        write_data_done = true;
      if (read_address_accepted)
        read_address_done = true;
      if (write_response_accepted) {
        CHECK(!write_response_done);
        write_response_done = true;
        CHECK(observed_write_response ==
              expected_response(write_permitted, write_address));
      }
      if (read_response_accepted) {
        CHECK(!read_response_done);
        read_response_done = true;
        CHECK(observed_read_response ==
              expected_response(read_permitted, read_address));
        CHECK(observed_read_data == (read_permitted ? expected_read_data : 0));
      }
      if (write_response_done && read_response_done)
        break;
    }
    CHECK(write_response_done && read_response_done);
    CHECK(write_address_done && write_data_done && read_address_done);
    CHECK(downstream_write_addresses - previous_write_addresses ==
          unsigned(write_permitted));
    CHECK(downstream_write_data_beats - previous_write_data_beats ==
          unsigned(write_permitted));
    CHECK(downstream_read_addresses - previous_read_addresses ==
          unsigned(read_permitted));
    CHECK(dut.violation_count ==
          previous_violations + unsigned(write_enabled && !write_permitted) +
              unsigned(read_enabled && !read_permitted));
    CHECK(bool(dut.violation_irq) == (dut.violation_count != 0));
    if (write_enabled && !write_permitted && !read_enabled) {
      CHECK(dut.last_violation_addr == write_address);
      CHECK(dut.last_violation_write);
    }
    if (read_enabled && !read_permitted && !write_enabled) {
      CHECK(dut.last_violation_addr == read_address);
      CHECK(!dut.last_violation_write);
    }
    dut.s_awvalid = dut.s_wvalid = dut.s_arvalid = 0;
    clock_cycle();
    expect_write = expect_read = false;
  }
};
void test_boundaries_and_channel_order(FirewallSimulation &s) {
  s.run_transaction(true, 0x1000, true, 0x2000, {0, 8, 2, 25});
  s.configure_region(
      0, {0x1080, 0x108f, true, false, false}); // explicit deny overlay
  s.configure_region(1, {0x1000, 0x10ff, true, true, true});
  s.configure_region(2, {0x2000, 0x20ff, true, true, false});
  s.configure_region(3, {0xfffffff0, 0xffffffff, true, true, true});
  for (unsigned a :
       {0xffcu, 0x1000u, 0x107cu, 0x1080u, 0x108cu, 0x1090u, 0x10fcu, 0x1100u,
        0x1001u, 0x2000u, 0x20fcu, 0x2100u, 0xfffffffcu, 0xffffffffu}) {
    s.run_transaction(true, a, false, 0, {0, 12, 0, 30});
    s.run_transaction(true, a, false, 0, {12, 0, 0, 30});
    s.run_transaction(false, 0, true, a, {0, 0, 0, 30});
  }
}

void test_byte_strobes(FirewallSimulation &s) {
  // Verify every byte strobe, including zero-byte writes, by subsequent reads.
  for (unsigned write_strobes = 0; write_strobes < 16; write_strobes++) {
    s.run_transaction(true, 0x1000, false, 0, {0, 0, 0, 15, write_strobes});
    s.run_transaction(false, 0, true, 0x1000);
  }
}

void test_simultaneous_violations(FirewallSimulation &s) {
  // Two denials occur on exactly the same edge: buffered write dispatch + read
  // capture.
  auto n = s.dut.violation_count;
  s.run_transaction(true, 0x3000, true, 0x4000, {0, 0, 1, 20});
  CHECK(s.dut.violation_count == n + 2);
  CHECK(s.dut.last_violation_write);
  CHECK(s.dut.last_violation_addr == 0x3000);
}

void test_busy_configuration_and_reset(FirewallSimulation &s) {
  // Busy configuration must not alter an address policy already accepted.
  s.dut.s_awaddr = 0x1000;
  s.dut.s_awvalid = 1;
  s.clock_cycle();
  CHECK(s.write_address_accepted);
  s.dut.s_awvalid = 0;
  s.dut.cfg_valid = 1;
  s.dut.cfg_index = 1;
  s.dut.cfg_enable = 0;
  s.dut.clk = 0;
  s.evaluate();
  CHECK(!s.dut.cfg_ready);
  s.clock_cycle();
  s.dut.cfg_valid = 0;
  s.reset(); // resets an incomplete write, including any cached authorization
  s.run_transaction(true, 0x1000, false, 0, {8, 0});
}

void test_region_configuration(FirewallSimulation &s) {
  // Boundary containment, malformed region, and disabled region.
  s.configure_region(0, {0x1000, 0x1002, true, true, true});
  s.run_transaction(true, 0x1000, true, 0x1000);
  s.configure_region(0, {0x1010, 0x1000, true, true, true});
  s.run_transaction(false, 0, true, 0x1000);
  s.configure_region(0, {0x1000, 0x10ff, false, true, true});
  s.run_transaction(true, 0x1000, false, 0);
  s.configure_region(0, {0x1000, 0x10ff, true, true, true});
  s.configure_region(1, {0x2000, 0x20ff, true, true, false});
  s.configure_region(2, {0x3000, 0x30ff, true, false, true});
  s.configure_region(3, {0x4000, 0x40ff, true, false, false}, true);
}

void test_configuration_lock(FirewallSimulation &s) {
  // Locked policy rejects configuration writes.
  s.dut.cfg_valid = 1;
  s.dut.cfg_index = 0;
  s.dut.cfg_enable = 0;
  s.dut.cfg_lock = 0;
  for (int i = 0; i < 5; i++) {
    s.dut.clk = 0;
    s.evaluate();
    CHECK(!s.dut.cfg_ready);
    s.clock_cycle();
  }
  s.dut.cfg_valid = 0;
  s.run_transaction(true, 0x1000, false, 0);
  s.run_transaction(false, 0, true, 0x1000);
}

void test_randomized_traffic(FirewallSimulation &s) {
  for (unsigned i = 0; i < kRandomScenarios; i++) {
    uint32_t write_address =
        0x1000 + (s.random() % 5) * 0x1000 + (s.random() % 70) * 4;
    uint32_t read_address =
        0x1000 + (s.random() % 5) * 0x1000 + (s.random() % 70) * 4;
    // Avoid ambiguous read-during-write to the same modeled memory word.
    if ((write_address >> 2) % 1024 == (read_address >> 2) % 1024)
      read_address += 4;
    const bool write_enabled = s.random() % 2;
    bool read_enabled = s.random() % 2;
    if (!write_enabled && !read_enabled)
      read_enabled = true;
    TransactionTiming timing;
    timing.write_strobes = s.random() % 16;
    timing.response_stall = 20 + s.random() % 30;
    timing.read_address_delay = s.random() % 15;
    timing.write_data_delay = s.random() % 15;
    timing.write_address_delay = s.random() % 15;
    s.run_transaction(write_enabled, write_address, read_enabled, read_address,
                      timing);
  }
}

void test_reset_recovery(FirewallSimulation &s) {
  // Reset clears lock and all permissions.
  s.reset();
  s.run_transaction(true, 0x1000, true, 0x2000);
  // Reset after W arrives without AW.
  s.dut.s_wvalid = 1;
  s.clock_cycle();
  CHECK(s.write_data_accepted);
  s.dut.s_wvalid = 0;
  s.reset();
  s.run_transaction(true, 0x1000, false, 0);
  // Reset while denied responses are stalled upstream.
  s.dut.s_bready = s.dut.s_rready = 0;
  s.dut.s_awvalid = s.dut.s_wvalid = s.dut.s_arvalid = 1;
  s.clock_cycle();
  CHECK(s.write_address_accepted && s.write_data_accepted &&
        s.read_address_accepted);
  s.dut.s_awvalid = s.dut.s_wvalid = s.dut.s_arvalid = 0;
  s.clock_cycle();
  CHECK(s.dut.s_bvalid && s.dut.s_rvalid);
  s.reset();
  CHECK(!s.dut.s_bvalid && !s.dut.s_rvalid);
  s.run_transaction(true, 0x1000, true, 0x2000);
}
} // namespace

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  unsigned seed = 1;
  try {
    if (argc > 3 || (argc == 3 && std::string(argv[2]) != "--trace"))
      throw std::invalid_argument("usage: Vaxi_lite_firewall [seed] [--trace]");
    if (argc > 1) {
      std::string argument = argv[1];
      if (argument.empty() ||
          argument.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("seed must be an unsigned integer");
      const auto value = std::stoull(argument);
      if (value > std::numeric_limits<unsigned>::max())
        throw std::out_of_range("seed exceeds unsigned integer range");
      seed = static_cast<unsigned>(value);
    }
    const bool waves = argc == 3;
    FirewallSimulation s(seed, waves);
    test_boundaries_and_channel_order(s);
    test_byte_strobes(s);
    test_simultaneous_violations(s);
    test_busy_configuration_and_reset(s);
    test_region_configuration(s);
    test_configuration_lock(s);
    test_randomized_traffic(s);
    test_reset_recovery(s);
    std::cout << "PASS seed=" << seed << " transactions=" << s.scenario_count
              << " cycles=" << s.cycle_count
              << " downstream_aw=" << s.downstream_write_addresses
              << " downstream_ar=" << s.downstream_read_addresses << "\n";
  } catch (const std::exception &e) {
    std::cerr << "FAIL seed=" << seed << " " << e.what() << "\n";
    return 1;
  }
}
