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
#include "base/trace.hh"
#include "base/types.hh"
#include "base/compiler.hh"
#include "base/statistics.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "mem/qport.hh"
#include "enums/MemSched.hh"
#include "params/CXLMemCtrl.hh"
#include "sim/clocked_object.hh"
#include "sim/cur_tick.hh"
#include "sim/system.hh"
#include "sim/eventq.hh"

#include <deque>
#include <unordered_map>
#include <vector>

namespace gem5
{

namespace memory
{


// /** Design a CXL Packet structure to manage packet */
// class CXLPacket {
//   public:
//     /** When did request enter the controller */
//     const Tick entryTime;

//     /** When will request leave the controller */
//     Tick readyTime;

//     /** This comes from the outside world */
//     const PacketPtr pkt;

//     CXLPacket(PacketPtr _pkt, bool is_read)
//       : entryTime(curTick()), readyTime(curTick()), pkt(_pkt) { }
// };
 
// typedef std::deque<CXLPacket*> CXLPacketQueue;
 

class CXLMemCtrl : public ClockedObject
{
  protected:
    /** Ports */
    class CPUPort: public QueuedResponsePort
    {
        RespPacketQueue queue;
        CXLMemCtrl& ctrl;
      
      public:
        
        CPUPort(const std::string& name, CXLMemCtrl& _ctrl);
      
      protected:

        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    CPUPort cpu_side_port;

    class MemCtrlPort: public QueuedRequestPort
    {
        ReqPacketQueue queue;
        CXLMemCtrl& ctrl;

      public:

        MemCtrlPort(const std::string& name, CXLMemCtrl& _ctrl);

      protected:

        void recvReqRetry() override;
        bool recvTimingResp(PacketPtr pkt) override;
        bool sendTimingReq(PacketPtr pkt);
    };

    MemCtrlPort mem_ctrl_side_port;

    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
    
    /**
     * Determine if there is a packet that can issue.
     *
     * @param pkt The packet to evaluate
     */
    virtual bool packetReady(MemPacket* pkt, MemInterface* mem_intr);

    /**
     * Used for debugging to observe the contents of the queues.
     */
    void printQs() const;

    /** Calculate the average latency */
    void calculateAvgLatency();

    /** Store packet's start tick */
    std::unordered_map<PacketId, Tick> packetInitialTick;

    /** Store the latency */ 
    std::unordered_map<PacketId, Tick> packetLatency;

    /** Request queue */
    std::deque<PacketPtr*> reqQueue;

    /**
     * Response queue where read packets wait after we're done working
     * with them, but it's not time to send the response yet. The
     * responses are stored separately mostly to keep the code clean
     * and help with events scheduling. For all logical purposes such
     * as sizing the read queue, this and the main read queue need to
     * be added together.
     */
    std::deque<PacketPtr*> respQueue;

    uint32_t requestQueueSize;
    uint32_t responseQueueSize;

    /** statistc for latency */
    struct CXLStats : public statistics::Group
    {
        CXLStats(CXLMemCtrl &cxlmc);

        void regStats() override;

        CXLMemCtrl &cxlmc;

        // Statistics that model needs to capture
        statistics::Scalar totalLatency;


    };

    CXLStats stats;

    virtual bool respQEmpty()
    {
        return respQueue.empty();
    }

     /** Handle timing requests */
    virtual bool recvTimingReq(PacketPtr pkt);

    /** Handle timing responses */
    virtual bool recvTimingResp(PacketPtr pkt);

    /** Handle retries */
    virtual void recvRetry();

    /** Provide address ranges */
    AddrRangeList getAddrRanges() const;

    Tick prevArrival;

  public:
    CXLMemCtrl(const CXLMemCtrlParams &p);
    virtual void init() override;
    ~CXLMemCtrl() {}
};

} // namespace memory
} // namespace gem5

#endif //__CXL_MEM_CTRL_HH__