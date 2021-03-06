// clang-format off
#include "args.hpp"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <random>

#include "Range.hpp"
#include "StringUtils.hpp"
//#include "swxtch-cpp-utils/StringUtils.hpp"
// clang-format on

using std::string;

void OptionParser::errorMessage(const string& message) const {
    std::cout << "Error: " << message << std::endl;
}

static Ipv4Vect ParseDestinations(const std::string& dest,
                                  const swxtch::net::Ipv4Addr_t& default_ipv4) {
    using namespace swxtch::str;
    using namespace swxtch::utils;
    Ipv4Vect ip_vec;

    auto ipv4Range = dest.empty() ? Range{default_ipv4} : MakeIpv4Range(dest);

    for (const auto& ipv4 : ipv4Range) {
        ip_vec.push_back(ipv4);
    }

    return ip_vec;
}

args_t OptionParser::ParseArguments() const {
    args_t args;
    argparse::ArgumentParser Parser(m_argv[0], m_version);
    Parser.add_argument("command").help("[producer|consumer] produce/consume multicast packets");
    Parser.add_argument("--nic")
        .default_value(string(DEFAULT_IFINDEX))
        .help("IfIndex or Name of NIC to use");
    Parser.add_argument("--mcast_ip")
        .default_value(string(MULTICAST_IP))
        .help("multicast group IP or range of groups IPs.");
    Parser.add_argument("--mcast_port")
        .default_value(MULTICAST_PORT)
        .help("multicast port")
        .action([](const string& value) {
            try {
                return std::stoi(value);
            } catch (const std::invalid_argument&) {
                std::cout << "Integer expected for port number";
                exit(1);
            }
        });
    Parser.add_argument("--total_pkts")
        .default_value(MAX_PKTS_TO_RECEIVE)
        .help("Total packets to send/receive. Insert 0 to not count")
        .action([](const string& value) {
            try {
                return std::stoi(value);
            } catch (const std::invalid_argument&) {
                std::cout << "Expected a valid packet number to receive";
                exit(1);
            }
        });
    Parser.add_argument("--pps")
        .default_value(PACKET_RATE_SEC)
        .help("(producer command only) packet-rate or packet per seconds")
        .action([](const string& value) {
            try {
                return std::stoi(value);
            } catch (const std::invalid_argument&) {
                std::cout << "Integer expected for spin value";
                exit(1);
            }
        });
    Parser.add_argument("--seconds")
        .default_value(RUN_FOR_NSEC)
        .help("Number of seconds to run the application. Insert 0 to run indefinitely")
        .action([](const string& value) {
            try {
                return std::stoi(value);
            } catch (const std::invalid_argument&) {
                std::cout << "Expected a valid number of seconds";
                exit(1);
            }
        });
    try {
        Parser.parse_args(m_argc, m_argv);
    } catch (const std::runtime_error& err) {
        std::cout << "error parsing command line arguments: " << err.what() << "\n";
        std::cout << Parser;
        exit(1);
    }

    args.Command = Parser.get<>("command").c_str();
    args.IfIndex = Parser.get<>("--nic").c_str();
    args.McastAddrStr
        = ParseDestinations(Parser.get<>("--mcast_ip"), swxtch::net::Ipv4Addr_t{"239.5.69.2"});
    args.McastPort = (uint16_t)Parser.get<int>("--mcast_port");
    args.PktsToCount = (uint32_t)Parser.get<int>("--total_pkts");
    args.PacketRate = Parser.get<int>("--pps");
    args.SecondsToRun = Parser.get<int>("--seconds");

    return args;
}

bool OptionParser::isValidMulticastIp(const Ipv4Vect& ipVect) const {
    bool flag = true;
    for (const auto mcipstr : ipVect) {
        size_t pos = 0;
        std::string token;
        std::string _mcipstr(mcipstr.str());
        std::vector<int> int_tokens;

        while ((pos = _mcipstr.find('.')) != string::npos) {
            token = _mcipstr.substr(0, pos);
            int_tokens.push_back(std::stoi(token));
            _mcipstr.erase(0, pos + 1);
        }
        int_tokens.push_back(std::stoi(_mcipstr));

        if (int_tokens.size() != 4) {
            return false;
        }

        uint32_t mcip = 0;
        for (auto i = int_tokens.begin(); i != int_tokens.end(); i++) {
            if ((*i > 255) || (*i < 0)) {
                return false;
            }
            mcip |= (uint8_t)*i;
            if (i < int_tokens.end() - 1) {
                mcip <<= 8;
            }
        }
        flag = (MIN_MC_IP <= mcip) && (mcip <= MAX_MC_IP);
        if (!flag) {
            return false;
        }
    }
    return true;
}

bool OptionParser::Check(const args_t* args) const {
    bool sanity_check = false;
    if (args != nullptr) {
        string cmd = args->Command;
        if (cmd != PRODUCER_COMMAND && cmd != CONSUMER_COMMAND) {
            errorMessage("Invalid Command. Expected producer or consumer.");
        } else if (!isValidMulticastIp(args->McastAddrStr)) {
            errorMessage(
                "Invalid Multicast IP. Expected value between 224.0.0.1 and 239.255.255.255");
        } else if (args->IfIndex == "") {
            errorMessage("Invalid IfIndex.");
        } else if ((args->McastPort > 49151) || (args->McastPort < 1024)) {
            errorMessage("Invalid Multicast Port. Expected value between 1024 and 49151.");
        } else if ((args->PacketRate * args->McastAddrStr.size() > 1000000)
                   || (args->PacketRate < 1)) {
            errorMessage(
                "Invalid Packet Rate. Expected a value between 0 and 1M. (Sum of all pps per "
                "group");
        } else {
            sanity_check = true;
        }
    }
    return sanity_check;
}
