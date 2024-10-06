from m5.params import *
from m5.citations import add_citation
from m5.objects.MemCtrl import *
from m5.proxy import *

class CXLMemCtrl(MemCtrl):
    type = 'CXLMemCtrl'
    cxx_header = 'cxl_mem/cxl_mem_ctrl.hh'
    cxx_class = 'gem5::memory::CXLMemCtrl'

    # Override memory interface parameter to add CXL-specific features
    dram = Param.MemInterface("CXL Memory interface")

    # Tracking average latency statistics in the controller
    total_latency = Param.Latency("0ns", "Total memory latency")
    num_packets = Param.Counter(0, "Number of processed packets")

    # Scheduler policy, similar to MemCtrl
    mem_sched_policy = Param.MemSched("frfcfs", "Memory scheduling policy")

    # Pipeline latency settings
    static_frontend_latency = Param.Latency("10ns", "Static frontend latency")
    static_backend_latency = Param.Latency("10ns", "Static backend latency")

    # Additional configuration for response events
    command_window = Param.Latency("10ns", "Command window latency")

    # Optional: Enable or disable sanity checks, like the port response queue size
    disable_sanity_check = Param.Bool(False, "Disable port resp Q size check")