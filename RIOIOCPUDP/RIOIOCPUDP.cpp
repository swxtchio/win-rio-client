#pragma once
#include "stdafx.h"

#include "../Shared/Constants.h"
#include "../Shared/Shared.h"
#include "argparse/argparse.hpp"
#include "../Shared/Utilities.hpp"
#include "auto_gen_ver_info.h"
#include <signal.h>  // sig_atomic_t

#define _HIDE_CONSOLE_LOG_

using std::cout;

// Console control handler routine
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
        // Handle the CTRL-C signal.
        case CTRL_C_EVENT:
            cout << "Ctrl-C detected" << endl;
            CloseRIOApp();
            exit(0);
            break;
        // Handle the CLOSE signal (Window X Button, End-Process, ALT-F4) ??.
        case CTRL_CLOSE_EVENT:
            cout << "Close event detected!" << endl;
            CloseRIOApp();
            exit(0);
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

int main(int argc, char** argv) {
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

    cout << "Config: " << endl;
    cout << "\tWaiting traffic from " << args.McastAddrStr.size() << " multicast groups" << endl;
    for (const auto mcAddr : args.McastAddrStr) {
        cout << "\tMCast group\t\t: " << mcAddr.str() << endl;
    }
    cout << "\tMCast Port\t\t: " << args.McastPort << endl;
    cout << "\tInterface IP Address\t: " << args.IfIndex << endl;


    if (SetConsoleCtrlHandler(CtrlHandler, TRUE) == 0) {
        ErrorExit("SetConsoleCtrlHandler for Ctrl-C.");
    }
    #ifndef _SINGLELINE_LOG_
        cout << "\tThe Control Handler is installed." << endl;
    #endif
    if (SetConsoleCtrlHandler(NULL, FALSE) == 0) {
        ErrorExit("SetConsoleCtrlHandler for normal Ctrl-C processing.");
    }
    #ifndef _SINGLELINE_LOG_
        cout << "\tNormal processing of Ctrl-C." << endl;
        cout << "\tPress Ctrl-C to exit" << endl;
    #endif

    GroupStatsMapInit(args);

    SetupTiming("RIO IOCP UDP");

    InitialiseWinsock();

    CreateRIOSocket(args);

    g_hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

    OVERLAPPED overlapped;

    RIO_NOTIFICATION_COMPLETION completionType;

    completionType.Type = RIO_IOCP_COMPLETION;
    completionType.Iocp.IocpHandle = g_hIOCP;
    completionType.Iocp.CompletionKey = (void*)0;
    completionType.Iocp.Overlapped = &overlapped;

    g_queue = g_rio.RIOCreateCompletionQueue(RIO_PENDING_RECVS, &completionType);

    if (g_queue == RIO_INVALID_CQ) {
        RioCleanUp();
        ErrorExit("RIOCreateCompletionQueue");
    }

    ULONG maxOutstandingReceive = RIO_PENDING_RECVS;
    ULONG maxReceiveDataBuffers = 1;
    ULONG maxOutstandingSend = 0;
    ULONG maxSendDataBuffers = 1;

    void* pContext = 0;

    g_requestQueue = g_rio.RIOCreateRequestQueue(g_s, maxOutstandingReceive, maxReceiveDataBuffers,
                                                 maxOutstandingSend, maxSendDataBuffers, g_queue,
                                                 g_queue, pContext);

    if (g_requestQueue == RIO_INVALID_RQ) {
        RioCleanUp();
        ErrorExit("RIOCreateRequestQueue");
    }

    PostRIORecvs(RECV_BUFFER_SIZE, RIO_PENDING_RECVS);

    if (SetConsoleCtrlHandler(CtrlHandler, TRUE) == 0) {
        ErrorExit("SetConsoleCtrlHandler for Ctrl-C.");
    }
    cout << "\tThe Control Handler is installed." << endl;
    if (SetConsoleCtrlHandler(NULL, FALSE) == 0) {
        ErrorExit("SetConsoleCtrlHandler for normal Ctrl-C processing.");
    }
    cout << "\tNormal processing of Ctrl-C." << endl;
    cout << "\tPress Ctrl-C to exit" << endl;

    bool done = false;

    RIORESULT results[RIO_MAX_RESULTS];

    INT notifyResult = g_rio.RIONotify(g_queue);

    if (notifyResult != ERROR_SUCCESS) {
        RioCleanUp();
        ErrorExit("RIONotify", notifyResult);
    }

    DWORD numberOfBytes = 0;

    ULONG_PTR completionKey = 0;

    OVERLAPPED* pOverlapped = 0;

    if (!::GetQueuedCompletionStatus(g_hIOCP, &numberOfBytes, &completionKey, &pOverlapped,
                                     INFINITE)) {
        RioCleanUp();
        ErrorExit("GetQueuedCompletionStatus");
    }

    ULONG numResults = g_rio.RIODequeueCompletion(g_queue, results, RIO_MAX_RESULTS);

    if (0 == numResults || RIO_CORRUPT_CQ == numResults) {
        RioCleanUp();
        ErrorExit("RIODequeueCompletion");
    }

    StartTiming();

    DWORD recvFlags = 0;

    int workValue = 0;

    EXTENDED_RIO_BUF* recvBuf = NULL;
    EXTENDED_RIO_BUF* addrLocBuf = NULL;
    EXTENDED_RIO_BUF* addrRemBuf = NULL;
    char* addrLocOffset = NULL;
    char* addrRemOffset = NULL;

    uint64_t totalOutOfOrder = 0;
    uint64_t lastOutOfOrder = 0;

    do {
        for (DWORD i = 0; i < numResults; ++i) {
            recvBuf = reinterpret_cast<EXTENDED_RIO_BUF*>(results[i].RequestContext);

            if (results[i].BytesTransferred == EXPECTED_DATA_SIZE) {
                g_packets++;

                workValue += DoWork(g_workIterations);

                if (!g_rio.RIOReceiveEx(
                        g_requestQueue, recvBuf, 1,
                        &g_addrLocRioBufs[g_addrLocRioBufIndex % g_addrLocRioBufTotalCount],
                        NULL, //&g_addrRemRioBufs[g_addrRemRioBufIndex % g_addrRemRioBufTotalCount],
                        NULL, 0, 0, recvBuf)) {
                    RioCleanUp();
                    ErrorExit("RIOReceiveEx Local Address");
                }


                pHdr = (ProtocolHeader_t*)(g_recvBufferPointer + recvBuf->Offset);

                #ifndef _HIDE_CONSOLE_LOG_
                    ShowHdr(pHdr);
                #endif

                addrLocBuf = &(g_addrLocRioBufs[g_addrLocRioBufIndex++ % g_addrLocRioBufTotalCount]);

                addrLocOffset = g_addrLocBufferPointer + addrLocBuf->Offset;

                #ifndef _HIDE_CONSOLE_LOG_
                    ShowAddr((SOCKADDR_INET*)addrLocOffset);
                #endif

                /*
                addrRemBuf = &(g_addrRemRioBufs[g_addrRemRioBufIndex++ % g_addrRemRioBufTotalCount]);

                addrRemOffset = g_addrRemBufferPointer + addrRemBuf->Offset;

                #ifndef _HIDE_CONSOLE_LOG_
                    ShowAddr((SOCKADDR_INET*)addrRemOffset);
                #endif
                */

                GroupStatsMapUpdate((SOCKADDR_INET*)addrLocOffset,
                                    EXPECTED_DATA_SIZE, pHdr);

                if (args.pktsToCount > 0) {
                    done = (g_packets >= args.pktsToCount);
                }

                if ((g_packets % (RIO_MAX_RESULTS * 4)) == 0) {

                    if (((totalOutOfOrder = GroupStatsMapTotalOOO()) > 0)
                        && (totalOutOfOrder > lastOutOfOrder)) {
                        lastOutOfOrder = totalOutOfOrder;
                        cout << "OutOfOrder Count = " << totalOutOfOrder << endl;
                    }
                }
            } else {
                g_otherPkts++;
            }
        }

        if (!done) {
            notifyResult = g_rio.RIONotify(g_queue);

            if (notifyResult != ERROR_SUCCESS) {
                RioCleanUp();
                ErrorExit("RIONotify");
            }

            if (!::GetQueuedCompletionStatus(g_hIOCP, &numberOfBytes, &completionKey,
                                                &pOverlapped, INFINITE)) {
                RioCleanUp();
                ErrorExit("GetQueuedCompletionStatus");
            }

            numResults = g_rio.RIODequeueCompletion(g_queue, results, RIO_MAX_RESULTS);

            if (RIO_CORRUPT_CQ == numResults) {
                RioCleanUp();
                ErrorExit("RIODequeueCompletion");
            }

        }
    } while (!done);

    CloseRIOApp();

    return workValue;
}
