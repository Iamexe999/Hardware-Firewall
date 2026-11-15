// SPDX-License-Identifier: MIT
module axi_lite_firewall (
  input logic clk, rst_n,
  input logic cfg_valid, cfg_lock,
  output logic cfg_ready, locked,
  input logic [1:0] cfg_index,
  input logic [31:0] cfg_base, cfg_limit,
  input logic cfg_enable, cfg_read, cfg_write,
  output logic [31:0] violation_count, last_violation_addr,
  output logic last_violation_write, violation_irq,
  input logic [31:0] s_awaddr,
  input logic [2:0] s_awprot,
  input logic s_awvalid,
  output logic s_awready,
  input logic [31:0] s_wdata,
  input logic [3:0] s_wstrb,
  input logic s_wvalid,
  output logic s_wready,
  output logic [1:0] s_bresp,
  output logic s_bvalid,
  input logic s_bready,
  input logic [31:0] s_araddr,
  input logic [2:0] s_arprot,
  input logic s_arvalid,
  output logic s_arready,
  output logic [31:0] s_rdata,
  output logic [1:0] s_rresp,
  output logic s_rvalid,
  input logic s_rready,
  output logic [31:0] m_awaddr,
  output logic [2:0] m_awprot,
  output logic m_awvalid,
  input logic m_awready,
  output logic [31:0] m_wdata,
  output logic [3:0] m_wstrb,
  output logic m_wvalid,
  input logic m_wready,
  input logic [1:0] m_bresp,
  input logic m_bvalid,
  output logic m_bready,
  output logic [31:0] m_araddr,
  output logic [2:0] m_arprot,
  output logic m_arvalid,
  input logic m_arready,
  input logic [31:0] m_rdata,
  input logic [1:0] m_rresp,
  input logic m_rvalid,
  output logic m_rready
);

  logic [31:0] base [4], limit_addr [4];
  logic [3:0] enabled, rd_perm, wr_perm;
  logic aw_full, w_full, write_active, aw_sent, w_sent, write_allowed;
  logic [31:0] aw_addr, w_data;
  logic [2:0] aw_prot;
  logic [3:0] w_strb;
  logic read_active, ar_sent;
  logic [31:0] ar_addr;
  logic [2:0] ar_prot;
  logic deny_w, deny_r;

  // Lowest-index matching region wins, including an explicit deny region.
  // Policy granularity is a full aligned 32-bit word, even for partial writes.
  function automatic logic permitted(input logic [31:0] addr, input logic wr);
    logic found;
    begin
      permitted = 0;
      found = 0;
      for (int i=0; i<4; i++) begin
        if (!found && enabled[i] && addr >= base[i] && addr <= limit_addr[i]) begin
          found = 1;
          permitted = (addr[1:0] == 0) &&
                      ({1'b0,addr}+33'd3 <= {1'b0,limit_addr[i]}) &&
                      (wr ? wr_perm[i] : rd_perm[i]);
        end
      end
    end
  endfunction

  assign cfg_ready = rst_n && !locked && !aw_full && !w_full &&
                     !write_active && !s_bvalid && !read_active && !s_rvalid;
  assign s_awready = rst_n && !cfg_valid && !aw_full && !write_active && !s_bvalid;
  assign s_wready  = rst_n && !cfg_valid && !w_full && !write_active && !s_bvalid;
  assign s_arready = rst_n && !cfg_valid && !read_active && !s_rvalid;
  assign m_awaddr = aw_addr;
  assign m_awprot = aw_prot;
  assign m_wdata = w_data;
  assign m_wstrb = w_strb;
  assign m_awvalid = rst_n && write_active && !aw_sent;
  assign m_wvalid = rst_n && write_active && !w_sent;
  assign m_bready = rst_n && write_active && aw_sent && w_sent && !s_bvalid;
  assign m_araddr = ar_addr;
  assign m_arprot = ar_prot;
  assign m_arvalid = rst_n && read_active && !ar_sent;
  assign m_rready = rst_n && read_active && ar_sent && !s_rvalid;
  assign deny_w = aw_full && w_full && !write_active && !s_bvalid && !write_allowed;
  assign deny_r = s_arvalid && s_arready && !permitted(s_araddr, 0);
  assign violation_irq = (violation_count != 0);

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      enabled <= 0; rd_perm <= 0; wr_perm <= 0; locked <= 0;
      for (int i=0; i<4; i++) begin base[i] <= 0; limit_addr[i] <= 0; end
      aw_full <= 0; w_full <= 0; write_active <= 0; aw_sent <= 0; w_sent <= 0;
      write_allowed <= 0; aw_addr <= 0; aw_prot <= 0; w_data <= 0; w_strb <= 0;
      read_active <= 0; ar_sent <= 0; ar_addr <= 0; ar_prot <= 0;
      s_bvalid <= 0; s_bresp <= 0; s_rvalid <= 0; s_rresp <= 0; s_rdata <= 0;
      violation_count <= 0; last_violation_addr <= 0; last_violation_write <= 0;
    end else begin
      if (cfg_valid && cfg_ready) begin
        base[cfg_index] <= cfg_base; limit_addr[cfg_index] <= cfg_limit;
        enabled[cfg_index] <= cfg_enable;
        rd_perm[cfg_index] <= cfg_read; wr_perm[cfg_index] <= cfg_write;
        if (cfg_lock) locked <= 1;
      end
      if (s_bvalid && s_bready) s_bvalid <= 0;
      if (s_rvalid && s_rready) s_rvalid <= 0;
      if (s_awvalid && s_awready) begin
        aw_full <= 1; aw_addr <= s_awaddr; aw_prot <= s_awprot;
        write_allowed <= permitted(s_awaddr, 1);
      end
      if (s_wvalid && s_wready) begin
        w_full <= 1; w_data <= s_wdata; w_strb <= s_wstrb;
      end
      if (aw_full && w_full && !write_active && !s_bvalid) begin
        aw_full <= 0; w_full <= 0;
        if (write_allowed) begin
          write_active <= 1; aw_sent <= 0; w_sent <= 0;
        end else begin s_bvalid <= 1; s_bresp <= 2'b11; end // DECERR
      end
      if (m_awvalid && m_awready) aw_sent <= 1;
      if (m_wvalid && m_wready) w_sent <= 1;
      if (m_bvalid && m_bready) begin
        s_bvalid <= 1; s_bresp <= m_bresp; write_active <= 0;
      end
      if (s_arvalid && s_arready) begin
        ar_addr <= s_araddr; ar_prot <= s_arprot;
        if (permitted(s_araddr, 0)) begin read_active <= 1; ar_sent <= 0; end
        else begin s_rvalid <= 1; s_rresp <= 2'b11; s_rdata <= 0; end
      end
      if (m_arvalid && m_arready) ar_sent <= 1;
      if (m_rvalid && m_rready) begin
        s_rvalid <= 1; s_rresp <= m_rresp; s_rdata <= m_rdata; read_active <= 0;
      end
      // Count both simultaneous events; write wins the last-event register.
      if (deny_w || deny_r) begin
        if (violation_count >= 32'hfffffffe)
          violation_count <= 32'hffffffff;
        else violation_count <= violation_count + {31'b0,deny_w} + {31'b0,deny_r};
        last_violation_addr <= deny_w ? aw_addr : s_araddr;
        last_violation_write <= deny_w;
      end
    end
  end

`ifndef SYNTHESIS
  assert property (@(posedge clk) disable iff (!rst_n)
    m_awvalid && !m_awready |=> m_awvalid && $stable({m_awaddr,m_awprot}));
  assert property (@(posedge clk) disable iff (!rst_n)
    m_wvalid && !m_wready |=> m_wvalid && $stable({m_wdata,m_wstrb}));
  assert property (@(posedge clk) disable iff (!rst_n)
    m_arvalid && !m_arready |=> m_arvalid && $stable({m_araddr,m_arprot}));
  assert property (@(posedge clk) disable iff (!rst_n)
    s_bvalid && !s_bready |=> s_bvalid && $stable(s_bresp));
  assert property (@(posedge clk) disable iff (!rst_n)
    s_rvalid && !s_rready |=> s_rvalid && $stable({s_rdata,s_rresp}));
  assert property (@(posedge clk) disable iff (!rst_n) locked |=> locked);
`endif
endmodule
