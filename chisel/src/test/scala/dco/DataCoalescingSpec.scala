package dco

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec

class DataCoalescingSpec extends AnyFlatSpec with ChiselScalatestTester {
  behavior of "DataCoalescer"

  private def clearReq(dut: DataCoalescer): Unit = {
    for (i <- 0 until 4) {
      dut.io.coreReq(i).valid.poke(false.B)
      dut.io.coreReq(i).bits.addr.poke(0.U)
      dut.io.coreReq(i).bits.write.poke(false.B)
      dut.io.coreReq(i).bits.wdata.poke(0.U)
      dut.io.coreReq(i).bits.be.poke("hf".U)
    }
    dut.io.memReq.ready.poke(true.B)
    dut.io.memResp.valid.poke(false.B)
    for (i <- 0 until 8) dut.io.memResp.bits.rdata(i).poke((0x100 + i).U)
  }

  it should "merge four contiguous loads and route each returned word" in {
    test(new DataCoalescer) { dut =>
      clearReq(dut)
      for (i <- 0 until 4) {
        dut.io.coreReq(i).valid.poke(true.B)
        dut.io.coreReq(i).bits.addr.poke((0x1000 + i * 4).U)
      }
      dut.clock.step()
      dut.io.coreReq(0).valid.poke(false.B)
      dut.io.coreReq(1).valid.poke(false.B)
      dut.io.coreReq(2).valid.poke(false.B)
      dut.io.coreReq(3).valid.poke(false.B)
      dut.clock.step()
      dut.io.memReq.valid.expect(true.B)
      dut.io.memReq.bits.lineAddr.expect("h1000".U)
      dut.io.memReq.bits.write.expect(false.B)
      dut.clock.step()
      dut.io.memResp.valid.poke(true.B)
      for (i <- 0 until 8) dut.io.memResp.bits.rdata(i).poke((0xabc00000L + i).U)
      dut.clock.step()
      for (i <- 0 until 4) {
        dut.io.coreResp(i).valid.expect(true.B)
        dut.io.coreResp(i).bits.data.expect((0xabc00000L + i).U)
      }
    }
  }

  it should "merge stores into one line transaction with byte enables" in {
    test(new DataCoalescer) { dut =>
      clearReq(dut)
      for (i <- 0 until 4) {
        dut.io.coreReq(i).valid.poke(true.B)
        dut.io.coreReq(i).bits.addr.poke((0x2000 + i * 4).U)
        dut.io.coreReq(i).bits.write.poke(true.B)
        dut.io.coreReq(i).bits.wdata.poke((0xfeed0000L + i).U)
      }
      dut.clock.step()
      for (i <- 0 until 4) dut.io.coreReq(i).valid.poke(false.B)
      dut.clock.step()
      dut.io.memReq.valid.expect(true.B)
      dut.io.memReq.bits.write.expect(true.B)
      dut.io.memReq.bits.lineAddr.expect("h2000".U)
      dut.io.memReq.bits.byteen.expect("h0000ffff".U)
      for (i <- 0 until 4) dut.io.memReq.bits.wdata(i).expect((0xfeed0000L + i).U)
    }
  }
}
