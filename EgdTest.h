/*******************************************************************************/
/* EgdTest.h
   Header file for EGD (Ethernet Global Data) example code.
   Supports Windows, QNX4, QNX6, and Linux platforms.
*/

#ifndef EGDTEST_H
#define EGDTEST_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>

#ifdef _WIN32
   #include <windows.h>
   #include <winsock2.h>       /* Use winsock2.h instead of winsock.h */
   #include <sys/timeb.h>
   #pragma comment(lib, "ws2_32.lib")  /* Updated from wsock32.lib */
#else
   #define SOCKET              int
   #define INVALID_SOCKET      (-1)    /* Fixed: was 0, which is a valid fd */
   #define SOCKET_ERROR        (-1)
   #define closesocket(s)      close(s)
#endif

#ifdef QNX4
   #include <sys/socket.h>
   #include <net/if.h>
   #include <ioctl.h>
   #include <netinet/in.h>
   #include <netinet/tcp.h>
   #include <netdb.h>
   #include <signal.h>
   #include <errno.h>
   #include <sys/kernel.h>
   #include <sys/wait.h>
   #include <sys/select.h>
   #include <unistd.h>
#endif

#ifdef QNX6
   #include <sys/socket.h>
   #include <net/if.h>
   #include <ioctl.h>
   #include <netinet/in.h>
   #include <netinet/tcp.h>
   #include <signal.h>
   #include <netdb.h>
   #include <errno.h>
   #include <sys/wait.h>
   #include <sys/select.h>
   #include <unistd.h>
#endif

#ifdef __linux__
   #include <sys/socket.h>
   #include <unistd.h>
   #include <netinet/in.h>
   #include <netdb.h>
   #include <arpa/inet.h>
   #include <errno.h>
   #include <signal.h>
#endif

/* EGD Protocol Constants */
#define EGD_PDU_TYPE_DATA_PRODUCTION    13
#define EGD_STATUS_HEALTHY_NOT_SYNCED    2
#define EGD_STATUS_INVALID               1
#define EGD_DEFAULT_PORT             18246
#define EGD_MAX_DATA_SIZE             1400

/* Follows is the definition of the Data Production Header.
   Note that the EGD_Time structure corresponds to the
   "struct timespec" as defined in most POSIX operating systems.
*/
#define BYTE uint8_t
#define WORD uint16_t
#define DWORD uint32_t

#pragma pack(1)
typedef struct
{
    uint32_t tv_sec;
    int32_t  tv_nsec;
} EGD_Time;

typedef struct
{
    BYTE        PDUType;
    BYTE        Version;
    WORD        ReqID;
    DWORD       ProducerID;
    DWORD       ExchangeID;
    EGD_Time    Timestamp;
    WORD        Status;
    WORD        ReservedA;
    WORD        Signature;
    BYTE        ReservedB[6];
} Data_Production_Hdr;
#pragma pack()

/* Definition for a maximum length EGD data message. */
typedef struct
{
    Data_Production_Hdr Hdr;
    BYTE                Data[EGD_MAX_DATA_SIZE];
} Data_Production_Message;


/*********************************************/
#ifdef _WIN32
/* Custom perror() for Windows Winsock errors.
   Note: This replaces the standard perror() on Windows only. */
static void egd_perror(const char* str)
{
    const char *p;

    switch(WSAGetLastError())
    {
        case WSAEINTR:           p="WSAEINTR";           break;
        case WSAEBADF:           p="WSAEBADF";           break;
        case WSAEACCES:          p="WSAEACCES";          break;
        case WSAEFAULT:          p="WSAEFAULT";          break;
        case WSAEINVAL:          p="WSAEINVAL";          break;
        case WSAEMFILE:          p="WSAEMFILE";          break;
        case WSAEWOULDBLOCK:     p="WSAEWOULDBLOCK";     break;
        case WSAEINPROGRESS:     p="WSAEINPROGRESS";     break;
        case WSAEALREADY:        p="WSAEALREADY";        break;
        case WSAENOTSOCK:        p="WSAENOTSOCK";        break;
        case WSAEDESTADDRREQ:    p="WSAEDESTADDRREQ";    break;
        case WSAEMSGSIZE:        p="WSAEMSGSIZE";        break;
        case WSAEPROTOTYPE:      p="WSAEPROTOTYPE";      break;
        case WSAENOPROTOOPT:     p="WSAENOPROTOOPT";     break;
        case WSAEPROTONOSUPPORT: p="WSAEPROTONOSUPPORT"; break;
        case WSAESOCKTNOSUPPORT: p="WSAESOCKTNOSUPPORT"; break;
        case WSAEOPNOTSUPP:      p="WSAEOPNOTSUPP";      break;
        case WSAEPFNOSUPPORT:    p="WSAEPFNOSUPPORT";    break;
        case WSAEAFNOSUPPORT:    p="WSAEAFNOSUPPORT";    break;
        case WSAEADDRINUSE:      p="WSAEADDRINUSE";      break;
        case WSAEADDRNOTAVAIL:   p="WSAEADDRNOTAVAIL";   break;
        case WSAENETDOWN:        p="WSAENETDOWN";        break;
        case WSAENETUNREACH:     p="WSAENETUNREACH";     break;
        case WSAENETRESET:       p="WSAENETRESET";       break;
        case WSAECONNABORTED:    p="WSAECONNABORTED";    break;
        case WSAECONNRESET:      p="WSAECONNRESET";      break;
        case WSAENOBUFS:         p="WSAENOBUFS";         break;
        case WSAEISCONN:         p="WSAEISCONN";         break;
        case WSAENOTCONN:        p="WSAENOTCONN";        break;
        case WSAESHUTDOWN:       p="WSAESHUTDOWN";       break;
        case WSAETOOMANYREFS:    p="WSAETOOMANYREFS";    break;
        case WSAETIMEDOUT:       p="WSAETIMEDOUT";       break;
        case WSAECONNREFUSED:    p="WSAECONNREFUSED";    break;
        case WSAELOOP:           p="WSAELOOP";           break;
        case WSAENAMETOOLONG:    p="WSAENAMETOOLONG";    break;
        case WSAEHOSTDOWN:       p="WSAEHOSTDOWN";       break;
        case WSAEHOSTUNREACH:    p="WSAEHOSTUNREACH";    break;
        case WSAENOTEMPTY:       p="WSAENOTEMPTY";       break;
        case WSAEPROCLIM:        p="WSAEPROCLIM";        break;
        case WSAEUSERS:          p="WSAEUSERS";          break;
        case WSAEDQUOT:          p="WSAEDQUOT";          break;
        case WSAESTALE:          p="WSAESTALE";          break;
        case WSAEREMOTE:         p="WSAEREMOTE";         break;
        case WSASYSNOTREADY:     p="WSASYSNOTREADY";     break;
        case WSAVERNOTSUPPORTED: p="WSAVERNOTSUPPORTED"; break;
        case WSANOTINITIALISED:  p="WSANOTINITIALISED";  break;
        case WSAHOST_NOT_FOUND:  p="WSAHOST_NOT_FOUND";  break;
        case WSATRY_AGAIN:       p="WSATRY_AGAIN";       break;
        case WSANO_RECOVERY:     p="WSANO_RECOVERY";     break;
        case WSANO_DATA:         p="WSANO_DATA";         break;
#ifdef IP_DEST_PORT_UNREACHABLE
        case IP_DEST_PORT_UNREACHABLE: p="IP_DEST_PORT_UNREACHABLE"; break;
        case IP_NO_RESOURCES:          p="IP_NO_RESOURCES";          break;
        case IP_BAD_OPTION:            p="IP_BAD_OPTION";            break;
        case IP_HW_ERROR:              p="IP_HW_ERROR";              break;
        case IP_PACKET_TOO_BIG:        p="IP_PACKET_TOO_BIG";        break;
        case IP_REQ_TIMED_OUT:         p="IP_REQ_TIMED_OUT";         break;
        case IP_BAD_REQ:               p="IP_BAD_REQ";               break;
        case IP_BAD_ROUTE:             p="IP_BAD_ROUTE";             break; /* Fixed: was "IP_TTL_EXPIRED_TRANSIT" */
        case IP_TTL_EXPIRED_TRANSIT:   p="IP_TTL_EXPIRED_TRANSIT";   break;
        case IP_TTL_EXPIRED_REASSEM:   p="IP_TTL_EXPIRED_REASSEM";   break;
        case IP_PARAM_PROBLEM:         p="IP_PARAM_PROBLEM";         break;
        case IP_SOURCE_QUENCH:         p="IP_SOURCE_QUENCH";         break;
        case IP_OPTION_TOO_BIG:        p="IP_OPTION_TOO_BIG";        break;
        case IP_BAD_DESTINATION:       p="IP_BAD_DESTINATION";       break;
#endif
        default:                       p="Unknown Error";             break;
    }
    fprintf(stderr, "%s: %s\n", str, p);
}
#else
    /* On Linux/QNX, use the standard perror() */
    #define egd_perror(s) perror(s)
#endif
/*********************************************/

#endif /* EGDTEST_H */