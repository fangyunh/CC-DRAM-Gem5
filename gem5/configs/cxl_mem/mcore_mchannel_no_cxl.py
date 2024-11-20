import m5
from m5.objects import *
m5.util.addToPath("../")
from common.MemConfig import create_mem_intf, config_mem
from common.FileSystemConfig import config_filesystem
from msi_caches import MyCacheSystem

# Create a simple options class
class OptionsDDR5:
    def __init__(self):
        self.mem_channels = 2
        self.mem_type = 'DDR5_4400_4x8'
        self.mem_ranks = None
        self.enable_dram_powerdown = None
        self.mem_channels_intlv = 1024
        self.xor_low_bit = 20

class OptionsDDR4:
    def __init__(self):
        self.mem_channels = 2
        self.mem_type = 'DDR4_2400_4x16'
        self.mem_ranks = None
        self.enable_dram_powerdown = None
        self.mem_channels_intlv = 2048
        self.xor_low_bit = 20

options = OptionsDDR5()

# Create the system
system = System()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=VoltageDomain())
system.mem_mode = "timing"
system.mem_ranges = [AddrRange('4GB')]

# Create CPUs, caches, and other components
system.cpu = [X86TimingSimpleCPU() for i in range(2)]
for cpu in system.cpu:
    cpu.createInterruptController()

# Create the memory bus
system.membus = SystemXBar()

# Create the Ruby System
system.caches = MyCacheSystem()
system.caches.setup(system, system.cpu, [system.membus])

# Configure memory controllers using config_mem
config_mem(options, system)

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
