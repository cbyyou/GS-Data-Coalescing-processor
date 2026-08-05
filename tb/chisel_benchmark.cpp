#include "VDataCoalescingSystem.h"
#include "verilated.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unordered_map>

struct W { std::array<uint32_t,4> base{}, stride{}; std::array<uint16_t,4> count{}; std::array<uint32_t,4> wd{}; std::array<uint8_t,4> delay{}; std::array<bool,4> wr{}; };
class H {
 public:
  VDataCoalescingSystem d; uint64_t cyc=0, txn=0; bool pend=false, next=false; std::array<uint32_t,8> pdata{}, ndata{};
  std::unordered_map<uint32_t,std::array<uint32_t,8>> mem;
  H(){ d.clock=0; d.reset=1; d.io_memReq_ready=1; d.io_memResp_valid=0; d.eval(); tick(); tick(); d.reset=0; }
  static uint32_t init(uint32_t a){return 0x10000000u^(a>>2);}
  void cfg(const W&w){ for(int i=0;i<4;i++){d.io_cfg_0_base=(i==0?w.base[i]:d.io_cfg_0_base); d.io_cfg_1_base=(i==1?w.base[i]:d.io_cfg_1_base); d.io_cfg_2_base=(i==2?w.base[i]:d.io_cfg_2_base); d.io_cfg_3_base=(i==3?w.base[i]:d.io_cfg_3_base);}
    d.io_cfg_0_base=w.base[0];d.io_cfg_1_base=w.base[1];d.io_cfg_2_base=w.base[2];d.io_cfg_3_base=w.base[3]; d.io_cfg_0_stride=w.stride[0];d.io_cfg_1_stride=w.stride[1];d.io_cfg_2_stride=w.stride[2];d.io_cfg_3_stride=w.stride[3];
    d.io_cfg_0_count=w.count[0];d.io_cfg_1_count=w.count[1];d.io_cfg_2_count=w.count[2];d.io_cfg_3_count=w.count[3]; d.io_cfg_0_write=w.wr[0];d.io_cfg_1_write=w.wr[1];d.io_cfg_2_write=w.wr[2];d.io_cfg_3_write=w.wr[3]; d.io_cfg_0_wdataBase=w.wd[0];d.io_cfg_1_wdataBase=w.wd[1];d.io_cfg_2_wdataBase=w.wd[2];d.io_cfg_3_wdataBase=w.wd[3]; }
  uint64_t run(const W&w){cfg(w); uint64_t start=cyc,t0=txn; uint8_t done=0; uint64_t total=0; for(auto n:w.count)total+=n; for(uint64_t e=0;e<total*30+100;e++){d.io_start_0=(e==w.delay[0]);d.io_start_1=(e==w.delay[1]);d.io_start_2=(e==w.delay[2]);d.io_start_3=(e==w.delay[3]);tick();d.io_start_0=d.io_start_1=d.io_start_2=d.io_start_3=0;done|=(d.io_done_0?1:0)|(d.io_done_1?2:0)|(d.io_done_2?4:0)|(d.io_done_3?8:0);if(done==15)return cyc-start;} std::fprintf(stderr,"timeout\n"); std::abort();}
 private:
  auto& line(uint32_t a){auto [it,ins]=mem.try_emplace(a);if(ins)for(int i=0;i<8;i++)it->second[i]=init(a+4*i);return it->second;}
  void tick(){d.clock=0;d.io_memResp_valid=pend; for(int i=0;i<8;i++)((&d.io_memResp_bits_rdata_0)[i])=pdata[i]; d.eval(); if(d.io_memReq_valid&&d.io_memReq_ready){txn++;auto&x=line(d.io_memReq_bits_lineAddr);for(int i=0;i<8;i++)ndata[i]=x[i];if(d.io_memReq_bits_write)for(int b=0;b<32;b++)if((d.io_memReq_bits_byteen>>b)&1){int q=b/4,s=(b%4)*8;uint32_t m=255u<<s;x[q]=(x[q]&~m)|(((&d.io_memReq_bits_wdata_0)[q]&m));}next=true;} d.clock=1;d.eval();cyc++;d.clock=0;d.eval();pend=next;pdata=ndata;next=false;}
};
static W make(uint32_t b,uint32_t s,std::array<uint32_t,4>o){W w;for(int i=0;i<4;i++){w.base[i]=b+o[i];w.stride[i]=s;w.count[i]=1000;}return w;}
int main(int argc,char**argv){Verilated::commandArgs(argc,argv);std::ofstream csv("build/chisel_benchmark.csv");csv<<"pattern,requests,transactions,requests_per_transaction,cycles\n";for(auto p:std::array<std::pair<const char*,W>,3>{{{"contiguous",make(0x1000,32,{0,4,8,12})},{"paired",make(0x2000,64,{0,4,32,36})},{"strided",make(0x3000,128,{0,32,64,96})}}}){H h;auto c=h.run(p.second);std::printf("chisel_%s requests=4000 transactions=%llu req_per_txn=%.3f cycles=%llu\n",p.first,(unsigned long long)h.txn,4000.0/h.txn,(unsigned long long)c);csv<<p.first<<",4000,"<<h.txn<<","<<4000.0/h.txn<<","<<c<<"\n";}return 0;}
