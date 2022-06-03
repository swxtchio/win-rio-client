#include "RioConsumer.hpp"

namespace riosession {
RioConsumer::RioConsumer(args_t* args, volatile sig_atomic_t* signal) : RioSession(args, signal) {
    BindSocket(args->McastPort, args->IfIndex);
    JoinGroups(args->McastAddrStr);
    m_MaxOutstandingReceive = MAX_PENDING_RECVS;
    m_MaxReceiveDataBuffers = 1;
    m_MaxOutstandingSend = 0;
    m_MaxSendDataBuffers = 1;
    m_RioBuffDescr = std::make_unique<RIO_BUF[]>(m_MaxOutstandingReceive);
    InitializeRIO();
    CreateCompletionQueue(static_cast<DWORD>(m_MaxOutstandingReceive));
    CreateRequestQueue();
    m_RioBuffPtr = AllocateAndRegisterBuffer(
        EXPECTED_DATA_SIZE, static_cast<DWORD>(m_MaxOutstandingReceive), m_RioBuffId);
    m_McAddrBuffPtr = AllocateAndRegisterBuffer(
        ADDR_SIZE, static_cast<DWORD>(m_MaxOutstandingReceive), m_McAddrBuffId);
    InitMcAddrDescriptors();
}

/**
 * @brief Join an specific multicast group. It can be performed several times to join
 * a series of multicast groups on the same socket.
 * @param grpaddr Multicast Group Address
 * @param iaddr Local Interface address to use
 * @return int
 */
int RioConsumer::JoinGroup(UINT32 grpaddr, UINT32 iaddr) {
    // "Share" socket address.
    int sockOpt = 1;
    setsockopt(m_SocketHandle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&sockOpt),
               sizeof(int));
    // Join the group
    struct ip_mreq imr;
    imr.imr_multiaddr.s_addr = grpaddr;
    imr.imr_interface.s_addr = iaddr;
    return setsockopt(m_SocketHandle, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&imr, sizeof(imr));
}

/**
 * @brief Join "n" multicast groups by iterating over all addresses
 * configured by the user.
 * @param mcastAddrs
 */
void RioConsumer::JoinGroups(Ipv4Vect mcastAddrs) {
    for (const auto mcAddr : mcastAddrs) {
        auto r = JoinGroup(inet_addr(mcAddr.str().c_str()), inet_addr(m_Args->IfIndex.c_str()));
        if (r < 0) {
            utilities::ErrorExit("setsockopt", r);
        }
    }
}

/**
 * @brief Init the Multicast Address Descriptors
 *  Each descriptor will point to a position of the Address Buffer where
 *  SOCKADDR_INET information will be stored on each received packet.
 */
void RioConsumer::InitMcAddrDescriptors() {
    ULONG offset = 0;
    m_McAddrDescr = std::make_unique<RIO_BUF[]>(m_MaxOutstandingReceive);
    for (DWORD i = 0; i < m_MaxOutstandingReceive; i++) {
        m_McAddrDescr[i].BufferId = m_McAddrBuffId;
        m_McAddrDescr[i].Offset = offset;
        m_McAddrDescr[i].Length = ADDR_SIZE;
        offset += ADDR_SIZE;
    }
}

/**
 * @brief Post a number of @param totalMessages receives using RIO.
 * Each RIO_BUF descriptor tells where to store each packet on the buffer.
 *
 * @param totalMessages
 */
void RioConsumer::PostFirstRecvs(DWORD totalMessages) {
    DWORD offset = 0;
    DWORD recvFlags = 0;
    for (DWORD i = 0; i < totalMessages; ++i) {
        // Fill Descriptors
        m_RioBuffDescr[i].BufferId = m_RioBuffId;
        m_RioBuffDescr[i].Offset = offset;
        m_RioBuffDescr[i].Length = EXPECTED_DATA_SIZE;

        offset += EXPECTED_DATA_SIZE;
        if (!m_RioFuncTable.RIOReceiveEx(m_RequestQueue, &m_RioBuffDescr[i], 1, &m_McAddrDescr[i],
                                         NULL, NULL, NULL, recvFlags, &m_RioBuffDescr[i])) {
            utilities::ErrorExit("RIOReceive");
        }
    }
}

void RioConsumer::Start() {
    DWORD numberOfBytes = 0;
    ULONG_PTR completionKey = 0;
    OVERLAPPED* pOverlapped = 0;
    DWORD recvFlags = 0;
    ULONGLONG packetCounter = 0;
    ULONGLONG otherPacketCounter = 0;
    RIORESULT results[MAX_RIO_RESULTS];
    ULONG mcAddrDescrIndex = 0;
    BOOL shouldNotify = true;
    
    m_Timing.setStart(); //set start time because report thread will crash if not
    m_ReportThread = std::make_unique<std::thread>(&RioConsumer::ReportWorker, this);
    PostFirstRecvs(static_cast<DWORD>(m_MaxOutstandingReceive));

    while (ShouldStop()) {
        if (shouldNotify)
            NotifyCompletionQueue();

        if (!::GetQueuedCompletionStatus(m_hIOCP, &numberOfBytes, &completionKey, &pOverlapped,
                                         100)) {
            shouldNotify = false;
            // utilities::ErrorExit("GetQueuedCompletionStatus");
        }

        ULONG numResults
            = m_RioFuncTable.RIODequeueCompletion(m_CompletionQueue, results, MAX_RIO_RESULTS);

        // If there is no pkts to read right now just loop around
        if (0 == numResults || RIO_CORRUPT_CQ == numResults) {
            continue;
            // utilities::ErrorExit("RIODequeueCompletion");
        }
        if (m_TotalPkts == 0)
            m_Timing.setStart(); //overwrite start time

        for (DWORD i = 0; i < numResults; ++i) {
            auto pBuffer = reinterpret_cast<RIO_BUF*>(results[i].RequestContext);
            m_TotalPkts++;  // atomic fetch add
            if (results[i].BytesTransferred == EXPECTED_DATA_SIZE) {
                packetCounter++;
                auto nextAddr = &m_McAddrDescr[mcAddrDescrIndex % m_MaxOutstandingReceive];
                auto mcastAddr
                    = reinterpret_cast<SOCKADDR_INET*>(m_McAddrBuffPtr + nextAddr->Offset);
                auto pHeader = reinterpret_cast<ProtocolHeader_t*>(m_RioBuffPtr + pBuffer->Offset);
                GroupStatsUpdate(mcastAddr, EXPECTED_DATA_SIZE, pHeader);

                // Start receiving again
                if (!m_RioFuncTable.RIOReceiveEx(m_RequestQueue, pBuffer, 1, nextAddr, NULL, NULL,
                                                 NULL, recvFlags, pBuffer)) {
                    utilities::ErrorExit("RIOReceive");
                }
                mcAddrDescrIndex++;
            } else {
                otherPacketCounter++;
            }
        }
        shouldNotify = true;
    }
    JoinThread(m_ReportThread);
    PrintTimings(packetCounter, otherPacketCounter);
    GroupStatsPrint();
}

/**
 * @brief Update the statistics for an specific Multicast Group
 *
 * @param addr Stores a pointer the IPV4 address of the multicast group used as a key in the map
 * @param pktSize Stores the size of the received packet
 * @param pHdr Stores a pointer to the ProtocolHeader
 */
void RioConsumer::GroupStatsUpdate(const SOCKADDR_INET* addr,
                                   const size_t pktSize,
                                   const ProtocolHeader_t* pHdr) {
    uint32_t mcGroup = addr->Ipv4.sin_addr.s_addr;
    McGroupStats_t& gmc = m_GroupStats[mcGroup];

    if (pHdr->Seq == gmc.ExpectedSequence.load()) {
        // The received sequence matches the expected one
        gmc.ExpectedSequence++;
        gmc.Sequence.store(pHdr->Seq);
    } else if (pHdr->Seq > gmc.ExpectedSequence.load()) {
        // If it's not the first packet then-->drops. Otherwise expected a new seq.
        if (gmc.Packets.load() != 0)
            gmc.RxDropped += pHdr->Seq - gmc.ExpectedSequence.load();
        gmc.ExpectedSequence.store(pHdr->Seq + 1);
        gmc.Sequence.store(pHdr->Seq);
    } else {
        // Out of order packets, if they are in a window of at least 3 packets.
        if (gmc.RxDropped.load() > 0)  //&& (gmc.ExpectedSequence - (pHdr->Seq) <= 3))
            gmc.RxDropped--;
        gmc.OutOfOrder++;
    }
    gmc.Packets++;
    gmc.Bytes.fetch_add(pktSize);
}

void RioConsumer::GroupStatsPrint() {
    char inetspace[16];
    uint64_t totalPackets = 0;
    uint64_t totalBytes = 0;
    uint64_t totalOutOfSequence = 0;
    uint64_t totalDrops = 0;

    std::cout << "\n  Group           Packets        Bytes      Last Seq   OutOfOrder    Drops"
              << std::endl;
    std::cout << "-----------------------------------------------------------------------------"
              << std::endl;

    for (auto const& [key, value] : m_GroupStats) {
        std::cout << inet_ntop(AF_INET, &key, inetspace, INET_ADDRSTRLEN) << "\t" << std::dec
                  << std::setw(10) << value.Packets << "  " << std::setw(12) << value.Bytes << "  "
                  << std::setw(10) << value.Sequence << "  " << std::setw(10) << value.OutOfOrder
                  << "  " << std::setw(10) << value.RxDropped << std::endl;

        totalPackets += value.Packets;
        totalBytes += value.Bytes;
        totalOutOfSequence += value.OutOfOrder;
        totalDrops += value.RxDropped;
    }
    std::cout << "-----------------------------------------------------------------------------"
              << std::endl;
    std::cout << "Totals:"
              << "\t\t" << std::setw(10) << totalPackets << "  " << std::setw(12) << totalBytes
              << "    " << std::setw(20) << totalOutOfSequence << std::setw(12) << totalDrops
              << std::endl;
    std::cout << std::endl;
}

void RioConsumer::PrintReportHeader() {
    // clang-format off
    printf("|               TOTALS                 |               THIS PERIOD                   |\n");
    printf("|------------|------------|------------|------------|------------|-----------|-------|\n");
    printf("|    PKTS    |     OOO    |   MISSING  |     OOO    |   MISSING  |    PPS    |  BPS  |\n");
    printf("|------------|------------|------------|------------|------------|-----------|-------|\n");
    // clang-format on
}

void RioConsumer::PrintReportRow(const TotalStats_t& stats,
                                 const uint64_t& oooNow,
                                 const uint64_t& missNow,
                                 const double& pps,
                                 const double& bps) {
    printf("| %10llu | %10llu | %10llu | %10llu | %10llu | %9.2f | %s |\n", stats.TotalPackets,
           stats.TotalOutOfOrder, stats.TotalDrops, oooNow, missNow, pps,
           swxtch::str::FormatValueToSI(bps, 1).c_str());
}

/**
 * @brief Get a snapshot of the statistic of each MulticastGroup and
 * add them to get the totals for that instant
 * @return TotalStats_t
 */
TotalStats_t RioConsumer::GetMcTotals() {
    TotalStats_t partialStats;
    for (auto const& [key, value] : m_GroupStats) {
        partialStats.TotalPackets += value.Packets.load();
        partialStats.TotalBytes += value.Bytes.load();
        partialStats.TotalOutOfOrder += value.OutOfOrder.load();
        partialStats.TotalDrops += value.RxDropped.load();
    }
    return partialStats;
}

void RioConsumer::ReportWorker() {
    uint64_t prevReportTime = 0;
    int reportCount = 0;
    TotalStats_t prevStats;

    while (ShouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto now = utilities::get_unix_time();
        auto timeDelta = (double)(now - prevReportTime) / (double)utilities::ONE_SECOND;
        if (timeDelta > REPORT_PERIOD_SEC) {
            prevReportTime = now;
            reportCount++;

            if ((reportCount % 16) == 1) {
                PrintReportHeader();
            }

            auto statsNow = GetMcTotals();

            auto rxDeltaPackets = statsNow.TotalPackets - prevStats.TotalPackets;
            auto rxDeltaBytes = statsNow.TotalBytes - prevStats.TotalBytes;
            auto rxDeltaDropped
                = statsNow.TotalDrops - std::min(prevStats.TotalDrops, statsNow.TotalDrops);
            auto rxDeltaOoo = statsNow.TotalOutOfOrder - prevStats.TotalOutOfOrder;
            auto rxPps = (double)rxDeltaPackets / timeDelta;
            auto rxBps = (double)rxDeltaBytes * 8 / timeDelta;
            PrintReportRow(statsNow, rxDeltaOoo, rxDeltaDropped, rxPps, rxBps);
            prevStats = statsNow;
        }
    }
}

}  // namespace riosession
