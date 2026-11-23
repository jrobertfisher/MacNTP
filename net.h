/*
 * MacNTP: UDP over the MacTCP driver (.IPP, also provided by Open Transport),
 * a minimal DNS client and an NTP client.
 */
#ifndef MACNTP_NET_H
#define MACNTP_NET_H

#include <MacTypes.h>
#include <MacTCP.h>

enum { kMaxAddrs = 4 };

typedef struct {
    short       refNum;
    StreamPtr   stream;
    Ptr         rcvBuf;
} UDPConn;

OSErr   UDPOpen(UDPConn *c);
void    UDPClose(UDPConn *c);

/* Looks up an A record. Returns the number of addresses found (0 = failed);
   *notFound is set if the server says the name doesn't exist. */
int     DNSLookup(UDPConn *c, ip_addr dnsServer, ConstStringPtr name,
                  ip_addr addrs[kMaxAddrs], Boolean *notFound);

/* Asks an NTP server for the time. On success, *ntpSeconds is seconds since
   1900-01-01 UTC, adjusted for half the round trip. */
Boolean NTPQuery(UDPConn *c, ip_addr server, unsigned long *ntpSeconds);

/* Asks all the addresses at once; returns the one that answered first (0 = none). */
ip_addr NTPQueryAny(UDPConn *c, const ip_addr *servers, int count, unsigned long *ntpSeconds);

#endif
