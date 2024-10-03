/**
 * MemCtrl Constructor: MemCtrl(const MemCtrlParams &p);
 * DRAM is accessed by MemInterface* dram in MemCtrlParams
 *
 */

#ifndef __CXL_MEM_CTRL_HH__
#define __CXL_MEM_CTRL_HH__

#include "src/mem/mem_ctrl.hh"

namespace gem5
{

namespace memory
{

class MemInterface;
class DRAMInterface;
class NVMInterface;

} // namespace memory
} // namespace gem5

#endif //__CXL_MEM_CTRL_HH__