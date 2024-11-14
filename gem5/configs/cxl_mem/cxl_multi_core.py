
# import the m5 (gem5) library created when gem5 is built
import m5

# import all of the SimObjects
from m5.objects import *

# Needed for running C++ threads
m5.util.addToPath("../")
from common.FileSystemConfig import config_filesystem

# You can import ruby_caches_MI_example to use the MI_example protocol instead
# of the MSI protocol
from msi_caches import MyCacheSystem

# create the system we are going to simulate
system = System()

# Set the clock frequency of the system (and all of its children)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system
system.mem_mode = "timing"  # Use timing accesses
system.mem_ranges = [AddrRange("2GB")]  # Create an address range

# Create a pair of simple CPUs
system.cpu = [X86TimingSimpleCPU() for i in range(2)]

# Instantiate the CXL Memory Controller
system.cxl_mem_ctrl = CXLMemCtrl(
    read_buffer_size=64,
    write_buffer_size=128,
    response_buffer_size=64
)


# Create a DDR3 memory controller and connect it to the membus
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR5_4400_4x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]

# create the interrupt controller for the CPU and connect to the membus
for cpu in system.cpu:
    cpu.createInterruptController()

# Create the Ruby System
system.caches = MyCacheSystem()
system.caches.setup(system, system.cpu, [system.cxl_mem_ctrl])

# Instantiate the memory bus
system.membus = SystemXBar()

# Connect CXL memory controller to DRAM XBar for multi-channels
system.cxl_mem_ctrl.memctrl_side_port = system.membus.cpu_side_ports

# Connect DRAM XBar to DRAM Memory controller
system.mem_ctrl.port = system.membus.mem_side_ports

# Run application and use the compiled ISA to find the binary
# grab the specific path to the binary
thispath = os.path.dirname(os.path.realpath(__file__))
binary = os.path.join(
    thispath,
    "../../",
    "tests/test-progs/threads/bin/x86/linux/threads",
)

# Create a process for a simple "multi-threaded" application
process = Process()
# Set the command
# cmd is a list which begins with the executable (like argv)
process.cmd = [binary]
# Set the cpu to use the process as its workload and create thread contexts
for cpu in system.cpu:
    cpu.workload = process
    cpu.createThreads()

system.workload = SEWorkload.init_compatible(binary)

# Set up the pseudo file system for the threads function above
config_filesystem(system)

# set up the root SimObject and start the simulation
root = Root(full_system=False, system=system)
# instantiate all of the objects we've created above
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
