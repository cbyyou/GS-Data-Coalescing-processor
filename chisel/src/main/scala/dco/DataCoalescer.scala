package dco

import chisel3._
import chisel3.util._

class LaneReq extends Bundle {
  val addr = UInt(32.W)
  val write = Bool()
  val wdata = UInt(32.W)
  val be = UInt(4.W)
}

class LaneResp extends Bundle {
  val data = UInt(32.W)
}

class LineReq extends Bundle {
  val write = Bool()
  val lineAddr = UInt(32.W)
  val wdata = Vec(8, UInt(32.W))
  val byteen = UInt(32.W)
}

class LineResp extends Bundle {
  val rdata = Vec(8, UInt(32.W))
}

/** Four-requester, one-entry-per-requester 32-byte segment coalescer. */
class DataCoalescer extends Module {
  val io = IO(new Bundle {
    val coreReq = Vec(4, Flipped(Decoupled(new LaneReq)))
    val coreResp = Vec(4, Valid(new LaneResp))
    val memReq = Decoupled(new LineReq)
    val memResp = Flipped(Valid(new LineResp))
  })

  val sCollect :: sIssue :: sWait :: Nil = Enum(3)
  val state = RegInit(sCollect)
  val bufValid = RegInit(VecInit(Seq.fill(4)(false.B)))
  val bufReq = Reg(Vec(4, new LaneReq))
  val activeMask = RegInit(0.U(4.W))
  val activeAddr = Reg(Vec(4, UInt(32.W)))
  val activeWrite = RegInit(false.B)
  val activeLine = RegInit(0.U(32.W))
  val activeWdata = Reg(Vec(8, UInt(32.W)))
  val activeByteen = RegInit(0.U(32.W))
  val respValid = RegInit(0.U(4.W))
  val respData = Reg(Vec(4, UInt(32.W)))

  for (i <- 0 until 4) {
    io.coreReq(i).ready := !bufValid(i)
    io.coreResp(i).valid := respValid(i)
    io.coreResp(i).bits.data := respData(i)
  }

  val hasBuffered = bufValid.asUInt.orR
  val seed = PriorityEncoder(bufValid.asUInt)
  def lookup[T <: Data](v: Seq[T], idx: UInt): T = {
    MuxLookup(idx, v.head)(v.zipWithIndex.map { case (x, i) => i.U -> x })
  }
  val seedAddr = lookup((0 until 4).map(i => bufReq(i).addr), seed)
  val seedWrite = lookup((0 until 4).map(i => bufReq(i).write), seed)
  val selectedLine = seedAddr & "hffffffE0".U
  val selectedMask = Wire(UInt(4.W))
  selectedMask := VecInit((0 until 4).map { i =>
    bufValid(i) && ((bufReq(i).addr & "hffffffE0".U) === selectedLine) &&
      (bufReq(i).write === seedWrite)
  }).asUInt
  val selectedWdata = Wire(Vec(8, UInt(32.W)))
  val selectedByteen = Wire(UInt(32.W))
  val selectedByteenWords = Wire(Vec(8, UInt(4.W)))
  for (wordIndex <- 0 until 8) {
    val wordCandidates = (0 until 4).map { i =>
      val matchesWord = bufReq(i).addr(4, 2) === wordIndex.U
      (selectedMask(i) && bufReq(i).write && matchesWord) -> bufReq(i).wdata
    }
    val byteCandidates = (0 until 4).map { i =>
      val matchesWord = bufReq(i).addr(4, 2) === wordIndex.U
      (selectedMask(i) && bufReq(i).write && matchesWord) -> bufReq(i).be
    }
    // MuxCase preserves the candidate order, so the lowest lane wins a
    // same-word store conflict. The byte enable follows the winning lane.
    selectedWdata(wordIndex) := MuxCase(0.U, wordCandidates)
    selectedByteenWords(wordIndex) := MuxCase(0.U, byteCandidates)
  }
  selectedByteen := selectedByteenWords.asUInt

  io.memReq.valid := state === sIssue
  io.memReq.bits.write := activeWrite
  io.memReq.bits.lineAddr := activeLine
  io.memReq.bits.byteen := activeByteen
  for (i <- 0 until 8) io.memReq.bits.wdata(i) := activeWdata(i)

  when (reset.asBool) {
    state := sCollect
    bufValid := VecInit(Seq.fill(4)(false.B))
    activeMask := 0.U
    respValid := 0.U
  }.otherwise {
    respValid := 0.U

    for (i <- 0 until 4) {
      when (io.coreReq(i).fire) {
        bufValid(i) := true.B
        bufReq(i) := io.coreReq(i).bits
      }
    }

    switch (state) {
      is (sCollect) {
        when (hasBuffered) {
          activeMask := selectedMask
          activeWrite := seedWrite
          activeLine := selectedLine
          activeByteen := selectedByteen
          for (i <- 0 until 4) activeAddr(i) := bufReq(i).addr
          for (i <- 0 until 8) activeWdata(i) := selectedWdata(i)
          state := sIssue
        }
      }
      is (sIssue) {
        when (io.memReq.fire) { state := sWait }
      }
      is (sWait) {
        when (io.memResp.valid) {
          respValid := activeMask
          for (i <- 0 until 4) {
            when (activeMask(i)) {
              bufValid(i) := false.B
              when (!activeWrite) {
                respData(i) := io.memResp.bits.rdata(activeAddr(i)(4, 2))
              }.otherwise {
                respData(i) := 0.U
              }
            }
          }
          state := sCollect
        }
      }
    }
  }
}
