#pragma once

// clang-format off
#if defined _WINDOWS
    #include "swxtch-win.hpp"
#else
    #include <netdb.h>
    #include <sys/socket.h>
    #include <arpa/inet.h>
#endif

#include <climits>  // CHAR_BIT
#include <stdexcept>
#include <string>
#include <array>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <ostream>
// clang-format on

namespace swxtch {
namespace net {

#if defined _WINDOWS
constexpr uint8_t HOST_NAME_MAX = 64;
#endif

struct Ipv4Addr_t {
    uint32_t ip_be = 0;
    Ipv4Addr_t() = default;
    explicit Ipv4Addr_t(const std::string& v) {
        *this = v;
    }
    Ipv4Addr_t operator=(const std::string& v) {
        if (v.empty()) {
            ip_be = 0;
        } else {
            uint32_t ip_be_temp = 0;
            if (inet_pton(AF_INET, v.c_str(), &ip_be_temp) != 1) {
                throw std::invalid_argument{"Ipv4Addr_t: Invalid IP Address"};
            }
            ip_be = ip_be_temp;
        }
        return *this;
    }
    bool operator!=(const Ipv4Addr_t& v) const {
        return this->ip_be != v.ip_be;
    }
    bool operator>(const Ipv4Addr_t& v) const {
        return this->ip_be > v.ip_be;
    }
    Ipv4Addr_t operator+(uint32_t i) const {
        Ipv4Addr_t sum = *this;
        sum.setHostOrder(sum.ipHostOrder() + 1);
        return sum;
    }
    bool empty() const {
        return ip_be == 0;
    }
    void setNetOrder(uint32_t v) {
        ip_be = v;
    }
    void setHostOrder(uint32_t v) {
        ip_be = htonl(v);
    }
    void setLowerBits(Ipv4Addr_t subnet, uint32_t v) {
        setHostOrder((ipHostOrder() & subnet.ipHostOrder()) | (v & ~subnet.ipHostOrder()));
    }
    uint32_t lowerBits(Ipv4Addr_t subnet) const {
        return (ipHostOrder() & ~subnet.ipHostOrder());
    }
    uint32_t ipNetOrder() const {
        return ip_be;
    }
    uint32_t ipHostOrder() const {
        return ntohl(ip_be);
    }
    std::string str() const {
        char temp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_be, temp, INET_ADDRSTRLEN);
        return temp;
    }

    friend std::ostream& operator<<(std::ostream& os, const Ipv4Addr_t& ipv4) {
        return os << ipv4.str();
    }
};

}  // namespace net
}  // namespace swxtch
