from m5.params import *
from m5.citations import add_citation
from m5.objects.ClockedObject import *
from m5.proxy import *

class CXLMemCtrl(ClockedObject):
    type = 'CXLMemCtrl'
    cxx_header = 'cxl_mem/cxl_mem_ctrl.hh'
    cxx_class = 'gem5::memory::CXLMemCtrl'

    # Port connects with CPU
    cpu_side_port = ResponsePort("Port connected to CPU")

    # Port connects with Memory Controller
    memctrl_side_port = ResponsePort("Port connected to MemCtrl")

    # Buffer size of read and write queue
    request_buffer_size = Param.Unsigned(32, "Request queue size")
    response_buffer_size = Param.Unsigned(32, "Response queue size")

    


    