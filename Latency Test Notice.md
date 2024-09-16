# Latency Test Notice

ISA: Different ISA

CPU: in-order (X86MinorCPU) & out-of-order (X8603CPU)

All gem5 BaseCPU’s take the naming format `{ISA}{Type}CPU`. Ergo, if we wanted a RISCV Minor CPU we’d use `RiscvMinorCPU`.

The Valid ISAs are:

- Riscv
- Arm
- X86
- Sparc
- Power
- Mips

The CPU types are:

- AtomicSimpleCPU
- O3CPU
- TimingSimpleCPu
- KvmCPU
- MinorCPU

DDR: DDR3, DDR4

