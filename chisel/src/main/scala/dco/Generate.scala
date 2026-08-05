package dco

import circt.stage.ChiselStage

object Generate {
  def main(args: Array[String]): Unit = {
    val target = if (args.nonEmpty) args(0) else "generated"
    ChiselStage.emitSystemVerilogFile(new DataCoalescingSystem, Array("--target-dir", target))
    ChiselStage.emitSystemVerilogFile(new SimpleGpuSystem, Array("--target-dir", target))
  }
}
