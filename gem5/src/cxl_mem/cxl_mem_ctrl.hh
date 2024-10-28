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
#include "mem/port.hh"
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
    class CPUPort: public ResponsePort
    {
      public:
        // The object that owns this object
        CXLMemCtrl& ctrl;
        // If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;

        bool needRetry;

        CPUPort(const std::string& name, CXLMemCtrl& _ctrl);

        AddrRangeList getAddrRanges() const override;
        
      protected:

        bool recvTimingReq(PacketPtr pkt) override;
        bool sendTimingResp(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    CPUPort cpu_side_port;

    class MemCtrlPort: public RequestPort
    {
      public:
        // The object that owns this object
        CXLMemCtrl& ctrl;
        // If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;

        bool needRetry;

        MemCtrlPort(const std::string& name, CXLMemCtrl& _ctrl);

        /**
         * Get a list of the non-overlapping address ranges the owner is
         * responsible for. All response ports must override this function
         * and return a populated list with at least one item.
         *
         * @return a list of ranges responded to
         */
        AddrRangeList getAddrRanges() const override;

        // // retry the response if blocked
        // bool retryResp;
      protected:

        void recvReqRetry() override;
        void sendTimingReq(pkt) override;
        bool recvTimingResp(PacketPtr pkt) override;
        void recvRangeChange() override;
    };
    
    MemCtrlPort memctrl_side_port;
    
    // send request
    virtual void processRequestEvent();
    EventFunctionWrapper reqEvent;

    // send response
    virtual void processResponseEvent();
    EventFunctionWrapper respEvent;

    /**
     * Used for debugging to observe the contents of the queues.
     */
    void printQs() const;

    /** Calculate the average latency */
    void calculateAvgLatency();

    /** Store the latency */ 
    std::unordered_map<PacketId, Tick> packetLatency;

    /** statistc for latency */
    struct CXLStats : public statistics::Group
    {
      CXLStats(CXLMemCtrl &cxlmc);

      void regStats() override;

      CXLMemCtrl &cxlmc;

      // Overall statistics
      statistics::Scalar totalLatency;
      statistics::Formula avgLatency;
      statistics::Histogram latencyHistogram;

      // Per-requestor statistics
      statistics::Vector perRequestorLatency;
      statistics::Vector perRequestorAccesses;
      statistics::Formula perRequestorAvgLatency;
    };


    CXLStats stats;

    /** Handle Request */
    bool handleRequest(PacketPtr pkt);

    /** Handle Response */
    bool handleResponse(PacketPtr pkt);

    /** Provide address ranges */
    AddrRangeList getAddrRanges() const;

    bool reqQEmpty()
    {
        return reqQueue.empty();
    }

    bool respQEmpty()
    {
        return respQueue.empty();
    }

    /** Check if response queue is full */
    bool reqQueueFull() const;

    /** Check if request queue is full */
    bool respQueueFull() const;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange();

    Tick prevArrival;
    // True if this is currently blocked waiting for a response.
    bool blocked;

  public:
    uint32_t requestQueueSize;
    uint32_t responseQueueSize;
    /** Request queue */
    std::deque<PacketPtr> respQueue;
    /** Resp queue */ 
    std::deque<PacketPtr> reqQueue;

    CXLMemCtrl(const CXLMemCtrlParams &p);
    virtual void init() override;
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
    ~CXLMemCtrl() {}
};

} // namespace memory
} // namespace gem5

#endif //__CXL_MEM_CTRL_HH__