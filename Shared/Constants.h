
static const unsigned short PORTNUM = 10000;


static const DWORD EXPECTED_DATA_SIZE = 100;

static const DWORD RECV_BUFFER_SIZE = 256;
static const DWORD SEND_BUFFER_SIZE = 256;

static const DWORD ADDR_BUFFER_SIZE = 64;


static const DWORD RIO_PENDING_RECVS = 4096;  // 1500000;
//static const DWORD RIO_PENDING_RECVS = 100000;
static const DWORD RIO_PENDING_SENDS = 10000;

static const long DATAGRAMS_TO_SEND = 50000000;
static const long SHUTDOWN_DATAGRAMS_TO_SEND = 1000;

static const DWORD SPIN_COUNT = 4000;

static const DWORD RIO_MAX_RESULTS = 2000;

static const DWORD RIO_RESULTS_THRESHOLD = (RIO_MAX_RESULTS / 3) * 2;

static const DWORD IOCP_PENDING_RECVS = 5000;

static const DWORD GQCSEX_MAX_RESULTS = 1000;

//static const unsigned short PORT = 8888;
//static const char* MC_IP = "239.5.69.2";

static const DWORD TIMING_THREAD_AFFINITY_MASK = 1;

static const DWORD NUM_IOCP_THREADS = 8;

static const bool USE_LARGE_PAGES = false;

#define TRACK_THREAD_STATS
//#define WE_RESET_EVENT

