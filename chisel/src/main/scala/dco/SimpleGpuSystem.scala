package dco

import chisel3._
import chisel3.util._

/** Chisel top-level equivalent of simple_gpu_system.sv. */
class SimpleGpuSystem extends Module {
  val io = IO(new Bundle {
    val imemWe = Input(Bool())
    val imemWaddr = Input(UInt(6.W))
    val imemWdata = Input(UInt(32.W))
    val start = Input(Bool())
    val busy = Output(Bool())
    val done = Output(Bool())
    val debugPc = Output(UInt(6.W))
    val retired = Output(UInt(32.W))
    val memReq = Decoupled(new LineReq)
    val memResp = Flipped(Valid(new LineResp))
  })

  val core = Module(new SimpleSimtCore)
  val coalescer = Module(new DataCoalescer)
  core.io.imemWe := io.imemWe
  core.io.imemWaddr := io.imemWaddr
  core.io.imemWdata := io.imemWdata
  core.io.start := io.start
  io.busy := core.io.busy
  io.done := core.io.done
  io.debugPc := core.io.debugPc
  io.retired := core.io.retired
  for (i <- 0 until 4) {
    coalescer.io.coreReq(i) <> core.io.laneReq(i)
    core.io.laneResp(i) <> coalescer.io.coreResp(i)
  }
  io.memReq <> coalescer.io.memReq
  coalescer.io.memResp := io.memResp
}
