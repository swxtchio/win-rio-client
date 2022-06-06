#pragma once
// clang-format off
#include "stdafx.h"
#include <signal.h>
#include <thread>
#include <map>
#include <utility>      // std::pair, std::make_pair
#include <atomic>
#include <algorithm>
#include <memory>
#include "Utilities.hpp"
#include "args.hpp"
#include "StringUtils.hpp"

// clang-format on

namespace riosession {

constexpr bool LARGE_PAGES_ENABLED = false;
constexpr DWORD EXPECTED_DATA_SIZE = 100;     // Expected payload size
constexpr ULONG MAX_PENDING_RECVS = 1500000;  // Choose a multiple of 65536
constexpr ULONG MAX_PENDING_SENDS = 4000;
constexpr DWORD MAX_RIO_RESULTS = 1000;
constexpr DWORD ADDR_SIZE = sizeof(SOCKADDR_INET);
constexpr double REPORT_PERIOD_SEC = 4.0;

#pragma pack(push, 1)
struct ProtocolHeader_t {
    uint16_t Token;
    uint8_t CmdType;
    uint8_t Tag;
    uint64_t Seq;
    uint64_t Timestamp;
};
#pragma pack(pop)

struct McGroupStats_t {
    std::atomic<uint64_t> Packets;
    std::atomic<uint64_t> Bytes;
    std::atomic<uint64_t> Sequence;
    std::atomic<uint64_t> ExpectedSequence;
    std::atomic<uint64_t> OutOfOrder;
    std::atomic<uint64_t> RxDropped;

    McGroupStats_t()
        : Packets(0), Bytes(0), Sequence(0), ExpectedSequence(0), OutOfOrder(0), RxDropped(0) {
    }

    McGroupStats_t(const McGroupStats_t& other) {
        Packets.store(other.Packets.load());
        Bytes.store(other.Bytes.load());
        Sequence.store(other.Sequence.load());
        ExpectedSequence.store(other.ExpectedSequence.load());
        OutOfOrder.store(other.OutOfOrder.load());
        RxDropped.store(other.RxDropped.load());
    }

    McGroupStats_t& operator=(const McGroupStats_t& other) {
        Packets.store(other.Packets.load());
        Bytes.store(other.Bytes.load());
        Sequence.store(other.Sequence.load());
        ExpectedSequence.store(other.ExpectedSequence.load());
        OutOfOrder.store(other.OutOfOrder.load());
        RxDropped.store(other.RxDropped.load());
        return *this;
    }
};

struct TotalStats_t {
    uint64_t TotalPackets = 0;
    uint64_t TotalBytes = 0;
    uint64_t TotalOutOfOrder = 0;
    uint64_t TotalDrops = 0;
};

using McGroupStatsMap = std::map<uint32_t, struct McGroupStats_t>;
using UniqueThread_t = std::unique_ptr<std::thread>;

struct Timing_s {
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point stopTime;

    void setStart() {
        startTime = std::chrono::steady_clock::now();
    }

    void setStop() {
        stopTime = std::chrono::steady_clock::now();
    }

    uint64_t getElapsedTimeMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime).count();
    }

    uint64_t getElapsedTimeSec() {
        return std::chrono::duration_cast<std::chrono::seconds>(stopTime - startTime).count();
    }
};

class RioSession {
   protected:
    SOCKET m_SocketHandle;
    RIO_RQ m_RequestQueue;
    RIO_CQ m_CompletionQueue;
    char* m_RioBuffPtr = nullptr;
    RIO_BUFFERID m_RioBuffId = NULL;
    std::unique_ptr<RIO_BUF[]> m_RioBuffDescr;
    char* m_McAddrBuffPtr = nullptr;
    RIO_BUFFERID m_McAddrBuffId = NULL;
    std::unique_ptr<RIO_BUF[]> m_McAddrDescr;
    RIO_EXTENSION_FUNCTION_TABLE m_RioFuncTable;
    HANDLE m_hIOCP;
    ULONG m_MaxOutstandingReceive;
    ULONG m_MaxReceiveDataBuffers;
    ULONG m_MaxOutstandingSend;
    ULONG m_MaxSendDataBuffers;
    args_t* m_Args;
    Timing_s m_Timing;
    volatile sig_atomic_t* m_ExitSignal;
    McGroupStatsMap m_GroupStats;
    std::atomic_ulong m_TotalPkts;
    UniqueThread_t m_ReportThread;

   protected:
    void CreateSocket(const DWORD flags = 0);
    void InitializeRIO();
    void CloseSocket();
    void BindSocket(uint16_t bindPort, const std::string& bindAddr);
    void ReleaseAndDeregisterBuffer(RIO_BUFFERID& bufferId, char* buffPointer);
    char* AllocateBufferSpace(const DWORD messageSize,
                              const DWORD totalMessages,
                              DWORD& bufferSize,
                              DWORD& receiveBuffersAllocated);
    char* AllocateAndRegisterBuffer(const DWORD messageSize,
                                    const DWORD totalMessages,
                                    RIO_BUFFERID& bufferId);
    void CreateCompletionQueue(DWORD cqSize);
    void CreateRequestQueue();
    void NotifyCompletionQueue();
    void InitGroupStats(const Ipv4Vect& mcastGroupAddr);
    void PrintTimings(ULONGLONG pktsProcessed, ULONGLONG pktsOther);
    virtual void GroupStatsUpdate(const SOCKADDR_INET* addr,
                                  const size_t pktSize,
                                  const ProtocolHeader_t* pHdr)
        = 0;
    virtual void GroupStatsPrint() = 0;
    virtual void InitMcAddrDescriptors() = 0;
    bool ShouldStop();
    void JoinThread(UniqueThread_t& t) const;

   public:
    virtual void Start() = 0;
    RioSession(args_t* args, volatile sig_atomic_t* signal);
    virtual void CleanUpRIO();
    ~RioSession() = default;
};
}  // namespace riosession
