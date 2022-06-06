#include "RioSession.hpp"

namespace riosession {

class RioConsumer : public RioSession {
   private:
    int JoinGroup(UINT32 grpaddr, UINT32 iaddr);
    void JoinGroups(Ipv4Vect mcastAddrs);
    void PostFirstRecvs(DWORD totalMessages);
    void GroupStatsUpdate(const SOCKADDR_INET* addr,
                          const size_t pktSize,
                          const ProtocolHeader_t* pHdr) override;
    void GroupStatsPrint() override;
    void InitMcAddrDescriptors() override;
    void PrintReportHeader();
    void PrintReportRow(const TotalStats_t& stats, const uint64_t& oooNow, const uint64_t& missNow, const double& pps, const double& bps);
    void ReportWorker();
    TotalStats_t GetMcTotals();

   public:
    void Start() override;
    RioConsumer(args_t* args, volatile sig_atomic_t* signal);
    ~RioConsumer() = default;
};

}  // namespace riosession
