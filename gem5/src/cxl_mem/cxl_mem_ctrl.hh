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
#include "mem/abstract_mem.hh"
#include "mem/port.hh"
#include "mem/qport.hh"
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



/** Design a CXL Packet structure to manage packet */
// class CXLPacket {
//   public:
//     /** When did request enter the controller */
//     const Tick entryTime;

//     /** When will request leave the controller */
//     // Tick readyTime;

//     /** This comes from the outside world */
//     PacketPtr pkt;

//     Addr address;

//     bool is_read;

//     CXLPacket(PacketPtr _pkt, bool _is_read)
//       : entryTime(curTick()), pkt(_pkt), is_read(_is_read) { }
// };
 
// typedef std::deque<CXLPacket*> CXLPacketQueue;


class CXLMemCtrl : public ClockedObject
{
  protected:
    enum BusState { START, READ, WRITE };
    /** Ports */
    class CPUPort: public QueuedResponsePort
    {
      public:
        RespPacketQueue queue;
        // The object that owns this object
        CXLMemCtrl& ctrl;

        CPUPort(const std::string& name, CXLMemCtrl& _ctrl);

        AddrRangeList getAddrRanges() const override;
        
      protected:
        // Support in future
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;

        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    CPUPort cpu_side_ports;

    class MemCtrlPort: public RequestPort
    {
      public:
        // The object that owns this object
        CXLMemCtrl& ctrl;
        // If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;

        MemCtrlPort(const std::string& name, CXLMemCtrl& _ctrl);

        // // retry the response if blocked
        // bool retryResp;
      protected:

        void recvReqRetry() override;
        bool recvTimingResp(PacketPtr pkt) override;
        void recvRangeChange() override;
    };
    
    MemCtrlPort memctrl_side_port;

    virtual bool recvTimingReq(PacketPtr pkt);
    virtual bool recvTimingResp(PacketPtr pkt);
    
    // send request
    virtual void processRequestEvent();
    EventFunctionWrapper reqEvent;

    // send response
    virtual void processResponseEvent();
    EventFunctionWrapper respEvent;

    void recvReqRetry();
    void recvRespRetry();

    /**
     * Handle a packet functionally. Update the data on a write and get the
     * data on a read. Called from CPU port on a recv functional.
     *
     * @param packet to functionally handle
     */
    void handleFunctional(PacketPtr pkt);

    /** Store the latency */ 
    std::unordered_map<PacketId, Tick> packetLatency;
    /** Records it is a read or write packet */
    // std::unordered_map<PacketId, bool> packetProperty;


    /** statistc for latency */
    struct CXLStats : public statistics::Group
    {
      CXLStats(CXLMemCtrl &cxlmc);

      void regStats() override;

      const CXLMemCtrl &cxlmc;

      // Overall statistics
      statistics::Scalar totalLatency;
      statistics::Scalar totalReadLatency;
      statistics::Scalar totalWriteLatency;
      statistics::Scalar totGap;
      /** Latency of read accessing DRAM */
      statistics::Scalar totalDRAMReadLatency;

      statistics::Scalar totalPacketsNum;
      statistics::Scalar totalReadPacketsNum;

      /** Latency that extract 64B from 2KB */
      statistics::Scalar totalReadCopyLatency;

      /** Packets number of read request to DRAM */
      statistics::Scalar totalDRAMReadPacketsNum;
      statistics::Scalar totalNonDRAMReadPacketsNum;
      statistics::Scalar totalWritePacketsNum;
      statistics::Scalar totalPacketsSize;
      statistics::Scalar totalCompressedPacketsNum;
      statistics::Scalar totalCompressionTimes;
      statistics::Scalar totalReadPacketsSize;
      statistics::Scalar totalWritePacketsSize;
      statistics::Scalar totalCompressedPacketsSize;
      
      statistics::Formula avgRdBWSys;
      statistics::Formula avgWrBWSys;

      statistics::Histogram latencyHistogram;
      statistics::Histogram readLatencyHistogram;
      statistics::Histogram writeLatencyHistogram;
      statistics::Histogram compressedSizeHistogram;

      statistics::Formula avgLatency;
      statistics::Formula avgReadLatency;
      statistics::Formula avgWriteLatency;
      statistics::Formula avgCompressedSize;
      statistics::Formula avgDRAMReadLatency;
      statistics::Formula avgReadCopyLatency;
    };


    CXLStats stats;

    /** Send respond */
    void accessAndRespond(PacketPtr pkt, Tick static_latency);

    /** Seek required packets in write queue or not */
    bool findInWriteQueue(PacketPtr pkt);

    /**
     * Get a list of the non-overlapping address ranges the owner is
     * responsible for. All response ports must override this function
     * and return a populated list with at least one item.
     *
     * @return a list of ranges responded to
     */
    virtual AddrRangeList getAddrRanges();

    bool readQEmpty()
    {
        return readQueue.empty();
    }

    bool writeQEmpty()
    {
        return writeQueue.empty();
    }

    bool respQEmpty()
    {
        return respQueue.empty();
    }

    /** Check if read queue is full */
    bool readQueueFull() const;

    /** Check if write queue is full */
    bool writeQueueFull() const;

    /** Check if resp queue is full */
    bool respQueueFull() const;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange();

    /** 
     * LZ4 compression, return the compression sizes in the best granularity
     */
    std::vector<unsigned int> LZ4Compression();
  
    /**
     * move the assigned size of packets in a buffer, prepared to be compressed
     */
    void fillSourceBuffer(char* srcBuffer, int startIndex, int packetsToProcess,
                             int packetCount);

    /** 
     * Compress the 4KB data in given granularity, 
     * return compressed sizes. e.g 4 * 1KB, 2* 2KB, 4KB 
     */
    std::vector<unsigned int> DynamicCompression(int blockSizeInKB);

    /** 
     * select the most appropriate compression granularity by considering the
     * trade off of access latency and compression ratio. More compression ratio 
     * means we can get more storage in DRAM, but we also get longer access latency
     * because we need to read larger size from DRAM.
     */
    std::vector<unsigned int> CompressionSelectedSize(); 

    /** Handle read request for compressed data block */
    void handleReadRequest(PacketPtr pkt);
    
    // Mapping for compressed block
    std::unordered_map<PacketPtr, PacketPtr> compressedReadMap;
    // metadata of compressed size
    std::unordered_map<Addr, unsigned int> compressedBlockSizes;

    const unsigned blockSize;

    // retry to receive req
    bool retryRdReq;
    bool retryWrReq;

    // Need resend the packet to downside mem ctrl
    bool resendReq;

    // loss to receive resp from memory ctrl
    bool resendMemResp;

    // fail to send resp to CPU
    bool retryMemResp;

    Tick prevArrival;

    /**
     * Pipeline latency of the controller frontend. The frontend
     * contribution is added to writes (that complete when they are in
     * the write buffer) and reads that are serviced the write buffer.
     */
    const Tick frontendLatency;

    /**
     * Pipeline latency of the backend and PHY. Along with the
     * frontend contribution, this latency is added to reads serviced
     * by the memory.
     */
    const Tick backendLatency;

    const Tick delay;

    // number of packet to be compressed
    const unsigned writePktThreshold;

    // state of sending read or write request
    BusState RWState;
    BusState nextRWState;

    // number of already compressed packet
    unsigned cmpedPkt;

    // compressed sizes
    std::vector<unsigned int> cmpBlockSizes;

    // Drain state check
    DrainState drain() override;
    // Marks that no packets will send by CPU
    bool goDraining;

  public:
    uint32_t readQueueSize;
    uint32_t writeQueueSize;
    uint32_t responseQueueSize;

    /** Request queue */
    std::deque<PacketPtr> readQueue;
    std::deque<PacketPtr> writeQueue;
    /** Resp queue */ 
    std::deque<PacketPtr> respQueue;

    CXLMemCtrl(const CXLMemCtrlParams &p);
    virtual void init() override;
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
    ~CXLMemCtrl() {}
};
  
} // namespace memory
} // namespace gem5

#endif //__CXL_MEM_CTRL_HH__