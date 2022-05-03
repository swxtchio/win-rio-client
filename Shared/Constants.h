
static const DWORD EXPECTED_DATA_SIZE = 168;

static const DWORD RECV_BUFFER_SIZE = EXPECTED_DATA_SIZE;
static const DWORD SEND_BUFFER_SIZE = EXPECTED_DATA_SIZE;

static const long DATAGRAMS_TO_SEND = 50000000;
static const long SHUTDOWN_DATAGRAMS_TO_SEND = 1000;

static const DWORD RIO_PENDING_RECVS = 1500000;

static const DWORD SPIN_COUNT = 4000;

static const DWORD RIO_MAX_RESULTS = 1000;

static const DWORD RIO_RESULTS_THRESHOLD = (RIO_MAX_RESULTS / 3) * 2;

static const DWORD IOCP_PENDING_RECVS = 5000;

static const DWORD GQCSEX_MAX_RESULTS = 1000;

static const unsigned short PORT = 8888;
static const char* MC_IP = "239.5.69.2";

static const DWORD TIMING_THREAD_AFFINITY_MASK = 1;

static const DWORD NUM_IOCP_THREADS = 8;

static const bool USE_LARGE_PAGES = false;

#define TRACK_THREAD_STATS
//#define WE_RESET_EVENT

