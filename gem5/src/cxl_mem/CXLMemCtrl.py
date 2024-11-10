from m5.params import *
from m5.citations import add_citation
from m5.objects.ClockedObject import *
from m5.proxy import *

class CXLMemCtrl(ClockedObject):
    type = 'CXLMemCtrl'
    cxx_header = 'cxl_mem/cxl_mem_ctrl.hh'
    cxx_class = 'gem5::CXLMemCtrl'

    # Port connects with CPU
    cpu_side_port = ResponsePort("Port connected to CPU")

    # Ports connect with Memory Controllers
    memctrl_side_port = RequestPort("Ports connected to MemCtrls")

    # Buffer size of read and write queue
    read_buffer_size = Param.Unsigned(64, "Read Request queue size")
    write_buffer_size = Param.Unsigned(128, "Write Request queue size")
    response_buffer_size = Param.Unsigned(64, "Response queue size")

    # Number of write packets to be compressed
    # default 16 means 16 * 4 * 64B = 4KB
    write_pkt_threshold = Param.Unsigned(64, "Number of write packets to be compressed")

    # pipeline latency of the controller and PHY, split into a
    # frontend part and a backend part, with reads and writes serviced
    # by the queues only seeing the frontend contribution, and reads
    # serviced by the memory seeing the sum of the two
    static_frontend_latency = Param.Latency("8ns", "Static frontend latency")
    static_backend_latency = Param.Latency("8ns", "Static backend latency")

    # delay for recheck the queues
    delay = Param.Latency("6ns", "Static delay of waiting read queue")
    