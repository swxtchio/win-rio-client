#include "RioConsumer.hpp"
#include "RioProducer.hpp"
#include "auto_gen_ver_info.h"

static volatile sig_atomic_t g_Exit = 0;

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType) {
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
            std::cout << "CTRL-C Detected. The application will close."<<std::endl;
            g_Exit = 1;
            return TRUE;
        default:
            return FALSE;
    }
}

std::string version() {
    std::stringstream ss_version;
    ss_version << "{\"Version\":\"" << APP_VERSION << "\",\"Commit\":\"" << GIT_COMMIT
               << "\",\"Date\":\"" << DATE << "\"}";
    return ss_version.str();
}

void InitializeWSA() {
    WSADATA data;
    WORD wVersionRequested = 0x202;

    if (0 != ::WSAStartup(wVersionRequested, &data)) {
        utilities::ErrorExit("WSAStartup");
    }

    if (riosession::LARGE_PAGES_ENABLED) {
        // check that we have SetLockMemoryPrivilege and enable it
        utilities::ErrorExit("TODO - USE_LARGE_PAGES");
    }
}

int main(int argc, char** argv) {
    using namespace riosession;
    OptionParser op(argc, argv, version());
    args_t args = op.ParseArguments();
    if (!op.Check(&args)) {
        return 1;
    }

    try {
        // Check if the index is a valid one, and override the struct string with
        // the actual IP Address (xxx.xxx.xxx.xxx)
        args.IfIndex = std::to_string(utilities::LocateAdapterWithIfName(args.IfIndex));
        args.IfIndex = utilities::GetInterfaceIpAddress(args.IfIndex);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return -1;
    }

    //
    std::cout << "Config: " << std::endl;
    std::cout << "\tWaiting traffic from " << args.McastAddrStr.size() << " multicast groups"
              << std::endl;
    for (const auto mcAddr : args.McastAddrStr) {
        std::cout << "\tMcast group: " << mcAddr.str() << std::endl;
    }
    std::cout << "\tMCast Port    : " << args.McastPort << std::endl;
    std::cout << "\tInterface IP Address     : " << args.IfIndex << std::endl;
    if (args.PktsToCount)
        std::cout << "\tCounting a total of: " << args.PktsToCount << " packets" << std::endl;
    else
        std::cout << "\tRunning without a total packet counter limit" << std::endl;
    if (args.SecondsToRun)
        std::cout << "\tRunning the application for at least: " << args.SecondsToRun << "seconds"
                  << std::endl;
    else
        std::cout << "\tRunning the application without a timing limit" << std::endl;
    //
    InitializeWSA();
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);
    if (args.Command == PRODUCER_COMMAND) {
        RioProducer rioProducer(&args, &g_Exit);
        rioProducer.Start();
        rioProducer.CleanUpRIO();
    } else {
        RioConsumer rioConsumer(&args, &g_Exit);
        rioConsumer.Start();
        rioConsumer.CleanUpRIO();
    }
    WSACleanup();
}
