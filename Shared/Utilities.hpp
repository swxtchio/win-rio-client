#pragma once

#include <string>
#include <codecvt>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <Windows.h>
#include <Msi.h>

#pragma comment(lib, "iphlpapi")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "MSI.lib")

namespace utilities {

bool nicIsNumber(const std::string& nicString) {
    for (char const& c : nicString) {
        if (std::isdigit(c) == 0)
            return false;
    }
    return true;
}

// TODO: wstring_convert and codecvt are deprecated in C++17.
std::string wstr_to_str(const std::wstring& wstr) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
    return convert.to_bytes(wstr);
}

uint8_t LocateAdapterWithIfName(std::string ifNameToFind) {
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

void FreeAdapterInfo(IP_ADAPTER_INFO* AdapterInfo) {
    if (AdapterInfo) {
        free(AdapterInfo);
    }
}

IP_ADAPTER_INFO* CreateAdapterInfo() {
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

IP_ADAPTER_INFO* LocateAdapterByIndex(int index, IP_ADAPTER_INFO* AdapterInfo) {
    IP_ADAPTER_INFO* LocatedAdapter = nullptr;
    for (IP_ADAPTER_INFO* Adapter = AdapterInfo; Adapter; Adapter = Adapter->Next) {
        if (Adapter->Index == index) {
            LocatedAdapter = Adapter;
            break;
        }
    }
    return LocatedAdapter;
}

std::string GetInterfaceIpAddress(const std::string& ifname) {
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

}  // namespace utilities