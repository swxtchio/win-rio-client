#include "RioSession.hpp"

namespace riosession {

class RioProducer : public RioSession {
   private:
    void SendOnInterface(const std::string& iaddr);
    void InitMcAddrDescriptors() override;
    uint64_t PostFirstSend(DWORD totalMessages);
    void GroupStatsUpdate(const SOCKADDR_INET* addr,
                          const size_t pktSize,
                          const ProtocolHeader_t* pHdr) override;
    void GroupStatsPrint() override;
    void SpinWorker();

   private:
    std::atomic_int64_t m_SpinDuration;
    UINT m_NumberOfMcGroups;

   public:
    void Start() override;
    RioProducer(args_t* args,  volatile sig_atomic_t* signal);
    ~RioProducer() = default;
};

}  // namespace riosession