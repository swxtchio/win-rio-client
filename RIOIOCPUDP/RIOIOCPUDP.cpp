#pragma once
#include "stdafx.h"

#include "../Shared/Constants.h"
#include "../Shared/Shared.h"
#include "argparse/argparse.hpp"
#include "../Shared/Utilities.hpp"
#include "auto_gen_ver_info.h"

using std::cout;

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

    //
    cout << "Config: " << endl;
    cout << "\tWaiting traffic from " << args.McastAddrStr.size() << " multicast groups" << endl;
    for (const auto mcAddr : args.McastAddrStr) {
        cout << "Mcast group: " << mcAddr.str() << endl;
    }
    cout << "\tMCast Port    : " << args.McastPort << endl;
    cout << "\tInterface IP Address     : " << args.IfIndex << endl;
    cout << "\tCounting a total of: " << args.pktsToCount << " packets" << endl;
    //

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
        ErrorExit("RIOCreateRequestQueue");
    }

    PostRIORecvs(RECV_BUFFER_SIZE, RIO_PENDING_RECVS);

    bool done = false;

    RIORESULT results[RIO_MAX_RESULTS];

    INT notifyResult = g_rio.RIONotify(g_queue);

    if (notifyResult != ERROR_SUCCESS) {
        ErrorExit("RIONotify", notifyResult);
    }

    DWORD numberOfBytes = 0;

    ULONG_PTR completionKey = 0;

    OVERLAPPED* pOverlapped = 0;

    if (!::GetQueuedCompletionStatus(g_hIOCP, &numberOfBytes, &completionKey, &pOverlapped,
                                     INFINITE)) {
        ErrorExit("GetQueuedCompletionStatus");
    }

    ULONG numResults = g_rio.RIODequeueCompletion(g_queue, results, RIO_MAX_RESULTS);

    if (0 == numResults || RIO_CORRUPT_CQ == numResults) {
        ErrorExit("RIODequeueCompletion");
    }

    StartTiming();

    DWORD recvFlags = 0;

    int workValue = 0;

    do {
        for (DWORD i = 0; i < numResults; ++i) {
            EXTENDED_RIO_BUF* pBuffer
                = reinterpret_cast<EXTENDED_RIO_BUF*>(results[i].RequestContext);

            if (results[i].BytesTransferred == EXPECTED_DATA_SIZE) {
                g_packets++;

                workValue += DoWork(g_workIterations);

                if (!g_rio.RIOReceive(g_requestQueue, pBuffer, 1, recvFlags, pBuffer)) {
                    ErrorExit("RIOReceive");
                }

                // done = false;
                done = (g_packets >= args.pktsToCount);
            } else {
                done = true;
                printf("results[i].BytesTransferred=%u\n", results[i].BytesTransferred);
            }
        }

        if (!done) {
            const INT notifyResult = g_rio.RIONotify(g_queue);

            if (notifyResult != ERROR_SUCCESS) {
                ErrorExit("RIONotify");
            }

            if (!::GetQueuedCompletionStatus(g_hIOCP, &numberOfBytes, &completionKey, &pOverlapped,
                                             INFINITE)) {
                ErrorExit("GetQueuedCompletionStatus");
            }

            numResults = g_rio.RIODequeueCompletion(g_queue, results, RIO_MAX_RESULTS);

            if (0 == numResults || RIO_CORRUPT_CQ == numResults) {
                ErrorExit("RIODequeueCompletion");
            }
        }
    } while (!done);

    StopTiming();

    PrintTimings();

    return workValue;
}
