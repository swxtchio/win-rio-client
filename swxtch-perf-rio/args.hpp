#pragma once
// clang-format off
#include <stdint.h>
#include <string>
#include <vector>
#include "argparse/argparse.hpp"
#include "NetworkUtils.hpp"
// clang-format on

using Ipv4Vect = std::vector<swxtch::net::Ipv4Addr_t>;

using args_t = struct args_s {
    std::string Command;
    Ipv4Vect McastAddrStr;
    uint16_t McastPort;
    std::string IfIndex;
    uint64_t PktsToCount;
    int PacketRate;
    int SecondsToRun;
};

constexpr char MULTICAST_IP[] = "239.5.69.2";
constexpr size_t MIN_MC_IP = 0xE0000001;
constexpr size_t MAX_MC_IP = 0xEFFFFFFF;
constexpr char DEFAULT_IFINDEX[] = "Ethernet";
constexpr int MULTICAST_PORT = 10000;
constexpr int MAX_PKTS_TO_RECEIVE = 20000000;
constexpr char CONSUMER_COMMAND[] = "consumer";
constexpr char PRODUCER_COMMAND[] = "producer";
constexpr int PACKET_RATE_SEC = 1;
constexpr int RUN_FOR_NSEC = 0;

class OptionParser {
   public:
    OptionParser(int argc, char** argv, const std::string& ver)
        : m_argc(argc), m_argv(argv), m_version(ver) {
    }
    args_t ParseArguments() const;
    bool Check(const args_t*) const;

   private:
    void errorMessage(const std::string&) const;
    bool isValidMulticastIp(const Ipv4Vect&) const;

    int m_argc;
    char** m_argv;
    std::string m_version;
};
