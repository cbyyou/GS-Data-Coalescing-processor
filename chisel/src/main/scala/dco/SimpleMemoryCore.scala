package dco

import chisel3._
import chisel3.util._

class CoreConfig extends Bundle {
  val base = UInt(32.W)
  val stride = UInt(32.W)
  val count = UInt(16.W)
  val write = Bool()
  val wdataBase = UInt(32.W)
}

/** Minimal independent load/store stream generator matching simple_memory_core.sv. */
class SimpleMemoryCore extends Module {
  val io = IO(new Bundle {
    val cfg = Input(new CoreConfig)
    val start = Input(Bool())
    val busy = Output(Bool())
    val done = Output(Bool())
    val completed = Output(UInt(16.W))
    val lastRdata = Output(UInt(32.W))
    val req = Decoupled(new LaneReq)
    val resp = Flipped(Valid(new LaneResp))
  })

  val sIdle :: sRequest :: sWait :: Nil = Enum(3)
  val state = RegInit(sIdle)
  val addr = RegInit(0.U(32.W))
  val stride = RegInit(0.U(32.W))
  val wdata = RegInit(0.U(32.W))
  val remaining = RegInit(0.U(16.W))
  val write = RegInit(false.B)
  val completed = RegInit(0.U(16.W))
  val done = RegInit(false.B)
  val lastRdata = RegInit(0.U(32.W))

  io.busy := state =/= sIdle
  io.done := done
  io.completed := completed
  io.lastRdata := lastRdata
  io.req.valid := state === sRequest
  io.req.bits.addr := addr
  io.req.bits.write := write
  io.req.bits.wdata := wdata
  io.req.bits.be := "hf".U

  done := false.B
  switch (state) {
    is (sIdle) {
      when (io.start) {
        addr := io.cfg.base
        stride := io.cfg.stride
        wdata := io.cfg.wdataBase
        remaining := io.cfg.count
        write := io.cfg.write
        completed := 0.U
        when (io.cfg.count === 0.U) { done := true.B }
          .otherwise { state := sRequest }
      }
    }
    is (sRequest) {
      when (io.req.fire) { state := sWait }
    }
    is (sWait) {
      when (io.resp.valid) {
        lastRdata := io.resp.bits.data
        completed := completed + 1.U
        when (remaining === 1.U) {
          remaining := 0.U
          done := true.B
          state := sIdle
        }.otherwise {
          remaining := remaining - 1.U
          addr := addr + stride
          wdata := wdata + 1.U
          state := sRequest
        }
      }
    }
  }
}

/** Four independent stream cores sharing the Chisel coalescer. */
class DataCoalescingSystem extends Module {
  val io = IO(new Bundle {
    val cfg = Input(Vec(4, new CoreConfig))
    val start = Input(Vec(4, Bool()))
    val busy = Output(Vec(4, Bool()))
    val done = Output(Vec(4, Bool()))
    val completed = Output(Vec(4, UInt(16.W)))
    val lastRdata = Output(Vec(4, UInt(32.W)))
    val memReq = Decoupled(new LineReq)
    val memResp = Flipped(Valid(new LineResp))
  })

  val cores = Seq.fill(4)(Module(new SimpleMemoryCore))
  val coalescer = Module(new DataCoalescer)
  for (i <- 0 until 4) {
    cores(i).io.cfg := io.cfg(i)
    cores(i).io.start := io.start(i)
    io.busy(i) := cores(i).io.busy
    io.done(i) := cores(i).io.done
    io.completed(i) := cores(i).io.completed
    io.lastRdata(i) := cores(i).io.lastRdata
    coalescer.io.coreReq(i) <> cores(i).io.req
    coalescer.io.coreResp(i) <> cores(i).io.resp
  }
  io.memReq <> coalescer.io.memReq
  coalescer.io.memResp := io.memResp
}
