import m5
from m5.objects import *

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

system.cpu = X86TimingSimpleCPU()

# System-wide memory bus
system.membus = SystemXBar()

# cache (no cache in this test)
# Connect Memory component: Set request port to respond port
# system.cpu.icache_port = systeml1_cache.cpu_side

# Connect request port with multiple respond ports ()
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

# create an I/O controller on the CPU and connect it to the memory bus
system.cpu.createInterruptController()

# connect PIO and interrupt ports to the memory bus is an x86-specific requirement
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports

# Create memory controller, this script uses DDR3
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Test on SE mode
binary = 'tests/test-progs/hello/bin/x86/linux/hello'
system.workload = SEWorkload.init_compatible(binary)

process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

# instantiate system and begin execution
root = Root(full_system = False, system = system)
m5.instantiate()

print("Beginning Simulation")
exit_event = m5.simulate()

# finish
print('Exiting @ trick {} because {} '.format(m5.curTick(), exit_event.getCause()))



