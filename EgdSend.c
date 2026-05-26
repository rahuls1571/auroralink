/*******************************************************************************/
/* EgdSend.c
   This program is an example of how to send an EGD Data Production Message
   that conforms to the EGD 2.01 specification.

   To build in Linux:
      gcc EgdSend.c -o EgdSend -lm

   Usage:
      EgdSend [destination] [interval_ms] [count]

      destination  - IP address or hostname to send to (default: 192.168.101.255)
      interval_ms  - interval between sends in milliseconds (default: 1000)
      count        - number of messages to send, 0 = infinite (default: 0)

   Examples:
      EgdSend                              # broadcast, 1 second interval, infinite
      EgdSend 192.168.1.100               # unicast, 1 second interval, infinite
      EgdSend 192.168.1.100 500           # unicast, 500ms interval, infinite
      EgdSend 192.168.1.100 500 10        # unicast, 500ms interval, 10 messages
*/

#include "EgdTest.h"

/* Default configuration */
#define DEFAULT_DESTINATION     "192.168.101.255"
#define DEFAULT_INTERVAL_MS     1000        /* 1 second */
#define DEFAULT_COUNT           0           /* 0 = infinite */
#define MIN_INTERVAL_MS         10          /* 10ms minimum to prevent flooding */
#define MAX_INTERVAL_MS         60000       /* 60 seconds maximum */

/* High-precision timing statistics */
typedef struct
{
    double minJitterMs;     /* Minimum observed jitter in ms */
    double maxJitterMs;     /* Maximum observed jitter in ms */
    double totalJitterMs;   /* Accumulated jitter for average calculation */
    double minSendTimeMs;   /* Minimum observed send time in ms */
    double maxSendTimeMs;   /* Maximum observed send time in ms */
    double totalSendTimeMs; /* Accumulated send time for average calculation */
    unsigned long samples;  /* Number of samples collected */
} TimingStats;

/* Flag to allow graceful shutdown on Ctrl+C */
#ifndef _WIN32
#include <signal.h>
static volatile int g_running = 1;
static void SignalHandler(int sig)
{
    (void)sig;
    g_running = 0;
}
#endif

/**************************************************************************/
/* Timespec helper functions                                               */
/**************************************************************************/

/* Add milliseconds to a timespec */
static void TimespecAddMs(struct timespec* ts, int ms)
{
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

/* Subtract two timespecs: result = a - b, returned in milliseconds */
static double TimespecDiffMs(const struct timespec* a, const struct timespec* b)
{
    return (double)(a->tv_sec  - b->tv_sec)  * 1000.0 +
           (double)(a->tv_nsec - b->tv_nsec) / 1000000.0;
}

/* Sleep until an absolute time using CLOCK_MONOTONIC.
   Returns 0 on success, non-zero if interrupted by a signal.
*/
static int SleepUntil(const struct timespec* wakeTime)
{
#ifdef _WIN32
    /* On Windows, calculate relative sleep time */
    struct timespec now;
    double remaining;
    clock_gettime(CLOCK_MONOTONIC, &now);
    remaining = TimespecDiffMs(wakeTime, &now);
    if (remaining > 0.0)
        Sleep((DWORD)remaining);
    return 0;
#else
    /* Use clock_nanosleep for absolute time sleep on Linux.
       This is more accurate than nanosleep() with a relative time
       because it avoids cumulative drift from signal interruptions.
    */
    int ret;
    while ((ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                  wakeTime, NULL)) != 0)
    {
        if (ret == EINTR)
        {
            /* Interrupted by signal — check if we should exit */
            if (!g_running)
                return 1;
            /* Otherwise retry the sleep */
            continue;
        }
        /* Any other error */
        return 1;
    }
    return 0;
#endif
}

/**************************************************************************/
/* Update timing statistics */
static void UpdateTimingStats(TimingStats* pStats, double sendTimeMs,
                               double jitterMs)
{
    /* Ignore first sample — it's often an outlier */
    if (pStats->samples == 0)
    {
        pStats->minJitterMs  = jitterMs;
        pStats->maxJitterMs  = jitterMs;
        pStats->minSendTimeMs = sendTimeMs;
        pStats->maxSendTimeMs = sendTimeMs;
    }
    else
    {
        if (jitterMs   < pStats->minJitterMs)  pStats->minJitterMs  = jitterMs;
        if (jitterMs   > pStats->maxJitterMs)  pStats->maxJitterMs  = jitterMs;
        if (sendTimeMs < pStats->minSendTimeMs) pStats->minSendTimeMs = sendTimeMs;
        if (sendTimeMs > pStats->maxSendTimeMs) pStats->maxSendTimeMs = sendTimeMs;
    }
    pStats->totalJitterMs   += jitterMs;
    pStats->totalSendTimeMs += sendTimeMs;
    pStats->samples++;
}

/* Print timing statistics */
static void PrintTimingStats(const TimingStats* pStats, int intervalMs)
{
    if (pStats->samples == 0)
        return;

    printf("\n--- Timing Statistics ---\n");
    printf("  Samples       : %lu\n",   pStats->samples);
    printf("  Target interval: %d ms\n", intervalMs);
    printf("  Send time  min : %.3f ms\n", pStats->minSendTimeMs);
    printf("  Send time  max : %.3f ms\n", pStats->maxSendTimeMs);
    printf("  Send time  avg : %.3f ms\n",
           pStats->totalSendTimeMs / (double)pStats->samples);
    printf("  Jitter     min : %.3f ms\n", pStats->minJitterMs);
    printf("  Jitter     max : %.3f ms\n", pStats->maxJitterMs);
    printf("  Jitter     avg : %.3f ms\n",
           pStats->totalJitterMs / (double)pStats->samples);
    printf("-------------------------\n");
}

/**************************************************************************/
/* This function gets the current time and formats it in the
   format required by EGD.
*/
void GetCurrentEgdTime(EGD_Time *pTime)
{
#ifdef _WIN32
    struct _timeb Tm;
    _ftime(&Tm);
    pTime->tv_sec  = (uint32_t)Tm.time;
    pTime->tv_nsec = Tm.millitm * 1000000;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    pTime->tv_sec  = (uint32_t)ts.tv_sec;
    pTime->tv_nsec = (int32_t)ts.tv_nsec;
#endif
}

/**************************************************************************/
/* This function converts a string to an IP address.
   Returns 0 on success and non-zero on error.
*/
int ParseAddressString(const char* addressString, struct sockaddr_in* pSockAddr)
{
    in_addr_t ba = 0;

    if ((ba = inet_addr(addressString)) == INADDR_NONE)
    {
        struct hostent *host_info;
        if ((host_info = gethostbyname(addressString)) == NULL)
        {
            if (strcmp("255.255.255.255", addressString) != 0)
            {
                fprintf(stderr, "ParseAddressString: could not resolve '%s'\n",
                        addressString);
                return 1;
            }
            ba = (in_addr_t)-1;
        }
        else
        {
            memcpy(&ba, host_info->h_addr_list[0], sizeof(in_addr_t));
        }
    }

    pSockAddr->sin_family      = AF_INET;
    pSockAddr->sin_addr.s_addr = ba;
    pSockAddr->sin_port        = htons(EGD_DEFAULT_PORT);

    return 0;
}

/**************************************************************************/
/* This function initializes the socket used to send the message.
   Returns 0 on success and non-zero on error.
*/
int SetupSendSocket(SOCKET* pSock, struct sockaddr_in* pBindAddr)
{
    int i;

    if ((*pSock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
    {
        egd_perror("socket");
        return 1;
    }

    memset(pBindAddr, 0, sizeof(struct sockaddr_in));
    pBindAddr->sin_family      = AF_INET;
    pBindAddr->sin_addr.s_addr = htonl(INADDR_ANY);
    pBindAddr->sin_port        = 0;

    if (bind(*pSock, (struct sockaddr *)pBindAddr,
             sizeof(struct sockaddr_in)) == SOCKET_ERROR)
    {
        egd_perror("bind");
        return 2;
    }

    i = 1;
    if (setsockopt(*pSock, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i)))
    {
        egd_perror("setsockopt SO_BROADCAST");
        return 3;
    }

    i = 32 * 1024;
    if (setsockopt(*pSock, SOL_SOCKET, SO_SNDBUF, (char *)&i, sizeof(i)))
    {
        egd_perror("setsockopt SO_SNDBUF");
        return 4;
    }

    return 0;
}

/**************************************************************************/
/* This function initializes the output header of an EGD Data Production Message. */
void InitHeader(Data_Production_Hdr* pHdr, DWORD producerId,
                DWORD exchangeId, WORD signature)
{
    memset(pHdr, 0, sizeof(Data_Production_Hdr));
    pHdr->PDUType    = EGD_PDU_TYPE_DATA_PRODUCTION;
    pHdr->Version    = 1;
    pHdr->ReqID      = 0;
    pHdr->ProducerID = producerId;
    pHdr->ExchangeID = exchangeId;
    pHdr->Status     = EGD_STATUS_HEALTHY_NOT_SYNCED;
    pHdr->Signature  = signature;
}

/**************************************************************************/
/* This function sends the Data Production Message and updates the header
   fields ready for the next send.
   Returns 0 on success and non-zero on failure.
*/
int SendDataMessage(SOCKET* pSock, struct sockaddr_in* pSockAddr,
                    Data_Production_Message* pMsg, int dataSize)
{
    int sendStatus;
    int totalSize = (int)sizeof(Data_Production_Hdr) + dataSize;

    GetCurrentEgdTime(&pMsg->Hdr.Timestamp);

    sendStatus = sendto(*pSock, (char*)pMsg, totalSize, 0,
                        (struct sockaddr *)pSockAddr,
                        sizeof(struct sockaddr_in));

    if (sendStatus == SOCKET_ERROR)
    {
        egd_perror("sendto");
        return 1;
    }

    pMsg->Hdr.ReqID++;
    return 0;
}

/**************************************************************************/
/* Populate the data portion of the message with simulated application data. */
static void UpdateMessageData(Data_Production_Message* pMsg,
                               unsigned long messageCount)
{
    double val;

    pMsg->Data[0] = (BYTE)(messageCount & 0xFF);
    pMsg->Data[4] = (BYTE)(messageCount & 0xFF);

    strncpy((char*)(&pMsg->Data[6]), "This is a test.",
            sizeof(pMsg->Data) - 6 - 1);
    pMsg->Data[sizeof(pMsg->Data) - 1] = '\0';

    val = sin((double)messageCount * 0.1) * 100.0;
    memcpy(&pMsg->Data[40], &val, sizeof(double));
}

/**************************************************************************/
/* Main send loop with high-precision timing.
  
   Strategy:
     - Record the absolute time each send SHOULD occur (nextWakeTime)
     - After each send, advance nextWakeTime by exactly intervalMs
     - Sleep until nextWakeTime using clock_nanosleep(TIMER_ABSTIME)
     - This means send time and any minor overruns are automatically
       compensated for in the next interval — no cumulative drift
*/
int SenderExample(SOCKET* pSock, char* destinationAddress,
                  int intervalMs, unsigned long count)
{
    struct sockaddr_in      bindSockAddress;
    struct sockaddr_in      destSockAddress;
    Data_Production_Message OutputMsg;
    DWORD                   producerId;
    DWORD                   exchangeId;
    WORD                    signature;
    int                     dataSize;
    unsigned long           messageCount = 0;
    unsigned long           totalSent    = 0;
    int                     sendFailed   = 0;
    TimingStats             stats;

    /* High-precision timing */
    struct timespec         nextWakeTime;   /* Absolute time of next send */
    struct timespec         sendStart;      /* Time before send */
    struct timespec         sendEnd;        /* Time after send */
    struct timespec         actualWake;     /* Actual time we woke up */
    double                  sendTimeMs;     /* How long the send took */
    double                  jitterMs;       /* How late we woke vs scheduled */

    /* Configuration */
    producerId = 42;
    exchangeId = 1;
    signature  = 0;
    dataSize   = 50;

    memset(&stats, 0, sizeof(TimingStats));

    /* Validate */
    if (dataSize < 0 || dataSize > EGD_MAX_DATA_SIZE)
    {
        fprintf(stderr, "SenderExample: invalid dataSize %d\n", dataSize);
        return 1;
    }
    if (intervalMs < MIN_INTERVAL_MS || intervalMs > MAX_INTERVAL_MS)
    {
        fprintf(stderr, "SenderExample: intervalMs %d out of range [%d, %d]\n",
                intervalMs, MIN_INTERVAL_MS, MAX_INTERVAL_MS);
        return 1;
    }

    if (SetupSendSocket(pSock, &bindSockAddress) != 0)
        return 2;

    if (ParseAddressString(destinationAddress, &destSockAddress) != 0)
        return 3;

    memset(&OutputMsg, 0, sizeof(Data_Production_Message));
    InitHeader(&OutputMsg.Hdr, producerId, exchangeId, signature);

    printf("EGD Sender starting:\n");
    printf("  Destination : %s:%d\n", destinationAddress, EGD_DEFAULT_PORT);
    printf("  Interval    : %d ms\n", intervalMs);
    printf("  Count       : %s\n",    count == 0 ? "infinite" : "");
    if (count > 0)
        printf("                %lu messages\n", count);
    printf("  Data size   : %d bytes\n", dataSize);
    printf("  Producer ID : %u\n",    (unsigned int)producerId);
    printf("  Exchange ID : %u\n",    (unsigned int)exchangeId);
    printf("  Timing      : high-precision (CLOCK_MONOTONIC / TIMER_ABSTIME)\n");
#ifndef _WIN32
    printf("Press Ctrl+C to stop.\n");
#endif
    printf("\n");

    /**************************************************************************/
    /* Initialise the first wake time to now + one interval.
       We use CLOCK_MONOTONIC so the timer is not affected by
       system clock changes or NTP adjustments.
    */
    clock_gettime(CLOCK_MONOTONIC, &nextWakeTime);
    TimespecAddMs(&nextWakeTime, intervalMs);

    /**************************************************************************/
    /* Main send loop                                                          */
    /**************************************************************************/
#ifndef _WIN32
    while (g_running)
#else
    while (1)
#endif
    {
        /* --- Update data --- */
        UpdateMessageData(&OutputMsg, messageCount);

        /* --- Record time immediately before send --- */
        clock_gettime(CLOCK_MONOTONIC, &sendStart);

        /* --- Send --- */
        if (SendDataMessage(pSock, &destSockAddress, &OutputMsg, dataSize) != 0)
        {
            sendFailed = 1;
            break;
        }

        /* --- Record time immediately after send --- */
        clock_gettime(CLOCK_MONOTONIC, &sendEnd);

        totalSent++;
        messageCount++;

        /* --- Calculate send time --- */
        sendTimeMs = TimespecDiffMs(&sendEnd, &sendStart);

        /* --- Print periodic status every 10 messages --- */
        if (totalSent % 10 == 0)
        {
            printf("Sent %lu messages | ReqID=%-5u | SendTime=%.3f ms\n",
                   totalSent,
                   (unsigned int)OutputMsg.Hdr.ReqID,
                   sendTimeMs);
        }

        /* --- Check count --- */
        if (count > 0 && totalSent >= count)
        {
            printf("Reached requested count of %lu messages.\n", count);
            break;
        }

        /* --- Sleep until the next scheduled wake time ---
           TIMER_ABSTIME means we sleep until an absolute point in time.
           Any time spent sending is automatically compensated for because
           nextWakeTime is advanced by a fixed interval each cycle,
           regardless of how long the send took.
        */
        if (SleepUntil(&nextWakeTime) != 0)
            break;

        /* --- Record actual wake time and calculate jitter --- */
        clock_gettime(CLOCK_MONOTONIC, &actualWake);
        jitterMs = TimespecDiffMs(&actualWake, &nextWakeTime);

        /* --- Update timing statistics --- */
        UpdateTimingStats(&stats, sendTimeMs, jitterMs);

        /* --- Advance the next wake time by exactly one interval ---
           This is the key to drift-free operation. We always advance
           from the SCHEDULED time, not the actual wake time.
           So any overrun is naturally absorbed into the next sleep.
        */
        TimespecAddMs(&nextWakeTime, intervalMs);

        /* --- Detect if we have fallen behind by more than one interval.
           This can happen if the system is heavily loaded.
           If so, reset to avoid a burst of catch-up sends.
        */
        if (jitterMs > (double)intervalMs)
        {
            fprintf(stderr,
                    "Warning: timer overrun of %.3f ms — resetting schedule.\n",
                    jitterMs);
            clock_gettime(CLOCK_MONOTONIC, &nextWakeTime);
            TimespecAddMs(&nextWakeTime, intervalMs);
        }
    }

#ifndef _WIN32
    if (!g_running)
        printf("\nShutdown signal received.\n");
#endif

    printf("Total messages sent: %lu\n", totalSent);

    /* Print timing statistics */
    PrintTimingStats(&stats, intervalMs);

    /* Send final shutdown message */
    if (!sendFailed)
    {
        printf("Sending shutdown message...\n");
        OutputMsg.Hdr.Status = EGD_STATUS_INVALID;
        SendDataMessage(pSock, &destSockAddress, &OutputMsg, dataSize);
    }

    closesocket(*pSock);
    *pSock = INVALID_SOCKET;

    return sendFailed ? 1 : 0;
}

/**************************************************************************/
/* Parse and validate a positive integer from a string.
   Returns 1 on success, 0 on failure.
*/
static int ParsePositiveInt(const char* str, int* out)
{
    char* end;
    long val = strtol(str, &end, 10);
    if (end == str || *end != '\0' || val < 0 || val > 2147483647)
        return 0;
    *out = (int)val;
    return 1;
}

/**************************************************************************/
/* Print usage information */
static void PrintUsage(const char* programName)
{
    printf("Usage: %s [destination] [interval_ms] [count]\n\n", programName);
    printf("  destination  IP address or hostname (default: %s)\n",
           DEFAULT_DESTINATION);
    printf("  interval_ms  Send interval in milliseconds, %d-%d (default: %d)\n",
           MIN_INTERVAL_MS, MAX_INTERVAL_MS, DEFAULT_INTERVAL_MS);
    printf("  count        Number of messages to send, 0=infinite (default: %d)\n\n",
           DEFAULT_COUNT);
    printf("Examples:\n");
    printf("  %s\n", programName);
    printf("  %s 192.168.1.100\n", programName);
    printf("  %s 192.168.1.100 500\n", programName);
    printf("  %s 192.168.1.100 500 10\n", programName);
}

/**************************************************************************/
/* Entry point */
int main(int argc, char* argv[])
{
    char*  destinationAddress = DEFAULT_DESTINATION;
    int    intervalMs         = DEFAULT_INTERVAL_MS;
    int    count              = DEFAULT_COUNT;
    SOCKET sock               = INVALID_SOCKET;
    int    result;

    if (argc > 1)
    {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        {
            PrintUsage(argv[0]);
            return 0;
        }
        destinationAddress = argv[1];
    }

    if (argc > 2)
    {
        if (!ParsePositiveInt(argv[2], &intervalMs))
        {
            fprintf(stderr, "Error: invalid interval_ms '%s'\n\n", argv[2]);
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (argc > 3)
    {
        if (!ParsePositiveInt(argv[3], &count))
        {
            fprintf(stderr, "Error: invalid count '%s'\n\n", argv[3]);
            PrintUsage(argv[0]);
            return 1;
        }
    }

#ifdef _WIN32
    WSADATA WSAData;
    if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
    {
        fprintf(stderr, "WSAStartup failed, Error=%d\n", GetLastError());
        return 1;
    }
#else
    signal(SIGINT,  SignalHandler);
    signal(SIGTERM, SignalHandler);
#endif

    result = SenderExample(&sock, destinationAddress,
                           intervalMs, (unsigned long)count);

    if (sock != INVALID_SOCKET)
        closesocket(sock);

#ifdef _WIN32
    WSACleanup();
#endif

    return result;
}