#pragma once
// clang-format off
#include <iostream>
#include <string>
#include <sstream>
#include <codecvt>
#include <process.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <Windows.h>
#include <Msi.h>
#include <chrono>
// clang-format on

#pragma comment(lib, "iphlpapi")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "MSI.lib")

namespace utilities {

constexpr uint64_t ONE_SECOND = 1000000000;

inline bool nicIsNumber(const std::string& nicString) {
    for (char const& c : nicString) {
        if (std::isdigit(c) == 0)
            return false;
    }
    return true;
}

inline uint64_t get_unix_time(void) {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

/**
 * @brief Wait for @spin nanoseconds. Wasting CPU time
 * 
 * @param nano 
 */
inline void spin(uint64_t nano) {
    if (nano <= 1000) {
        return;
    }
    auto StartTime(std::chrono::high_resolution_clock::now());
    while (true) {
        auto CurrentTime(std::chrono::high_resolution_clock::now());
        auto ElapsedTime_ns
            = std::chrono::duration_cast<std::chrono::nanoseconds>(CurrentTime - StartTime).count();
        if (ElapsedTime_ns >= nano)
            break;
    }
}

// TODO: wstring_convert and codecvt are deprecated in C++17.
inline std::string wstr_to_str(const std::wstring& wstr) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
    return convert.to_bytes(wstr);
}

inline uint8_t LocateAdapterWithIfName(std::string ifNameToFind) {
    uint8_t ifIndex = 0;

    // Set the flags to pass to GetAdaptersAddresses
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    // Specify IPv4 as default query
    ULONG family = AF_INET;
    ULONG outBufLen = sizeof(IP_ADAPTER_ADDRESSES);
    PIP_ADAPTER_ADDRESSES pAddressesInfo = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);

    // Make an initial call to GetAdaptersAddresses to get the
    // size needed into the outBufLen variable
    if (GetAdaptersAddresses(family, flags, NULL, pAddressesInfo, &outBufLen)
        == ERROR_BUFFER_OVERFLOW) {
        free(pAddressesInfo);
        pAddressesInfo = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
    }

    if (pAddressesInfo == NULL) {
        std::ostringstream what;
        what << "--- Memory Allocation failed for IP_ADAPTER_ADDRESSES struct " << std::endl;
        throw std::runtime_error(what.str());
    }

    if (DWORD dwRetValue = GetAdaptersAddresses(family, flags, NULL, pAddressesInfo, &outBufLen);
        dwRetValue != NO_ERROR) {
        // TODO: Check this error control with the example provided by Microsoft in
        // https://docs.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersaddresses
        if (dwRetValue == ERROR_NO_DATA) {
            std::ostringstream what;
            what << "--- Unable to found any IPv4 Interface " << std::endl;
            throw std::runtime_error(what.str());
        } else {
            free(pAddressesInfo);
            std::ostringstream what;
            what << "-- Unexpected error trying to get Interface Info " << std::endl;
            throw std::runtime_error(what.str());
        }
    }

    PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddressesInfo;
    bool findFlag = false;
    // Check if @ifNameToFind is a numerical index or
    // a interface name. Example: "15" represents and index
    // "Ethernet 2" represents an interface name.
    // Loop around until it finds it, or not.
    bool isNumeric = nicIsNumber(ifNameToFind);
    while (pCurrAddresses) {
        std::string fName = (isNumeric) ? std::to_string(pCurrAddresses->IfIndex)
                                        : wstr_to_str(std::wstring(pCurrAddresses->FriendlyName));
        if (fName == ifNameToFind) {
            findFlag = true;
            ifIndex = pCurrAddresses->IfIndex;
            break;
        }
        pCurrAddresses = pCurrAddresses->Next;
    }

    if (!findFlag) {
        std::ostringstream what;
        if (isNumeric)
            what << "--- No interface with index: " << ifNameToFind << " was found " << std::endl;
        else
            what << "--- No interface with name: " << ifNameToFind << " was found " << std::endl;
        throw std::runtime_error(what.str());
    }

    return ifIndex;
}

inline void FreeAdapterInfo(IP_ADAPTER_INFO* AdapterInfo) {
    if (AdapterInfo) {
        free(AdapterInfo);
    }
}

inline IP_ADAPTER_INFO* CreateAdapterInfo() {
    ULONG Buflen = sizeof(IP_ADAPTER_INFO);
    IP_ADAPTER_INFO* AdapterInfo = (IP_ADAPTER_INFO*)malloc(Buflen);

    if (GetAdaptersInfo(AdapterInfo, &Buflen) == ERROR_BUFFER_OVERFLOW) {
        free(AdapterInfo);
        AdapterInfo = (IP_ADAPTER_INFO*)malloc(Buflen);
    }
    if (GetAdaptersInfo(AdapterInfo, &Buflen) == NO_ERROR) {
        return AdapterInfo;
    }
    FreeAdapterInfo(AdapterInfo);
    return nullptr;
}

inline IP_ADAPTER_INFO* LocateAdapterByIndex(int index, IP_ADAPTER_INFO* AdapterInfo) {
    IP_ADAPTER_INFO* LocatedAdapter = nullptr;
    for (IP_ADAPTER_INFO* Adapter = AdapterInfo; Adapter; Adapter = Adapter->Next) {
        if (Adapter->Index == index) {
            LocatedAdapter = Adapter;
            break;
        }
    }
    return LocatedAdapter;
}

inline std::string GetInterfaceIpAddress(const std::string& ifname) {
    std::string IpAddress = "0.0.0.0";
    IP_ADAPTER_INFO* AdapterInfo = CreateAdapterInfo();
    if (AdapterInfo) {
        int index;
        try {
            index = stoi(ifname);
        } catch (std::invalid_argument e) {
            return IpAddress;
        }
        IP_ADAPTER_INFO* Adapter = LocateAdapterByIndex(index, AdapterInfo);
        if (Adapter) {
            IpAddress = std::string(Adapter->IpAddressList.IpAddress.String);
        }
    }
    FreeAdapterInfo(AdapterInfo);
    return IpAddress;
}

inline std::string GetLastErrorMessage(DWORD last_error, bool stripTrailingLineFeed = true) {
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
    std::cout << "Error: " << pFunction << " failed: " << lastError << std::endl;
    std::cout << GetLastErrorMessage(lastError) << std::endl;
    exit(0);
}

inline void ErrorExit(const char* pFunction) {
    const DWORD lastError = ::GetLastError();

    ErrorExit(pFunction, lastError);
}

template <typename TV, typename TM>
inline TV RoundDown(TV Value, TM Multiple) {
    return ((Value / Multiple) * Multiple);
}

template <typename TV, typename TM>
inline TV RoundUp(TV Value, TM Multiple) {
    return (RoundDown(Value, Multiple) + (((Value % Multiple) > 0) ? Multiple : 0));
}

}  // namespace utilities
