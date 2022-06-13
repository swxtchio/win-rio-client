#pragma comment(lib, "ws2_32.lib")

#include <WS2tcpip.h>

#include <iostream>

#include <deque>
#include <string>
#include <process.h>
#include <winerror.h>

#include "..\RIOIOCPUDP\args.hpp"

using std::cout;
using std::deque;
using std::endl;
using std::string;

RIO_EXTENSION_FUNCTION_TABLE g_rio;
RIO_CQ g_queue = 0;
RIO_RQ g_requestQueue = 0;

HANDLE g_hIOCP = 0;

HANDLE g_hStartedEvent = 0;
HANDLE g_hStoppedEvent = 0;

HANDLE g_hReadsToProcessEvent = 0;
HANDLE g_hShutdownReaderThreadEvent = 0;

SOCKET g_s;

volatile unsigned long long g_packets = 0;

LARGE_INTEGER g_frequency;
LARGE_INTEGER g_startCounter;
LARGE_INTEGER g_stopCounter;

typedef std::deque<HANDLE> Threads;

HANDLE g_hReaderThread = 0;

Threads g_threads;

long g_workIterations = 0;

volatile long g_pendingRecvs = 0;

typedef std::deque<RIO_BUFFERID> BufferList;

BufferList g_buffers;


CRITICAL_SECTION g_criticalSection;

struct ThreadData {
    ThreadData()
        : threadId(0),
          packetsProcessed(0),
          minPacketsProcessed(std::numeric_limits<size_t>::max()),
          maxPacketsProcessed(0),
          notifyCalled(0),
          dequeueCalled(0) {
    }

    DWORD threadId;

    size_t packetsProcessed;
    size_t minPacketsProcessed;
    size_t maxPacketsProcessed;
    size_t notifyCalled;
    size_t dequeueCalled;
};

ThreadData g_threadData[NUM_IOCP_THREADS];

struct EXTENDED_RIO_BUF : public RIO_BUF {
    DWORD operation;

    EXTENDED_RIO_BUF* pNext;
};


volatile PVOID g_pReadList = 0;

struct EXTENDED_OVERLAPPED : public OVERLAPPED {
    WSABUF buf;
};

unsigned int __stdcall ThreadFunction(void* pV);

unsigned int __stdcall ReaderThreadFunction(void* pV);

inline string GetLastErrorMessage(DWORD last_error, bool stripTrailingLineFeed = true) {
    CHAR errmsg[512];

    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, last_error,
                        0, errmsg, 511, NULL)) {
        // if we fail, call ourself to find out why and return that error

        const DWORD thisError = ::GetLastError();

        if (thisError != last_error) {
            return GetLastErrorMessage(thisError, stripTrailingLineFeed);
        } else {
            // But don't get into an infinite loop...

            return "Failed to obtain error string";
        }
    }

    if (stripTrailingLineFeed) {
        const size_t length = strlen(errmsg);

        if (errmsg[length - 1] == '\n') {
            errmsg[length - 1] = 0;

            if (errmsg[length - 2] == '\r') {
                errmsg[length - 2] = 0;
            }
        }
    }

    return errmsg;
}

inline void ErrorExit(const char* pFunction, const DWORD lastError) {
    cout << "Error: " << pFunction << " failed: " << lastError << endl;
    cout << GetLastErrorMessage(lastError) << endl;

    exit(0);
}

inline void ErrorExit(const char* pFunction) {
    const DWORD lastError = ::GetLastError();

    ErrorExit(pFunction, lastError);
}


//**************************************************************
//**************************************************************


ULONG g_otherPkts = 0;


RIO_BUFFERID g_recvBufferId;
RIO_BUFFERID g_sendBufferId;
RIO_BUFFERID g_addrLocBufferId;
RIO_BUFFERID g_addrRemBufferId;

char* g_recvBufferPointer = NULL;
char* g_sendBufferPointer = NULL;
char* g_addrLocBufferPointer = NULL;
char* g_addrRemBufferPointer = NULL;

EXTENDED_RIO_BUF* g_recvRioBufs = NULL;
EXTENDED_RIO_BUF* g_sendRioBufs = NULL;
EXTENDED_RIO_BUF* g_addrLocRioBufs = NULL;
EXTENDED_RIO_BUF* g_addrRemRioBufs = NULL;

__int64 g_recvRioBufIndex = 0;
__int64 g_sendRioBufIndex = 0;
__int64 g_addrLocRioBufIndex = 0;
__int64 g_addrRemRioBufIndex = 0;

DWORD g_recvRioBufTotalCount = 0;
DWORD g_sendRioBufTotalCount = 0;
DWORD g_addrLocRioBufTotalCount = 0;
DWORD g_addrRemRioBufTotalCount = 0;



// ######################################
// ## PACKED STRUCTURES FROM HERE DOWN ##
// ######################################
#pragma pack(push, 1)

struct ProtocolHeader_t {
    uint16_t Token;
    uint8_t CmdType;
    uint8_t Tag;
    uint64_t Seq;
    uint64_t Timestamp;
};

/**
 * IPv4 Header
 */
struct ipv4_hdr_t {
    uint8_t version_ihl;      /**< version and header length */
    uint8_t type_of_service;  /**< type of service */
    uint16_t total_length;    /**< length of packet */
    uint16_t packet_id;       /**< packet ID */
    uint16_t fragment_offset; /**< fragmentation offset */
    uint8_t time_to_live;     /**< time to live */
    uint8_t next_proto_id;    /**< protocol ID */
    uint16_t hdr_checksum;    /**< header checksum */
    uint32_t src_addr;        /**< source address */
    uint32_t dst_addr;        /**< destination address */
    inline int HdrLen() const {
        return ((int)((std::byte)version_ihl & (std::byte)0xF) * 4);
    }
};

/**
 * UDP Header
 */
struct udp_hdr_t {
    uint16_t src_port;    /**< UDP source port. */
    uint16_t dst_port;    /**< UDP destination port. */
    uint16_t dgram_len;   /**< UDP datagram length */
    uint16_t dgram_cksum; /**< UDP datagram checksum */
};


SOCKADDR_INET address;
char str[INET_ADDRSTRLEN];
ProtocolHeader_t* pHdr;
udp_hdr_t* pUdp;
SOCKADDR_INET* pAddLoc;
SOCKADDR_INET* pAddRem;

void printTime(const uint64_t Timestamp) {
    std::time_t temp = Timestamp / 1000000000ULL;  // to seconds
    std::tm t;
    gmtime_s(&t, &temp);
    std::cout << std::put_time(&t, "%Y-%m-%d %H:%M:%S %z");
}

void ShowHdr(const ProtocolHeader_t* pHdr) {
    cout << endl
         << pHdr->Token << "\t" << pHdr->CmdType << "\t" << pHdr->Seq << "\t" << pHdr->Tag << "\t";
    printTime(pHdr->Timestamp);
}

void ShowAddr(const SOCKADDR_INET* addr) {
    cout << "\t" << inet_ntop(AF_INET, &(addr->Ipv4.sin_addr), str, INET_ADDRSTRLEN)
         << " \t " << ntohs(addr->Ipv4.sin_port);
}

void RioCleanUp() {
    g_rio.RIOCloseCompletionQueue(g_queue);
    
    if (g_recvBufferPointer != nullptr) {
        g_rio.RIODeregisterBuffer(g_recvBufferId);
        if (0 == VirtualFreeEx(GetCurrentProcess(), g_recvBufferPointer, 0, MEM_RELEASE)) {
            ErrorExit("Error deAllocating the buffer");
        }
    }

    if (g_addrLocBufferPointer != nullptr) {
        g_rio.RIODeregisterBuffer(g_addrLocBufferId);
        if (0 == VirtualFreeEx(GetCurrentProcess(), g_addrLocBufferPointer, 0, MEM_RELEASE)) {
            ErrorExit("Error deAllocating the buffer");
        }
    }

    if (g_addrRemBufferPointer != nullptr) {
        g_rio.RIODeregisterBuffer(g_addrRemBufferId);
        if (0 == VirtualFreeEx(GetCurrentProcess(), g_addrRemBufferPointer, 0, MEM_RELEASE)) {
            ErrorExit("Error deAllocating the buffer");
        }
    }

    if (g_sendBufferPointer != nullptr) {
        g_rio.RIODeregisterBuffer(g_sendBufferId);
        if (0 == VirtualFreeEx(GetCurrentProcess(), g_sendBufferPointer, 0, MEM_RELEASE)) {
            ErrorExit("Error deAllocating the buffer");
        }
    }
}

// STATS

struct StatsMcGroupStats_t {
    uint64_t Packets;
    uint64_t Bytes;
    uint64_t Sequence;
    uint64_t OutOfSequence;
};

using StatsMcGroupStatsMap = std::map<uint32_t, struct StatsMcGroupStats_t>;

StatsMcGroupStatsMap g_mcGroupMap;

void GroupStatsMapInit(args_t args) {
    for (const auto mcAddr : args.McastAddrStr) {
        g_mcGroupMap.insert(
            std::pair<uint32_t, StatsMcGroupStats_t>(mcAddr.ipNetOrder(), StatsMcGroupStats_t{}));
    }
}

void GroupStatsMapReset() {
    for (StatsMcGroupStatsMap::iterator itr = g_mcGroupMap.begin(); itr != g_mcGroupMap.end(); ++itr) {
        itr->second.Packets = 0;
        itr->second.Bytes = 0;
        itr->second.Sequence = 0;
        itr->second.OutOfSequence = 0;
    }
}

uint64_t GroupStatsMapTotalOOO() {
    uint64_t total = 0;
    for (StatsMcGroupStatsMap::iterator itr = g_mcGroupMap.begin(); itr != g_mcGroupMap.end();
         ++itr) {
        total += itr->second.OutOfSequence;
    }
    return total;
}

void GroupStatsMapUpdate(const SOCKADDR_INET* addr,
                         const size_t pktSize,
                         const ProtocolHeader_t* pHdr) {
    static bool first = true;
    uint32_t mcGroup = addr->Ipv4.sin_addr.S_un.S_addr;

    StatsMcGroupStatsMap::iterator itr = g_mcGroupMap.find(mcGroup);
    if (itr == g_mcGroupMap.end()) {
        StatsMcGroupStats_t st_aux{};
        g_mcGroupMap.insert(std::pair<uint32_t, StatsMcGroupStats_t>(mcGroup, st_aux));
    }

    StatsMcGroupStats_t& gmc = g_mcGroupMap[mcGroup];

    if (pHdr->Seq == 0LL) {
        gmc.Packets = 0;
        gmc.Bytes = 0;
        gmc.Sequence = 0;
        gmc.OutOfSequence = 0;
    }
    if (gmc.Sequence < pHdr->Seq) {
        gmc.Sequence = pHdr->Seq;
    }
    else {
        gmc.OutOfSequence++;

        if (first) {
            cout << "First OutOfOrder Seq.: " << gmc.Sequence << endl;
            first = false;
        }
    }
    gmc.Packets++;
    gmc.Bytes += pktSize;
}

void GroupStatsMapPrint() {
    cout << "\n  Group           Packets        Bytes      Last Seq   OutOfOrder " << endl;
    cout << "--------------------------------------------------------------------" << endl;

    for (StatsMcGroupStatsMap::iterator itr = g_mcGroupMap.begin(); itr != g_mcGroupMap.end();
         ++itr) {
        cout << inet_ntop(AF_INET, &itr->first, str, INET_ADDRSTRLEN)
        << "\t" << std::dec 
        << std::setw(10) << itr->second.Packets << "  "
        << std::setw(12) << itr->second.Bytes << "  "
        << std::setw(10) << itr->second.Sequence << "  "
        << std::setw(10) << itr->second.OutOfSequence << endl;
    }

    cout << "--------------------------------------------------------------------" << endl;
    cout << "Total                                                 " <<
            std::setw(10) << GroupStatsMapTotalOOO() << endl;
    cout << endl;
}


void BindSocket(SOCKET s, uint16_t bindPort, const std::string& bindAddr) {
    sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(bindPort);
    if (!bindAddr.empty()) {
        auto res = inet_pton(AF_INET, bindAddr.c_str(), &addr.sin_addr.s_addr);
        if (res != 1) {
            RioCleanUp();
            ErrorExit("Error converting the specified address to bind");
        }

    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (SOCKET_ERROR
        == ::bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
        RioCleanUp();
        ErrorExit("Error during socket binding process");
    }
}


//-----

template <typename TV, typename TM>
inline TV RoundDown(TV Value, TM Multiple) {
    return ((Value / Multiple) * Multiple);
}

template <typename TV, typename TM>
inline TV RoundUp(TV Value, TM Multiple) {
    return (RoundDown(Value, Multiple) + (((Value % Multiple) > 0) ? Multiple : 0));
}

inline void SetupTiming(const char* pProgramName, const bool lockToThreadForTiming = true) {
    if (lockToThreadForTiming) {
        HANDLE hThread = ::GetCurrentThread();

        if (0 == ::SetThreadAffinityMask(hThread, TIMING_THREAD_AFFINITY_MASK)) {
            ErrorExit("SetThreadAffinityMask");
        }
    }

    if (!::QueryPerformanceFrequency(&g_frequency)) {
        ErrorExit("QueryPerformanceFrequency");
    }
}

inline void InitialiseWinsock() {
    WSADATA data;

    WORD wVersionRequested = 0x202;

    if (0 != ::WSAStartup(wVersionRequested, &data)) {
        ErrorExit("WSAStartup");
    }

    if (USE_LARGE_PAGES) {
        // check that we have SeLockMemoryPrivilege and enable it

        ErrorExit("TODO - USE_LARGE_PAGES");
    }
}

inline SOCKET CreateSocket(const DWORD flags = 0) {
    g_s = ::WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, flags);

    if (g_s == INVALID_SOCKET) {
        ErrorExit("WSASocket");
    }

    return g_s;
}

inline HANDLE CreateIOCP() {
    g_hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

    if (0 == g_hIOCP) {
        ErrorExit("CreateIoCompletionPort");
    }

    return g_hIOCP;
}

inline void InitialiseRIO(SOCKET s) {
    GUID functionTableId = WSAID_MULTIPLE_RIO;

    DWORD dwBytes = 0;

    bool ok = true;

    if (0
        != WSAIoctl(s, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &functionTableId, sizeof(GUID),
                    (void**)&g_rio, sizeof(g_rio), &dwBytes, 0, 0)) {
        ErrorExit("WSAIoctl");
    }
}

inline void Bind(SOCKET s, const unsigned short port) {
    sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (SOCKET_ERROR == ::bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
        ErrorExit("bind");
    }
}

inline int join_group(SOCKET sd, UINT32 grpaddr, UINT32 iaddr) {
    // "Share" socket address.
    int sockOpt = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&sockOpt), sizeof(int));
    // Join the group
    struct ip_mreq imr;
    imr.imr_multiaddr.s_addr = grpaddr;
    imr.imr_interface.s_addr = iaddr;
    return setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&imr, sizeof(imr));
}

inline void CreateRIOSocket(args_t args) {
    g_s = CreateSocket(WSA_FLAG_REGISTERED_IO);

    int yes = 1;
    setsockopt(g_s, IPPROTO_IP, IP_PKTINFO, reinterpret_cast<char*>(&yes), sizeof(yes));

    //Bind(g_s, args.McastPort);
    BindSocket(g_s, args.McastPort, args.IfIndex);

    string strIp = "";
    Ipv4Vect addrs = args.McastAddrStr;
    for (const auto mcAddr : addrs) {
        if (auto r
            = join_group(g_s, inet_addr(mcAddr.str().c_str()), inet_addr(args.IfIndex.c_str()))
              < 0) {
            ErrorExit("setsockopt", r);
        }
    }

    InitialiseRIO(g_s);
}

inline void SetSocketSendBufferToMaximum(SOCKET s) {
    int soRecvBufSize = 0;

    int optLen = sizeof(soRecvBufSize);

    if (SOCKET_ERROR
        == ::getsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&soRecvBufSize),
                        &optLen)) {
        ErrorExit("setsockopt");
    }

    cout << "Send size = " << soRecvBufSize << endl;

    soRecvBufSize = std::numeric_limits<int>::max();

    cout << "Try to set Send buf to " << soRecvBufSize << endl;

    if (SOCKET_ERROR
        == ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&soRecvBufSize),
                        sizeof(soRecvBufSize))) {
        ErrorExit("setsockopt");
    }

    if (SOCKET_ERROR
        == ::getsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&soRecvBufSize),
                        &optLen)) {
        ErrorExit("setsockopt");
    }

    cout << "Send buf actually set to " << soRecvBufSize << endl;
}

inline void SetSocketRecvBufferToMaximum(SOCKET s) {
    int soRecvBufSize = 0;

    int optLen = sizeof(soRecvBufSize);

    if (SOCKET_ERROR
        == ::getsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&soRecvBufSize),
                        &optLen)) {
        ErrorExit("setsockopt");
    }

    cout << "Recv size = " << soRecvBufSize << endl;

    soRecvBufSize = std::numeric_limits<int>::max();

    //   0x3FFFFFFF - possible max?;

    cout << "Try to set recv buf to " << soRecvBufSize << endl;

    if (SOCKET_ERROR
        == ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&soRecvBufSize),
                        sizeof(soRecvBufSize))) {
        ErrorExit("setsockopt");
    }

    if (SOCKET_ERROR
        == ::getsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&soRecvBufSize),
                        &optLen)) {
        ErrorExit("setsockopt");
    }

    cout << "Recv buf actually set to " << soRecvBufSize << endl;
}

inline char* AllocateBufferSpace(const DWORD recvBufferSize,
                                 const DWORD pendingRecvs,
                                 DWORD& bufferSize,
                                 DWORD& receiveBuffersAllocated) {
    const DWORD preferredNumaNode = 0;

    const SIZE_T largePageMinimum = USE_LARGE_PAGES ? ::GetLargePageMinimum() : 0;

    SYSTEM_INFO systemInfo;

    ::GetSystemInfo(&systemInfo);

    systemInfo.dwAllocationGranularity;

    const unsigned __int64 granularity
        = (largePageMinimum == 0 ? systemInfo.dwAllocationGranularity : largePageMinimum);

    const unsigned __int64 desiredSize = recvBufferSize * pendingRecvs;

    unsigned __int64 actualSize = RoundUp(desiredSize, granularity);

    if (actualSize > std::numeric_limits<DWORD>::max()) {
        actualSize = (std::numeric_limits<DWORD>::max() / granularity) * granularity;
    }

    receiveBuffersAllocated
        = std::min<DWORD>(pendingRecvs, static_cast<DWORD>(actualSize / recvBufferSize));

    bufferSize = static_cast<DWORD>(actualSize);

    char* pBuffer = reinterpret_cast<char*>(
        VirtualAllocExNuma(GetCurrentProcess(), 0, bufferSize,
                           MEM_COMMIT | MEM_RESERVE | (largePageMinimum != 0 ? MEM_LARGE_PAGES : 0),
                           PAGE_READWRITE, preferredNumaNode));

    if (pBuffer == 0) {
        ErrorExit("VirtualAlloc");
    }

    return pBuffer;
}

inline char* AllocateBufferSpace(const DWORD recvBufferSize,
                                 const DWORD pendingRecvs,
                                 DWORD& receiveBuffersAllocated) {
    DWORD notUsed;

    return AllocateBufferSpace(recvBufferSize, pendingRecvs, notUsed, receiveBuffersAllocated);
}

inline void PostIOCPRecvs(const DWORD recvBufferSize, const DWORD pendingRecvs) {
    DWORD totalBuffersAllocated = 0;

    while (totalBuffersAllocated < pendingRecvs) {
        DWORD receiveBuffersAllocated = 0;

        char* pBuffer = AllocateBufferSpace(recvBufferSize, pendingRecvs, receiveBuffersAllocated);

        totalBuffersAllocated += receiveBuffersAllocated;

        DWORD offset = 0;

        const DWORD recvFlags = 0;

        EXTENDED_OVERLAPPED* pBufs = new EXTENDED_OVERLAPPED[receiveBuffersAllocated];

        DWORD bytesRecvd = 0;
        DWORD flags = 0;

        for (DWORD i = 0; i < receiveBuffersAllocated; ++i) {
            EXTENDED_OVERLAPPED* pOverlapped = pBufs + i;

            ZeroMemory(pOverlapped, sizeof(EXTENDED_OVERLAPPED));

            pOverlapped->buf.buf = pBuffer + offset;
            pOverlapped->buf.len = recvBufferSize;

            offset += recvBufferSize;

            if (SOCKET_ERROR
                == ::WSARecv(g_s, &(pOverlapped->buf), 1, &bytesRecvd, &flags,
                             static_cast<OVERLAPPED*>(pOverlapped), 0)) {
                const DWORD lastError = ::GetLastError();

                if (lastError != ERROR_IO_PENDING) {
                    ErrorExit("WSARecv", lastError);
                }
            }
        }

        if (totalBuffersAllocated != pendingRecvs) {
            cout << pendingRecvs << " receives pending" << endl;
        }
    }

    cout << totalBuffersAllocated << " total receives pending" << endl;
}


inline void PostRIORecvs(const DWORD recvBufferSize, const DWORD pendingRecvs) {

    DWORD totalBuffersAllocated = 0;

    DWORD totalBufferCount = 0;
    DWORD totalBufferSize = 0;
    DWORD offset = 0;
    DWORD recvFlags = 0;

    // SEND
    /*
    g_sendBufferPointer = AllocateBufferSpace(SEND_BUFFER_SIZE, RIO_PENDING_SENDS,
                                                totalBufferSize, totalBufferCount);
    g_sendBufferId
        = g_rio.RIORegisterBuffer(g_sendBufferPointer, static_cast<DWORD>(totalBufferSize));

    if (g_sendBufferId == RIO_INVALID_BUFFERID) {
        printf_s("RIORegisterBuffer Error: %d\n", GetLastError());
        exit(0);
    }

    g_sendRioBufs = new EXTENDED_RIO_BUF[totalBufferCount];
    g_sendRioBufTotalCount = totalBufferCount;

    for (DWORD i = 0; i < g_sendRioBufTotalCount; ++i) {
        /// split g_sendRioBufs to SEND_BUFFER_SIZE for each RIO operation
        EXTENDED_RIO_BUF* pBuffer = g_sendRioBufs + i;

        pBuffer->operation = 1; //OP_SEND;
        pBuffer->BufferId = g_sendBufferId;
        pBuffer->Offset = offset;
        pBuffer->Length = SEND_BUFFER_SIZE;

        offset += SEND_BUFFER_SIZE;
    }
    */

    // ADDRESS LOCAL
    
    /// registering RIO buffers for ADDR
    totalBufferCount = 0;
    totalBufferSize = 0;
    offset = 0;

    g_addrLocBufferPointer = AllocateBufferSpace(ADDR_BUFFER_SIZE, RIO_PENDING_RECVS,
                                                totalBufferSize, totalBufferCount);
    g_addrLocBufferId
        = g_rio.RIORegisterBuffer(g_addrLocBufferPointer, static_cast<DWORD>(totalBufferSize));

    if (g_addrLocBufferId == RIO_INVALID_BUFFERID) {
        printf_s("RIORegisterBuffer Addr Local Error: %d\n", GetLastError());
        RioCleanUp();
        exit(0);
    }

    g_addrLocRioBufs = new EXTENDED_RIO_BUF[totalBufferCount];
    g_addrLocRioBufTotalCount = totalBufferCount;

    for (DWORD i = 0; i < totalBufferCount; ++i) {
        EXTENDED_RIO_BUF* pBuffer = g_addrLocRioBufs + i;

        pBuffer->operation = 3;     //OP_NONE;
        pBuffer->BufferId = g_addrLocBufferId;
        pBuffer->Offset = offset;
        pBuffer->Length = ADDR_BUFFER_SIZE;

        offset += ADDR_BUFFER_SIZE;
    }
    
    //**
    //
    // ADDRESS REMOTE
    /*
    /// registering RIO buffers for ADDR REM
    totalBufferCount = 0;
    totalBufferSize = 0;
    offset = 0;

    g_addrRemBufferPointer = AllocateBufferSpace(ADDR_BUFFER_SIZE, RIO_PENDING_RECVS,
                                                 totalBufferSize, totalBufferCount);
    g_addrRemBufferId
        = g_rio.RIORegisterBuffer(g_addrRemBufferPointer, static_cast<DWORD>(totalBufferSize));

    if (g_addrRemBufferId == RIO_INVALID_BUFFERID) {
        printf_s("RIORegisterBuffer Addr Remote Error: %d\n", GetLastError());
        RioCleanUp();
        exit(0);
    }

    g_addrRemRioBufs = new EXTENDED_RIO_BUF[totalBufferCount];
    g_addrRemRioBufTotalCount = totalBufferCount;

    for (DWORD i = 0; i < totalBufferCount; ++i) {
        EXTENDED_RIO_BUF* pBuffer = g_addrRemRioBufs + i;

        pBuffer->operation = 4;  // OP_NONE;
        pBuffer->BufferId = g_addrRemBufferId;
        pBuffer->Offset = offset;
        pBuffer->Length = ADDR_BUFFER_SIZE;

        offset += ADDR_BUFFER_SIZE;
    }
    */

    // RECEIVE
    
    /// registering RIO buffers for RECV and then, post pre-RECV
    totalBufferCount = 0;
    totalBufferSize = 0;
    offset = 0;

    g_recvBufferPointer = AllocateBufferSpace(RECV_BUFFER_SIZE, RIO_PENDING_RECVS,
                                                totalBufferSize, totalBufferCount);

    g_recvBufferId
        = g_rio.RIORegisterBuffer(g_recvBufferPointer, static_cast<DWORD>(totalBufferSize));

    if (g_recvBufferId == RIO_INVALID_BUFFERID) {
        printf_s("RIORegisterBuffer Error: %d\n", GetLastError());
        RioCleanUp();
        exit(0);
    }

    g_recvRioBufs = new EXTENDED_RIO_BUF[totalBufferCount];

    for (DWORD i = 0; i < totalBufferCount; ++i) {
        EXTENDED_RIO_BUF* pBuffer = g_recvRioBufs + i;

        pBuffer->operation = 2;     //OP_RECV;
        pBuffer->BufferId = g_recvBufferId;
        pBuffer->Offset = offset;
        pBuffer->Length = RECV_BUFFER_SIZE;

        offset += RECV_BUFFER_SIZE;

        /// posting pre RECVs
        if (!g_rio.RIOReceiveEx(g_requestQueue, pBuffer, 1,
                                &g_addrLocRioBufs[g_addrLocRioBufIndex++],
                                NULL,   //&g_addrRemRioBufs[g_addrRemRioBufIndex++],
                                NULL, 0, 0, pBuffer)) {

            printf_s("RIOReceive Error: %d\n", GetLastError());
            exit(0);
        }
    }
    

    printf_s("%d total receives pending\n", totalBufferCount);

}

inline void PostRIORecvs_Old(const DWORD recvBufferSize, const DWORD pendingRecvs) {
    DWORD totalBuffersAllocated = 0;

    while (totalBuffersAllocated < pendingRecvs) {
        DWORD bufferSize = 0;

        DWORD receiveBuffersAllocated = 0;

        g_recvBufferPointer = AllocateBufferSpace(recvBufferSize, pendingRecvs, bufferSize,
                                            receiveBuffersAllocated);

        totalBuffersAllocated += receiveBuffersAllocated;

        g_recvBufferId = g_rio.RIORegisterBuffer(g_recvBufferPointer, static_cast<DWORD>(bufferSize));

        g_buffers.push_back(g_recvBufferId);

        if (g_recvBufferId == RIO_INVALID_BUFFERID) {
            ErrorExit("RIORegisterBuffer");
        }

        DWORD offset = 0;

        DWORD recvFlags = 0;

        g_recvRioBufs = new EXTENDED_RIO_BUF[receiveBuffersAllocated];

        for (DWORD i = 0; i < receiveBuffersAllocated; ++i) {
            // now split into buffer slices and post our recvs

            EXTENDED_RIO_BUF* pBuffer = g_recvRioBufs + i;

            pBuffer->operation = 0;
            pBuffer->BufferId = g_recvBufferId;
            pBuffer->Offset = offset;
            pBuffer->Length = recvBufferSize;

            offset += recvBufferSize;

            g_pendingRecvs++;

            if (!g_rio.RIOReceive(g_requestQueue, pBuffer, 1, recvFlags, pBuffer)) {
                ErrorExit("RIOReceive");
            }
        }

        if (totalBuffersAllocated != pendingRecvs) {
            cout << pendingRecvs << " receives pending" << endl;
        }
    }

    cout << totalBuffersAllocated << " total receives pending" << endl;
}

inline void CreateIOCPThreads(const DWORD numThreads) {
    ::InitializeCriticalSectionAndSpinCount(&g_criticalSection, SPIN_COUNT);

    g_hStartedEvent = ::CreateEvent(0, TRUE, FALSE, 0);

    if (0 == g_hStartedEvent) {
        ErrorExit("CreateEvent");
    }

    g_hStoppedEvent = ::CreateEvent(0, TRUE, FALSE, 0);

    if (0 == g_hStoppedEvent) {
        ErrorExit("CreateEvent");
    }

    // Start our worker threads

    for (DWORD i = 0; i < numThreads; ++i) {
        unsigned int notUsed;

        const uintptr_t result = ::_beginthreadex(0, 0, ThreadFunction, (void*)i, 0, &notUsed);

        if (result == 0) {
            ErrorExit("_beginthreadex", errno);
        }

        g_threads.push_back(reinterpret_cast<HANDLE>(result));
    }

    cout << numThreads << " threads running" << endl;
}

inline void CreateReaderThread() {
    g_hReadsToProcessEvent = ::CreateEvent(0, FALSE, FALSE, 0);

    if (0 == g_hReadsToProcessEvent) {
        ErrorExit("CreateEvent");
    }

    g_hShutdownReaderThreadEvent = ::CreateEvent(0, TRUE, FALSE, 0);

    if (0 == g_hShutdownReaderThreadEvent) {
        ErrorExit("CreateEvent");
    }

    unsigned int notUsed;

    const uintptr_t result = ::_beginthreadex(0, 0, ReaderThreadFunction, 0, 0, &notUsed);

    if (result == 0) {
        ErrorExit("_beginthreadex", errno);
    }

    g_hReaderThread = (HANDLE)result;
}

inline void StartTiming() {
    cout << "Timing started" << endl;

    if (!::QueryPerformanceCounter(&g_startCounter)) {
        ErrorExit("QueryPerformanceCounter");
    }
}

inline void StopTiming() {
    if (!::QueryPerformanceCounter(&g_stopCounter)) {
        ErrorExit("QueryPerformanceCounter");
    }

    cout << endl << "Timing stopped" << endl;
}

inline void WaitForProcessingStarted() {
    if (WAIT_OBJECT_0 != ::WaitForSingleObject(g_hStartedEvent, INFINITE)) {
        ErrorExit("WaitForSingleObject");
    }

    StartTiming();
}

inline void WaitForProcessingStopped() {
    if (WAIT_OBJECT_0 != ::WaitForSingleObject(g_hStoppedEvent, INFINITE)) {
        ErrorExit("WaitForSingleObject");
    }

    StopTiming();
}

inline void DisplayThreadStats(const size_t index = 0) {
#ifdef TRACK_THREAD_STATS
    if (g_threadData[index].packetsProcessed != 0) {
        cout << "Thread Id: " << g_threadData[index].threadId << endl;

#ifdef THREAD_STATS_SHOW_DEQUE
        cout << "  Dequeue: " << g_threadData[index].dequeueCalled << endl;
#endif

#ifdef THREAD_STATS_SHOW_NOTIFY
        cout << "   Notify: " << g_threadData[index].notifyCalled << endl;
#endif

        cout << "  Packets: " << g_threadData[index].packetsProcessed << endl;

#ifdef THREAD_STATS_SHOW_MIN_MAX
        cout << "      Min: "
             << (g_threadData[index].packetsProcessed > 0 ? g_threadData[index].minPacketsProcessed
                                                          : 0);
        cout << " - Max: " << g_threadData[index].maxPacketsProcessed;
        cout << " - Average: "
             << (g_threadData[index].dequeueCalled == 0
                     ? 0
                     : g_threadData[index].packetsProcessed / g_threadData[index].dequeueCalled)
             << endl;
#endif

        cout << endl;
    }
#endif
}

inline size_t CountActiveThreads() {
    size_t activeThreads = 0;

    for (size_t i = 0; i < NUM_IOCP_THREADS; i++) {
        if (g_threadData[i].packetsProcessed != 0) {
            activeThreads++;
        }
    }

    return activeThreads;
}

inline void StopIOCPThreads() {
    // Tell all threads to exit

    for (Threads::const_iterator it = g_threads.begin(), end = g_threads.end(); it != end; ++it) {
        if (0 == ::PostQueuedCompletionStatus(g_hIOCP, 0, 0, 0)) {
            ErrorExit("PostQueuedCompletionStatus");
        }
    }

    cout << "Threads stopping" << endl;

    // Wait for all threads to exit

    for (Threads::const_iterator it = g_threads.begin(), end = g_threads.end(); it != end; ++it) {
        HANDLE hThread = *it;

        if (WAIT_OBJECT_0 != ::WaitForSingleObject(hThread, INFINITE)) {
            ErrorExit("WaitForSingleObject");
        }

        ::CloseHandle(hThread);
    }

    cout << "Threads stopped" << endl;

    const size_t activeThreads = CountActiveThreads();

    cout << activeThreads << " threads processed datagrams" << endl;

    for (size_t i = 0; i < NUM_IOCP_THREADS; i++) {
        DisplayThreadStats(i);
    }

    ::DeleteCriticalSection(&g_criticalSection);
}

inline void StopReaderThread() {
    if (!::SetEvent(g_hShutdownReaderThreadEvent)) {
        ErrorExit("SetEvent");
    }

    if (WAIT_OBJECT_0 != ::WaitForSingleObject(g_hReaderThread, INFINITE)) {
        ErrorExit("WaitForSingleObject");
    }

    ::CloseHandle(g_hReaderThread);
}


inline void PrintTimings(const char* pDirection = "Received ") {
    LARGE_INTEGER elapsed;

    elapsed.QuadPart
        = (g_stopCounter.QuadPart - g_startCounter.QuadPart) / (g_frequency.QuadPart / 1000);

    cout << "Complete in " << elapsed.QuadPart << "ms" << endl;
    cout << pDirection << g_packets << " datagrams" << endl;

    if (elapsed.QuadPart != 0) {
        const double perSec = ((double)g_packets / (double)elapsed.QuadPart) * 1000.00;
        cout << g_otherPkts << " other packets" << endl;

        std::cout << std::setprecision(2) << std::fixed << perSec << " datagrams per second"
                  << endl;
    }

    GroupStatsMapPrint();
}

inline void PrintStatsSingleLine() {
    LARGE_INTEGER elapsed;
    double perSec = 0;

    elapsed.QuadPart
        = (g_stopCounter.QuadPart - g_startCounter.QuadPart) / (g_frequency.QuadPart / 1000);

    if (elapsed.QuadPart != 0) {
        perSec = ((double)g_packets / (double)elapsed.QuadPart) * 1000.00;
    }

    cout << std::setw(12) << elapsed.QuadPart << std::setw(12) << g_packets << std::setw(12)
         << g_otherPkts << std::setw(14) << std::setprecision(2) << std::fixed << perSec
         << std::setw(12) << GroupStatsMapTotalOOO() << endl;

    // GroupStatsMapPrint();
}

inline void CleanupRIO() {
    if (SOCKET_ERROR == ::closesocket(g_s)) {
        const DWORD lastError = ::GetLastError();

        cout << "error closing socket: " << GetLastErrorMessage(lastError);
    }

    g_rio.RIOCloseCompletionQueue(g_queue);

    for (BufferList::const_iterator it = g_buffers.begin(), end = g_buffers.end(); it != end;
         ++it) {
        g_rio.RIODeregisterBuffer(*it);
    }
}

inline void CloseRIOApp() {
    StopTiming();
    #ifdef _SINGLELINE_LOG_
        PrintStatsSingleLine();
    #else
        PrintTimings();
    #endif
    RioCleanUp();
    // exit(0);
}


inline int DoWork(const size_t iterations) {
    int result = rand();

    for (size_t i = 0; i < iterations; ++i) {
        result += rand();
    }

    return result % 223;
}
