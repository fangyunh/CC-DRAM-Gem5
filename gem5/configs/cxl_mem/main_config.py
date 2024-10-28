import m5
from m5.objects import *
from caches import *
import argparse

# Parent of all other objects in the system
system = System()

# Clock and voltage domain setup
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# Memory configuration
system.mem_mode = 'timing'
total_memory = '4GB'
num_channels = 4  # Number of DRAM channels
per_channel_mem = int(m5.eval(total_memory)) // num_channels
system.mem_ranges = [AddrRange(per_channel_mem) for _ in range(num_channels)]

# Instantiate the CPU
system.cpu = X86TimingSimpleCPU()

# Cache Parameter Configuration in Command Line
parser = argparse.ArgumentParser(description='A simple system with 3-level cache.')
parser.add_argument("binary", default="", nargs="?", type=str, help="Path to binary to execute")
parser.add_argument("--l1i_size", help=f"L1 instruction cache size. Default: 16kB.")
parser.add_argument("--l1d_size", help="L1 data cache size. Default: 64kB.")
parser.add_argument("--l2_size", help="L2 cache size. Default 256kB.")
options = parser.parse_args()

# Define the workload (replace with your actual binary path)
binary = "tests/test-progs/hello/bin/x86/linux/hello"  # Example binary path
system.workload = SEWorkload.init_compatible(options.binary)

# Create and assign a process to the CPU
process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

# Instantiate L1 Instruction and Data Caches
system.cpu.icache = L1ICache(options)
system.cpu.dcache = L1DCache(options)
system.cpu.icache.connectCPU(system.cpu)
system.cpu.dcache.connectCPU(system.cpu)

# Create an L2 bus and connect L1 caches to it
system.l2bus = L2XBar()
system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectBus(system.l2bus)

# Instantiate and connect the L2 Cache
system.l2cache = L2Cache(options)
system.l2cache.connectCPUSideBus(system.l2bus)

# Instantiate the CXL Memory Controller with multiple channels
system.cxl_mem_ctrl = CXLMemCtrl(
    request_buffer_size=32,
    response_buffer_size=32
)

# Connect L2 cache to the CXL memory controller
system.l2cache.mem_side = system.cxl_mem_ctrl.cpu_side_port

# Instantiate the memory bus
system.membus = SystemXBar()

# Connect CXL memory controller to DRAM XBar for multi-channels
system.cxl_mem_ctrl.memctrl_side_port = system.membus.cpu_side_ports

# Create an interrupt controller and connect it to the memory bus
system.cpu.createInterruptController()

# Connect PIO and interrupt ports to the memory bus (x86-specific requirement)
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# Instantiate multiple DDR4 memory controllers for each channel
system.mem_ctrls = []
for i in range(num_channels):
    mem_ctrl = MemCtrl()
    mem_ctrl.dram = DDR4_2400_8x8()
    mem_ctrl.dram.range = system.mem_ranges[i]
    mem_ctrl.port = system.membus.mem_side_ports
    system.mem_ctrls.append(mem_ctrl)

# Below is single channel config
# Create a DDR4 memory controller and connect it to the membus
# system.mem_ctrl = MemCtrl()
# A single DDR4-2400 x64 channel (one command and address bus), with
# timings based on a DDR4-2400 8 Gbit datasheet (Micron MT40A1G8)
# in an 8x8 configuration.
# Total channel capacity is 16GiB
# 8 devices/rank * 2 ranks/channel * 1GiB/device = 16GiB/channel
# system.mem_ctrl.dram = DDR4_2400_8x8()
# system.mem_ctrl.dram.range = system.mem_ranges[0]
# system.mem_ctrl.port = system.membus.mem_side_ports

# Connect the system up to the membus
system.system_port = system.membus.cpu_side_ports

# Instantiate the Root object and begin simulation
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning Simulation")
exit_event = m5.simulate()

# Finish the simulation and print the exit cause
print('Exiting @ tick {} because {}'.format(m5.curTick(), exit_event.getCause()))
