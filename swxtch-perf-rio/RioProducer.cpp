#include "RioProducer.hpp"

namespace riosession {
RioProducer::RioProducer(args_t* args, volatile sig_atomic_t* signal) : RioSession(args, signal) {
    BindSocket(0, args->IfIndex);  // Bind to any port on ifIndex addr
    m_MaxOutstandingReceive = 0;
    m_MaxReceiveDataBuffers = 1;
    m_NumberOfMcGroups = m_Args->McastAddrStr.size();
    m_MaxOutstandingSend = m_Args->PacketRate * m_NumberOfMcGroups;
    m_MaxSendDataBuffers = 1;
    m_SpinDuration = (int64_t)(1e9 / (double)args->PacketRate);
    m_RioBuffDescr = std::make_unique<RIO_BUF[]>(m_MaxOutstandingSend);
    std::cout << "Max Outstanding sends: " << m_MaxOutstandingSend << std::endl;
    SendOnInterface(args->IfIndex);
    InitializeRIO();
    CreateCompletionQueue(static_cast<DWORD>(m_MaxOutstandingSend));
    CreateRequestQueue();
    m_RioBuffPtr = AllocateAndRegisterBuffer(EXPECTED_DATA_SIZE,
                                             static_cast<DWORD>(m_Args->PacketRate), m_RioBuffId);
    m_McAddrBuffPtr = AllocateAndRegisterBuffer(ADDR_SIZE, m_NumberOfMcGroups, m_McAddrBuffId);
    InitMcAddrDescriptors();
}

void RioProducer::SendOnInterface(const std::string& iaddr) {
    auto ipAddr = inet_addr(iaddr.c_str());
    auto r
        = setsockopt(m_SocketHandle, IPPROTO_IP, IP_MULTICAST_IF, (char*)&ipAddr, sizeof(ipAddr));
    if (r != 0) {
        utilities::ErrorExit("setsockopt IP_MULTICAST_IF");
    }
}

void RioProducer::InitMcAddrDescriptors() {
    DWORD offset = 0;
    DWORD recvFlags = 0;
    DWORD data_offset = 0;
    ULONGLONG packetCounter = 0;
    m_McAddrDescr = std::make_unique<RIO_BUF[]>(m_NumberOfMcGroups);
    for (DWORD i = 0; i < m_NumberOfMcGroups; i++) {
        m_McAddrDescr[i].BufferId = m_McAddrBuffId;
        m_McAddrDescr[i].Offset = offset;
        m_McAddrDescr[i].Length = ADDR_SIZE;

        auto sockAddr = reinterpret_cast<SOCKADDR_INET*>(m_McAddrBuffPtr + offset);
        sockAddr->Ipv4.sin_family = AF_INET;
        sockAddr->Ipv4.sin_port = htons(m_Args->McastPort);
        uint32_t mcAddr = 0;
        auto mcastStrAddr = m_Args->McastAddrStr[i].str();
        auto res = inet_pton(AF_INET, mcastStrAddr.c_str(), &mcAddr);
        if (res != 1) {
            utilities::ErrorExit("Converting Mcast Address");
        }
        sockAddr->Ipv4.sin_addr.s_addr = mcAddr;
        offset += ADDR_SIZE;
    }
}

uint64_t RioProducer::PostFirstSend(DWORD totalMessages) {
    DWORD offset = 0;
    DWORD sendFlags = 0;
    uint64_t sequenceNumber = 0;
    DWORD groupCounter = 0;
    // Fill @totalMessages descriptors and initialize @totalMessages
    // packets with data.
    // There are PacketRate * NumberOfMcGroups descriptors but only PacketRate Real Packets
    for (DWORD i = 0; i < m_Args->PacketRate; ++i) {
        for (DWORD j = 0; j < m_NumberOfMcGroups; j++) {
            m_RioBuffDescr[(m_NumberOfMcGroups * i) + j].BufferId = m_RioBuffId;
            m_RioBuffDescr[(m_NumberOfMcGroups * i) + j].Offset = offset;
            m_RioBuffDescr[(m_NumberOfMcGroups * i) + j].Length = EXPECTED_DATA_SIZE;
        }
        auto pHeader = reinterpret_cast<ProtocolHeader_t*>(m_RioBuffPtr + offset);
        pHeader->Token = 490u;
        pHeader->CmdType = 0;
        pHeader->Seq = i;
        pHeader->Timestamp = utilities::get_unix_time();
        offset += EXPECTED_DATA_SIZE;
        sequenceNumber++;
    }

    for (DWORD i = 0; i < m_MaxOutstandingSend; i++) {
        // Send the same packet for each Multicast Group
        if (!m_RioFuncTable.RIOSendEx(m_RequestQueue, &m_RioBuffDescr[i], 1, NULL,
                                      &m_McAddrDescr[i % m_Args->McastAddrStr.size()], NULL, NULL,
                                      sendFlags, &m_RioBuffDescr[i])) {
            utilities::ErrorExit("RIOSend");
        }
        m_TotalPkts++;  // atomic fetch_add(1)
        groupCounter = (groupCounter + 1) % m_NumberOfMcGroups;
        auto mcAddr
            = (SOCKADDR_INET*)(m_McAddrBuffPtr + m_McAddrDescr[i % m_NumberOfMcGroups].Offset);
        GroupStatsUpdate(mcAddr, EXPECTED_DATA_SIZE, nullptr);
        // Spin and generate a new packet sequence after sending  groupCounter Packets
        if (!groupCounter) {
            utilities::spin(m_SpinDuration.load());
        }
    }
    return sequenceNumber;
}

void RioProducer::Start() {
    DWORD numberOfBytes = 0;
    ULONG_PTR completionKey = 0;
    OVERLAPPED* pOverlapped = 0;
    DWORD sendFlags = 0;
    ULONGLONG sequenceNumber = 0;
    DWORD maxResults = m_MaxOutstandingSend;
    DWORD groupCounter = 0;
    m_Timing.setStart();
    auto results = std::make_unique<RIORESULT[]>(maxResults);
    std::cout << "Max Results: " << maxResults << std::endl;
    m_ReportThread = std::make_unique<std::thread>(&RioProducer::SpinWorker, this);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sequenceNumber = PostFirstSend(m_MaxOutstandingSend);

    while (ShouldStop()) {
        NotifyCompletionQueue();
        if (!::GetQueuedCompletionStatus(m_hIOCP, &numberOfBytes, &completionKey, &pOverlapped,
                                         INFINITE)) {
            utilities::ErrorExit("GetQueuedCompletionStatus");
        }

        ULONG numResults
            = m_RioFuncTable.RIODequeueCompletion(m_CompletionQueue, results.get(), maxResults);

        if (0 == numResults || RIO_CORRUPT_CQ == numResults) {
            utilities::ErrorExit("RIODequeueCompletion");
        }

        for (DWORD i = 0; i < numResults; ++i) {
            auto pBuffer = reinterpret_cast<RIO_BUF*>(results[i].RequestContext);
            auto pHeader = reinterpret_cast<ProtocolHeader_t*>(m_RioBuffPtr + pBuffer->Offset);
            pHeader->Seq = sequenceNumber;
            pHeader->Timestamp = utilities::get_unix_time();

            auto nextAddr = &m_McAddrDescr[i % m_NumberOfMcGroups];

            if (!m_RioFuncTable.RIOSendEx(m_RequestQueue, pBuffer, 1, NULL, nextAddr, NULL, NULL,
                                          sendFlags, pBuffer)) {
                utilities::ErrorExit("RIOSend");
            }
            auto mcAddr = reinterpret_cast<SOCKADDR_INET*>(m_McAddrBuffPtr + nextAddr->Offset);
            GroupStatsUpdate(mcAddr, EXPECTED_DATA_SIZE, nullptr);
            m_TotalPkts++;  // atomic fetch add
            groupCounter = (groupCounter + 1) % m_NumberOfMcGroups;
            if (!groupCounter) {
                sequenceNumber++;
                utilities::spin(m_SpinDuration.load());
            }
        }
    }
    PrintTimings(m_TotalPkts, 0);
    GroupStatsPrint();
    JoinThread(m_ReportThread);
}

void RioProducer::SpinWorker() {
    uint64_t PreviousN = 0;
    uint64_t PreviousTuningN = 0;
    int64_t ExpectedPPSDiv10 = (m_Args->PacketRate * m_NumberOfMcGroups) / 10;
    int64_t DeltaThreashold = std::max((m_Args->PacketRate * m_NumberOfMcGroups) / 2000UL, 1UL);
    auto NextReportTime = utilities::get_unix_time() + (uint64_t)1e9;
    auto NextTuningTime = utilities::get_unix_time() + (uint64_t)1e8;
    while (ShouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto Now = utilities::get_unix_time();
        if (Now >= NextReportTime) {
            auto ElapsedTime_ns = 1e9 + (double)(Now - NextReportTime);
            auto ReportPeriod = ElapsedTime_ns / 1e9;
            auto PacketDelta = (double)m_TotalPkts - (double)PreviousN;
            PreviousN = m_TotalPkts.load();
            std::cout << "Sent " << m_TotalPkts
                      << " total packets, throughput: " << (PacketDelta / ReportPeriod)
                      << " pkts/sec" << std::endl;

            NextReportTime = utilities::get_unix_time() + (uint64_t)1e9;
        }
        if (Now >= NextTuningTime) {
            NextTuningTime = Now + (uint64_t)1e8;
            auto PacketDelta = ((int64_t)m_TotalPkts.load() - (int64_t)PreviousTuningN) - ExpectedPPSDiv10;
            PreviousTuningN = m_TotalPkts.load();
            if (PacketDelta > DeltaThreashold) {
                auto var =m_SpinDuration.load() ;
                if ( var > 0) {
                    var += (PacketDelta / DeltaThreashold) * 10;
                }
                if ( var < 1000) {
                    m_SpinDuration.store(100);
                }
                else{
                    m_SpinDuration.store(var);
                }
            }
            if (PacketDelta < (-DeltaThreashold)) {
                auto calc = (PacketDelta / DeltaThreashold) * 10;
                calc += m_SpinDuration.load();
                if ( calc < 1000 )
                    m_SpinDuration.store(100);
                else
                    m_SpinDuration.store(calc);
            }
        }
    }
}

void RioProducer::GroupStatsUpdate(const SOCKADDR_INET* addr,
                                   const size_t pktSize,
                                   const ProtocolHeader_t* pHdr) {
    uint32_t mcGroup = addr->Ipv4.sin_addr.s_addr;
    McGroupStats_t& gmc = m_GroupStats[mcGroup];

    gmc.Sequence.store(gmc.Packets.load());  // Sequence is behind 1.
    gmc.Packets++;
    gmc.Bytes.fetch_add(pktSize);
}

void RioProducer::GroupStatsPrint() {
    char inetspace[16];
    std::cout << "\n  Group           Packets        Bytes      Last Seq   " << std::endl;
    std::cout << "---------------------------------------------------------" << std::endl;

    for (auto const& [key, value] : m_GroupStats) {
        std::cout << inet_ntop(AF_INET, &key, inetspace, INET_ADDRSTRLEN) << "\t" << std::dec
                  << std::setw(10) << value.Packets << "  " << std::setw(12) << value.Bytes << "  "
                  << std::setw(10) << value.Sequence << std::endl;
    }
    std::cout << std::endl;
}

}  // namespace riosession
