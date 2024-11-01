import m5
from m5.objects import *

# Create the system
system = System()

# Clock and voltage domain setup
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system (single)
system.mem_mode = "timing"  # Use timing accesses
system.mem_ranges = [AddrRange("512MB")]  # Create an address range

# Memory configuration
# system.mem_mode = 'timing'
# total_memory = '32GiB'
# num_channels = 4  # Number of DRAM channels
# per_channel_mem = '8GiB'

# # Create separate memory ranges for each channel
# system.mem_ranges = [AddrRange(start=Addr('8GiB') * i, size=per_channel_mem) for i in range(num_channels)]

# Instantiate the CPU
system.cpu = X86TimingSimpleCPU()

# Create a simple cache
system.cache = SimpleCache(size="1kB")
# Connect the I and D cache ports of the CPU to the memobj.
# Since cpu_side is a vector port, each time one of these is connected, it will
# create a new instance of the CPUSidePort class
system.cpu.icache_port = system.cache.cpu_side
system.cpu.dcache_port = system.cache.cpu_side

# Instantiate the CXL Memory Controller with multiple channels
system.cxl_mem_ctrl = CXLMemCtrl(
    request_buffer_size=32,
    response_buffer_size=32
)

# Connect L2 cache to the CXL memory controller
system.cache.mem_side = system.cxl_mem_ctrl.cpu_side_port

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

# single channel
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Instantiate and add each DDR4 memory controller individually
# for i in range(num_channels):
#     mem_ctrl = MemCtrl()
#     mem_ctrl.dram = DDR3_1600_8x8()
#     mem_ctrl.dram.range = system.mem_ranges[i]
#     mem_ctrl.port = system.membus.mem_side_ports
    
#     # Assign each memory controller to a unique attribute in the system
#     setattr(system, f"mem_ctrl{i}", mem_ctrl)

# Connect the system up to the memory bus
system.system_port = system.membus.cpu_side_ports

# Create a process for a simple "Hello World" application
process = Process()
# Set the command
# grab the specific path to the binary
thispath = os.path.dirname(os.path.realpath(__file__))
binpath = os.path.join(
    thispath, "../../", "tests/test-progs/hello/bin/x86/linux/hello"
)
# cmd is a list which begins with the executable (like argv)
process.cmd = [binpath]
# Set the cpu to use the process as its workload and create thread contexts
system.cpu.workload = process
system.cpu.createThreads()

system.workload = SEWorkload.init_compatible(binpath)

# Instantiate the Root object and begin simulation
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning Simulation")
exit_event = m5.simulate()

# Finish the simulation and print the exit cause
print('Exiting @ tick {} because {}'.format(m5.curTick(), exit_event.getCause()))
