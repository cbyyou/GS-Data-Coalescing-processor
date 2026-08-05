package dco

import chisel3._
import chisel3.util._

/** Four-lane lockstep SIMT core with the same minimal ISA as simple_simt_core.sv. */
class SimpleSimtCore extends Module {
  val io = IO(new Bundle {
    val imemWe = Input(Bool())
    val imemWaddr = Input(UInt(6.W))
    val imemWdata = Input(UInt(32.W))
    val start = Input(Bool())
    val busy = Output(Bool())
    val done = Output(Bool())
    val debugPc = Output(UInt(6.W))
    val retired = Output(UInt(32.W))
    val laneReq = Vec(4, Decoupled(new LaneReq))
    val laneResp = Vec(4, Flipped(Valid(new LaneResp)))
  })

  val OP_NOP = 0.U(4.W)
  val OP_MOVI = 1.U(4.W)
  val OP_LID = 2.U(4.W)
  val OP_ADD = 3.U(4.W)
  val OP_ADDI = 4.U(4.W)
  val OP_SLLI = 5.U(4.W)
  val OP_LOAD = 6.U(4.W)
  val OP_STORE = 7.U(4.W)
  val OP_SUB = 8.U(4.W)
  val OP_AND = 9.U(4.W)
  val OP_OR = 10.U(4.W)
  val OP_XOR = 11.U(4.W)
  val OP_SRLI = 12.U(4.W)
  val OP_HALT = 15.U(4.W)

  val sIdle :: sRun :: sMemory :: Nil = Enum(3)
  val state = RegInit(sIdle)
  val pc = RegInit(0.U(6.W))
  val retired = RegInit(0.U(32.W))
  val done = RegInit(false.B)
  val imem = Reg(Vec(64, UInt(32.W)))
  val regs = Reg(Vec(4, Vec(8, UInt(32.W))))
  val reqPending = RegInit(0.U(4.W))
  val rspPending = RegInit(0.U(4.W))
  val lsAddr = Reg(Vec(4, UInt(32.W)))
  val lsWdata = Reg(Vec(4, UInt(32.W)))
  val lsRd = RegInit(0.U(3.W))
  val lsWrite = RegInit(false.B)

  when (io.imemWe) { imem(io.imemWaddr) := io.imemWdata }

  val instruction = imem(pc)
  val opcode = instruction(31, 28)
  val rd = instruction(27, 25)
  val rs1 = instruction(24, 22)
  val rs2 = instruction(21, 19)
  val immediate = Cat(Fill(13, instruction(18)), instruction(18, 0))
  val laneReadyMask = VecInit(io.laneReq.map(_.ready)).asUInt
  val laneValidMask = VecInit(io.laneResp.map(_.valid)).asUInt
  val acceptedMask = reqPending & laneReadyMask
  val responseMask = rspPending & laneValidMask
  val reqRemaining = reqPending & ~acceptedMask
  val rspRemaining = rspPending & ~responseMask

  io.busy := state =/= sIdle
  io.done := done
  io.debugPc := pc
  io.retired := retired
  for (i <- 0 until 4) {
    io.laneReq(i).valid := state === sMemory && reqPending(i)
    io.laneReq(i).bits.addr := lsAddr(i)
    io.laneReq(i).bits.write := lsWrite
    io.laneReq(i).bits.wdata := lsWdata(i)
    io.laneReq(i).bits.be := "hf".U
  }

  done := false.B
  when (reset.asBool) {
    state := sIdle
    pc := 0.U
    retired := 0.U
    reqPending := 0.U
    rspPending := 0.U
    for (i <- 0 until 4; r <- 0 until 8) regs(i)(r) := 0.U
  }.otherwise {
    switch (state) {
      is (sIdle) {
        when (io.start) {
          pc := 0.U
          retired := 0.U
          reqPending := 0.U
          rspPending := 0.U
          for (i <- 0 until 4; r <- 0 until 8) regs(i)(r) := 0.U
          state := sRun
        }
      }
      is (sRun) {
        switch (opcode) {
          is (OP_NOP) { pc := pc + 1.U; retired := retired + 1.U }
          is (OP_MOVI) { for (i <- 0 until 4) regs(i)(rd) := immediate; pc := pc + 1.U; retired := retired + 1.U }
          is (OP_LID) { for (i <- 0 until 4) regs(i)(rd) := i.U; pc := pc + 1.U; retired := retired + 1.U }
          is (OP_ADD) { for (i <- 0 until 4) regs(i)(rd) := regs(i)(rs1) + regs(i)(rs2); pc := pc + 1.U; retired := retired + 1.U }
          is (OP_ADDI) { for (i <- 0 until 4) regs(i)(rd) := regs(i)(rs1) + immediate; pc := pc + 1.U; retired := retired + 1.U }
          is (OP_SLLI) { for (i <- 0 until 4) regs(i)(rd) := regs(i)(rs1) << instruction(4, 0); pc := pc + 1.U; retired := retired + 1.U }
          is (OP_SUB) { for (i <- 0 until 4) regs(i)(rd) := regs(i)(rs1) - regs(i)(rs2); pc := pc + 1.U; retired := retired + 1.U }
          is (OP_AND) { for (i <- 0 until 4) regs(i)(rd) := regs(i)(rs1) & regs(i)(rs2); pc := pc + 1.U; retired := retired + 1.U }
          is (OP_OR) { for (i <- 0 until 4) regs(i)(rd) := regs(i)(rs1) | regs(i)(rs2); pc := pc + 1.U; retired := retired + 1.U }
          is (OP_XOR) { for (i <- 0 until 4) regs(i)(rd) := regs(i)(rs1) ^ regs(i)(rs2); pc := pc + 1.U; retired := retired + 1.U }
          is (OP_SRLI) { for (i <- 0 until 4) regs(i)(rd) := regs(i)(rs1) >> instruction(4, 0); pc := pc + 1.U; retired := retired + 1.U }
          is (OP_LOAD, OP_STORE) {
            for (i <- 0 until 4) {
              lsAddr(i) := regs(i)(rs1) + immediate
              lsWdata(i) := regs(i)(rd)
            }
            lsRd := rd
            lsWrite := opcode === OP_STORE
            reqPending := "hf".U
            rspPending := "hf".U
            state := sMemory
          }
          is (OP_HALT) { retired := retired + 1.U; done := true.B; state := sIdle }
        }
      }
      is (sMemory) {
        reqPending := reqRemaining
        rspPending := rspRemaining
        when (!lsWrite) {
          for (i <- 0 until 4) when (responseMask(i)) { regs(i)(lsRd) := io.laneResp(i).bits.data }
        }
        when ((reqRemaining === 0.U) && (rspRemaining === 0.U)) {
          pc := pc + 1.U
          retired := retired + 1.U
          state := sRun
        }
      }
    }
  }
}
