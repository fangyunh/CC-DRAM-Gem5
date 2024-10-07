/**
 * MemCtrl Constructor: MemCtrl(const MemCtrlParams &p);
 * DRAM is accessed by MemInterface* dram in MemCtrlParams
 * MemCtrl::recvTimingReq(PacketPtr pkt)
 * MemCtrl::CtrlStats Struct may contains some stat for latency
 * Here is a static latency in accessAndRespond
 * Our latency is calculated from receives the request and ends at 
 * response the CPU
 */

#ifndef __CXL_MEM_CTRL_HH__
#define __CXL_MEM_CTRL_HH__

#include "base/callback.hh"
#include "base/statistics.hh"
#include "enums/MemSched.hh"
#include "params/CXLMemCtrl.hh"
#include "mem/mem_ctrl.hh"
#include "sim/eventq.hh"

namespace gem5
{

namespace memory
{

class MemInterface;
class DRAMInterface;
class NVMInterface;

class CXLMemCtrl : public MemCtrl
{
  protected:
    // Override recvTimingReq() to capture the start time of request processing
    bool recvTimingReq(PacketPtr pkt) override;

    // Override accessAndRespond() to capture end time
    void accessAndRespond(PacketPtr pkt, Tick static_latency,
                          MemInterface* mem_intr) override;

    // Override processRespondEvent() to present average latency
    void processNextReqEvent(MemInterface* mem_intr,
                          MemPacketQueue& resp_queue,
                          EventFunctionWrapper& resp_event,
                          EventFunctionWrapper& next_req_event,
                          bool& retry_wr_req) override;
    
    // Calculate the average latency
    void calculateAvgLatency();

    // Store the packets entry time to queue
    std::unordered_map<PacketId, Tick> packetEntryTime;

    // Store the latency
    std::unordered_map<PacketId, Tick> packetLatency;

    Tick totalLatency;
    int numPackets;
  public:
    CXLMemCtrl(const CXLMemCtrlParams &p);

    ~CXLMemCtrl() {}
}

} // namespace memory
} // namespace gem5

#endif //__CXL_MEM_CTRL_HH__