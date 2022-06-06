#include "RioSession.hpp"

namespace riosession {
RioSession::RioSession(args_t* args, volatile sig_atomic_t* signal)
    : m_Args(args), m_ExitSignal(signal) {
    CreateSocket(WSA_FLAG_REGISTERED_IO);
    m_hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    m_TotalPkts = 0;
    InitGroupStats(args->McastAddrStr);
}

/**
 * @brief Create the WSA Socket as a DGRAM for UDP.
 *
 * @param flags Default flags set to 0.
 */
void RioSession::CreateSocket(const DWORD flags) {
    m_SocketHandle = ::WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, flags);

    if (m_SocketHandle == INVALID_SOCKET) {
        utilities::ErrorExit("WSASocket");
    }
}

void RioSession::InitializeRIO() {
    GUID functionTableId = WSAID_MULTIPLE_RIO;
    DWORD dwBytes = 0;

    if (0
        != WSAIoctl(m_SocketHandle, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &functionTableId,
                    sizeof(GUID), (void**)&m_RioFuncTable, sizeof(m_RioFuncTable), &dwBytes, NULL,
                    NULL)) {
        utilities::ErrorExit("WSAIoctl");
    }
}

/**
 * @brief Bind the socket to an specific Address and Port
 *
 * @param bindAddr
 * @param bindPort
 */
void RioSession::BindSocket(uint16_t bindPort, const std::string& bindAddr) {
    sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(bindPort);
    if (!bindAddr.empty()) {
        auto res = inet_pton(AF_INET, bindAddr.c_str(), &addr.sin_addr.s_addr);
        if (res != 1) {
            utilities::ErrorExit("Error converting the specified address to bind");
        }

    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (SOCKET_ERROR
        == ::bind(m_SocketHandle, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
        utilities::ErrorExit("Error during socket binding process");
    }
}

/**
 * @brief Create a unique Request queue for Send and Receive.
 *
 */
void RioSession::CreateRequestQueue() {
    m_RequestQueue = m_RioFuncTable.RIOCreateRequestQueue(
        m_SocketHandle, m_MaxOutstandingReceive, m_MaxReceiveDataBuffers, m_MaxOutstandingSend,
        m_MaxSendDataBuffers, m_CompletionQueue, m_CompletionQueue, NULL);

    if (m_RequestQueue == RIO_INVALID_RQ) {
        utilities::ErrorExit("Error creating a RIO Request Queue");
    }
}

char* RioSession::AllocateBufferSpace(const DWORD messageSize,
                                      const DWORD totalMessages,
                                      DWORD& bufferSize,
                                      DWORD& receiveBuffersAllocated) {
    const DWORD preferredNumaNode = 0;

    const SIZE_T largePageMinimum = LARGE_PAGES_ENABLED ? ::GetLargePageMinimum() : 0;

    SYSTEM_INFO systemInfo;

    ::GetSystemInfo(&systemInfo);

    systemInfo.dwAllocationGranularity;

    const unsigned __int64 granularity
        = (largePageMinimum == 0 ? systemInfo.dwAllocationGranularity : largePageMinimum);

    const unsigned __int64 desiredSize = messageSize * totalMessages;

    unsigned __int64 actualSize = utilities::RoundUp(desiredSize, granularity);

    if (actualSize > std::numeric_limits<DWORD>::max()) {
        actualSize = (std::numeric_limits<DWORD>::max() / granularity) * granularity;
    }

    receiveBuffersAllocated
        = std::min<DWORD>(totalMessages, static_cast<DWORD>(actualSize / messageSize));

    bufferSize = static_cast<DWORD>(actualSize);

    // MEM_COMMIT assures that the memory will be filled with zeroes
    char* pBuffer = reinterpret_cast<char*>(
        VirtualAllocExNuma(GetCurrentProcess(), 0, bufferSize,
                           MEM_COMMIT | MEM_RESERVE | (largePageMinimum != 0 ? MEM_LARGE_PAGES : 0),
                           PAGE_READWRITE, preferredNumaNode));

    if (pBuffer == 0) {
        utilities::ErrorExit("VirtualAlloc");
    }

    return pBuffer;
}

/**
 * @brief Allocate a Buffer of expected size @param messageSize * @param totalMessages
 * register that buffer using the RIO API and @return a pointer to the buffer.
 *  Store on @param bufferId the ID that the RIO API has assigned to the buffer.
 *
 * @param messageSize
 * @param totalMessages
 * @param bufferId
 * @return char*
 */
char* RioSession::AllocateAndRegisterBuffer(const DWORD messageSize,
                                            const DWORD totalMessages,
                                            RIO_BUFFERID& bufferId) {
    DWORD bufferSize = 0;
    DWORD pendingRealSize = 0;

    auto bufferPointer
        = AllocateBufferSpace(messageSize, totalMessages, bufferSize, pendingRealSize);
    bufferId = m_RioFuncTable.RIORegisterBuffer(bufferPointer, static_cast<DWORD>(bufferSize));
    if (bufferId == RIO_INVALID_BUFFERID || pendingRealSize != totalMessages) {
        utilities::ErrorExit("RIORegisterBuffer");
    }
    return bufferPointer;
}

/**
 * @brief Clean Up the object. Close the Socket, close the completion Queue
 *  de-register the buffer and de-allocate the memory.
 */
void RioSession::CleanUpRIO() {
    CloseSocket();
    m_RioFuncTable.RIOCloseCompletionQueue(m_CompletionQueue);
    ReleaseAndDeregisterBuffer(m_RioBuffId, m_RioBuffPtr);
    ReleaseAndDeregisterBuffer(m_McAddrBuffId, m_McAddrBuffPtr);
}

/**
 * @brief Close the Win Socket
 *
 */
void RioSession::CloseSocket() {
    if (SOCKET_ERROR == ::closesocket(m_SocketHandle)) {
        utilities::ErrorExit("Error Closing Socket");
    }
}

/**
 * @brief De-allocates the packet memory buffer and De-register the buffer from RIO API.
 *
 */
void RioSession::ReleaseAndDeregisterBuffer(RIO_BUFFERID& bufferId, char* buffPointer) {
    m_RioFuncTable.RIODeregisterBuffer(bufferId);
    if (0 == VirtualFreeEx(GetCurrentProcess(), buffPointer, 0, MEM_RELEASE)) {
        utilities::ErrorExit("Error deAllocating the buffer");
    };
}

/**
 * @brief Create a Completion Queue object using IOCP Completion type.
 *
 * @param cqSize
 */
void RioSession::CreateCompletionQueue(DWORD cqSize) {
    OVERLAPPED overlapped;
    RIO_NOTIFICATION_COMPLETION completionType;
    completionType.Type = RIO_IOCP_COMPLETION;
    completionType.Iocp.IocpHandle = m_hIOCP;
    completionType.Iocp.CompletionKey = (void*)0;
    completionType.Iocp.Overlapped = &overlapped;
    m_CompletionQueue = m_RioFuncTable.RIOCreateCompletionQueue(cqSize, &completionType);
    if (m_CompletionQueue == RIO_INVALID_CQ) {
        utilities::ErrorExit("RIOCreateCompletionQueue");
    }
}

/**
 * @brief Notify the completion Queue
 *
 */
void RioSession::NotifyCompletionQueue() {
    INT notifyResult = m_RioFuncTable.RIONotify(m_CompletionQueue);
    if (notifyResult != ERROR_SUCCESS) {
        utilities::ErrorExit("RIONotify", notifyResult);
    }
}

/**
 * @brief Init the Multicast Group Stats for each Multicast Group
 *  configured by the user
 * @param mcastGroupAddr
 */
void RioSession::InitGroupStats(const Ipv4Vect& mcastGroupAddr) {
    for (const auto mcAddr : mcastGroupAddr) {
        m_GroupStats.insert(
            std::pair<uint32_t, McGroupStats_t>(mcAddr.ipNetOrder(), McGroupStats_t{}));
    }
}

/**
 * @brief Prints timing, datagrams per seconds.
 *
 * @param pktsProcessed
 * @param pktsMalformed
 */
void RioSession::PrintTimings(ULONGLONG pktsProcessed, ULONGLONG pktsOther) {
    uint64_t elapsedMs = m_Timing.getElapsedTimeMs();
    std::cout << "Results: " << std::endl;
    std::cout << "\tComplete in " << elapsedMs << "ms" << std::endl;
    std::cout << "\tProcessed a total of: " << pktsProcessed << " packets" << std::endl;
    std::cout << "\tWith " << pktsOther << " other received packets" << std::endl;
    if (elapsedMs != 0) {
        const double perSec = pktsProcessed / (elapsedMs / 1000.00);
        std::cout << "\t" << perSec << " datagrams per second" << std::endl;
    }
}

bool RioSession::ShouldStop() {
    // If volatile variable is 1, exit the application
    auto sigExit = (*m_ExitSignal == 0) ? true : false;
    // If PktsToCount is 0 keep working. If not, check if we already send/receive that value.
    auto pktsTreshold = (m_Args->PktsToCount == 0) ? true : (m_TotalPkts < m_Args->PktsToCount);
    // If TimeTreshold is 0 keep working. If not, check if the program run for N seconds.
    m_Timing.setStop();
    auto timeTreshold = (m_Args->SecondsToRun == 0)
                            ? true
                            : (m_Timing.getElapsedTimeSec() < m_Args->SecondsToRun);
    return (sigExit && pktsTreshold && timeTreshold);
}

/**
 * @brief If Pointer to thread is not null, join it.
 *
 * @param t
 */
void RioSession::JoinThread(UniqueThread_t& t) const {
    if (t) {
        if (t->joinable()) {
            t->join();
        }
        t.reset(nullptr);
    }
}

}  // namespace riosession
