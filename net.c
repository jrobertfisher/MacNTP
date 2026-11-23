/*
 * MacNTP: UDP over the MacTCP driver, a minimal DNS client and an NTP client.
 * (Packet code tested against real DNS and NTP servers on Linux; see notes.)
 */
#include "net.h"
#include <Devices.h>
#include <Events.h>
#include <Memory.h>
#include <stddef.h>
#include <string.h>

#define CHECK(cond) _Static_assert(cond, #cond)
/* Pin the MacTCP parameter-block layout this code relies on. */
CHECK(offsetof(UDPCreatePB, rcvBuffLen) == 4);
CHECK(offsetof(UDPCreatePB, localPort) == 12);
CHECK(offsetof(UDPSendPB, remoteHost) == 2);
CHECK(offsetof(UDPSendPB, remotePort) == 6);
CHECK(offsetof(UDPSendPB, wdsPtr) == 8);
CHECK(offsetof(UDPSendPB, checkSum) == 12);
CHECK(offsetof(UDPReceivePB, remoteHost) == 2);
CHECK(offsetof(UDPReceivePB, remotePort) == 6);
CHECK(offsetof(UDPReceivePB, rcvBuff) == 8);
CHECK(offsetof(UDPReceivePB, rcvBuffLen) == 12);
CHECK(offsetof(UDPiopb, csCode) == 26);
CHECK(offsetof(UDPiopb, udpStream) == 28);
CHECK(offsetof(UDPiopb, csParam) == 32);
CHECK(sizeof(wdsEntry) == 6);

enum {
    kRcvBufSize   = 4096,
    kReadTimeout  = 3,          /* seconds per attempt (DNS) */
    kNTPReadTimeout = 2,        /* seconds per attempt (NTP, all servers at once) */
    kDNSAttempts  = 3,
    kNTPAttempts  = 2,
    kDNSPort      = 53,
    kNTPPort      = 123,
    kNTPPacketLen = 48
};

typedef int (*ReplyHandler)(const unsigned char *data, unsigned short len, void *user);

static OSErr UDPCall(UDPiopb *pb, short refNum, short csCode)
{
    pb->ioCompletion = NULL;
    pb->ioCRefNum = refNum;
    pb->csCode = csCode;
    return PBControlSync((ParmBlkPtr)pb);
}

OSErr UDPOpen(UDPConn *c)
{
    UDPiopb pb;
    OSErr err = OpenDriver("\p.IPP", &c->refNum);
    if (err) return err;
    c->rcvBuf = NewPtrClear(kRcvBufSize);
    if (!c->rcvBuf) return memFullErr;
    memset(&pb, 0, sizeof pb);
    pb.csParam.create.rcvBuff = c->rcvBuf;
    pb.csParam.create.rcvBuffLen = kRcvBufSize;
    pb.csParam.create.localPort = 0;                    /* any free port */
    err = UDPCall(&pb, c->refNum, UDPCreate);
    if (err) { DisposePtr(c->rcvBuf); c->rcvBuf = NULL; return err; }
    c->stream = pb.udpStream;
    return noErr;
}

void UDPClose(UDPConn *c)
{
    UDPiopb pb;
    if (!c->rcvBuf) return;
    memset(&pb, 0, sizeof pb);
    pb.udpStream = c->stream;
    UDPCall(&pb, c->refNum, UDPRelease);
    DisposePtr(c->rcvBuf);
    c->rcvBuf = NULL;
}

/* Sends `req` and waits for a reply the handler accepts.
   Returns 1 accepted, 0 no reply, -1 refused by the server. */
static int UDPExchange(UDPConn *c, ip_addr host, udp_port port,
                       const unsigned char *req, short reqLen, int attempts,
                       ReplyHandler handler, void *user)
{
    UDPiopb pb;
    wdsEntry wds[2];
    int attempt;

    wds[0].length = reqLen;
    wds[0].ptr = (Ptr)req;
    wds[1].length = 0;
    wds[1].ptr = NULL;

    for (attempt = 0; attempt < attempts; attempt++) {
        memset(&pb, 0, sizeof pb);
        pb.udpStream = c->stream;
        pb.csParam.send.remoteHost = host;
        pb.csParam.send.remotePort = port;
        pb.csParam.send.wdsPtr = (Ptr)wds;
        pb.csParam.send.checkSum = true;
        if (UDPCall(&pb, c->refNum, UDPWrite)) continue;

        for (;;) {
            int result = 0;
            Ptr data;
            memset(&pb, 0, sizeof pb);
            pb.udpStream = c->stream;
            pb.csParam.receive.timeOut = kReadTimeout;
            if (UDPCall(&pb, c->refNum, UDPRead)) break;    /* timed out: send again */

            data = pb.csParam.receive.rcvBuff;
            if (pb.csParam.receive.remoteHost == host && pb.csParam.receive.remotePort == port)
                result = handler((const unsigned char *)data, pb.csParam.receive.rcvBuffLen, user);

            memset(&pb, 0, sizeof pb);                      /* give the buffer back */
            pb.udpStream = c->stream;
            pb.csParam.receive.rcvBuff = data;
            UDPCall(&pb, c->refNum, UDPBfrReturn);

            if (result != 0) return result;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* DNS                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned short id;
    ip_addr *addrs;
    int count;
    Boolean notFound;
} DNSState;

static int DNSSkipName(const unsigned char *p, int len, int off)
{
    while (off < len) {
        unsigned char l = p[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : -1;
        off += 1 + l;
    }
    return -1;
}

static int DNSHandler(const unsigned char *p, unsigned short len, void *user)
{
    DNSState *st = (DNSState *)user;
    int off, i, qd, an;
    if (len < 12 || ((p[0] << 8) | p[1]) != st->id || !(p[2] & 0x80)) return 0;
    if ((p[3] & 0x0F) == 3) { st->notFound = true; return -1; }     /* NXDOMAIN */
    if ((p[3] & 0x0F) != 0) return -1;
    qd = (p[4] << 8) | p[5];
    an = (p[6] << 8) | p[7];
    off = 12;
    for (i = 0; i < qd; i++) {
        off = DNSSkipName(p, len, off);
        if (off < 0 || off + 4 > len) return -1;
        off += 4;
    }
    for (i = 0; i < an && st->count < kMaxAddrs; i++) {
        unsigned short type, rdlen;
        off = DNSSkipName(p, len, off);
        if (off < 0 || off + 10 > len) break;
        type = (p[off] << 8) | p[off + 1];
        rdlen = (p[off + 8] << 8) | p[off + 9];
        off += 10;
        if (off + rdlen > len) break;
        if (type == 1 && rdlen == 4)                        /* A record (CNAMEs are skipped) */
            st->addrs[st->count++] = ((unsigned long)p[off] << 24) | ((unsigned long)p[off + 1] << 16)
                                   | ((unsigned long)p[off + 2] << 8) | p[off + 3];
        off += rdlen;
    }
    return st->count > 0 ? 1 : -1;
}

int DNSLookup(UDPConn *c, ip_addr dnsServer, ConstStringPtr name,
              ip_addr addrs[kMaxAddrs], Boolean *notFound)
{
    unsigned char q[300];
    int qlen = 12, i, labelStart;
    DNSState st;

    st.id = (unsigned short)TickCount();
    st.addrs = addrs;
    st.count = 0;
    st.notFound = false;

    memset(q, 0, 12);
    q[0] = st.id >> 8; q[1] = st.id & 0xFF;
    q[2] = 0x01;                                            /* recursion desired */
    q[5] = 1;                                               /* one question */

    labelStart = qlen++;
    for (i = 1; i <= name[0]; i++) {
        if (name[i] == '.') {
            q[labelStart] = qlen - labelStart - 1;
            labelStart = qlen++;
        } else {
            if (qlen >= (int)sizeof q - 6) return 0;
            q[qlen++] = name[i];
        }
    }
    q[labelStart] = qlen - labelStart - 1;
    if (q[labelStart] == 0) qlen--;                         /* trailing dot */
    q[qlen++] = 0;                                          /* root label */
    q[qlen++] = 0; q[qlen++] = 1;                           /* type A */
    q[qlen++] = 0; q[qlen++] = 1;                           /* class IN */

    UDPExchange(c, dnsServer, kDNSPort, q, qlen, kDNSAttempts, DNSHandler, &st);
    if (notFound) *notFound = st.notFound;
    return st.count;
}

/* ------------------------------------------------------------------ */
/* NTP                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned long sentTicks;
    unsigned long seconds;
} NTPState;

static int NTPHandler(const unsigned char *r, unsigned short len, void *user)
{
    NTPState *st = (NTPState *)user;
    unsigned long secs, halfTrip;
    if (len < kNTPPacketLen || (r[0] & 7) != 4) return 0;  /* not a server reply */
    if (r[1] == 0 || r[1] > 15) return -1;                  /* kiss-o'-death or unsynchronised */
    secs = ((unsigned long)r[40] << 24) | ((unsigned long)r[41] << 16)
         | ((unsigned long)r[42] << 8) | (unsigned long)r[43];
    if (r[44] & 0x80) secs++;                               /* round the fraction */
    halfTrip = (TickCount() - st->sentTicks) / 2;
    st->seconds = secs + (halfTrip + 30) / 60;
    return 1;
}

Boolean NTPQuery(UDPConn *c, ip_addr server, unsigned long *ntpSeconds)
{
    unsigned char req[kNTPPacketLen];
    NTPState st;
    memset(req, 0, sizeof req);
    req[0] = 0x1B;                                          /* LI 0, version 3, mode 3 (client) */
    st.sentTicks = TickCount();
    st.seconds = 0;
    if (UDPExchange(c, server, kNTPPort, req, kNTPPacketLen, kNTPAttempts, NTPHandler, &st) != 1)
        return false;
    *ntpSeconds = st.seconds;
    return true;
}

/* Sends the request to every address at once and takes the first good answer,
   so one slow or dead server in a pool doesn't hold things up. */
ip_addr NTPQueryAny(UDPConn *c, const ip_addr *servers, int count, unsigned long *ntpSeconds)
{
    unsigned char req[kNTPPacketLen];
    UDPiopb pb;
    wdsEntry wds[2];
    NTPState st;
    int attempt, i;

    memset(req, 0, sizeof req);
    req[0] = 0x1B;                                          /* LI 0, version 3, mode 3 (client) */
    wds[0].length = kNTPPacketLen;
    wds[0].ptr = (Ptr)req;
    wds[1].length = 0;
    wds[1].ptr = NULL;

    for (attempt = 0; attempt < kNTPAttempts; attempt++) {
        st.sentTicks = TickCount();
        st.seconds = 0;
        for (i = 0; i < count; i++) {
            memset(&pb, 0, sizeof pb);
            pb.udpStream = c->stream;
            pb.csParam.send.remoteHost = servers[i];
            pb.csParam.send.remotePort = kNTPPort;
            pb.csParam.send.wdsPtr = (Ptr)wds;
            pb.csParam.send.checkSum = true;
            UDPCall(&pb, c->refNum, UDPWrite);
        }

        for (;;) {
            ip_addr from;
            int result = 0;
            Ptr data;
            memset(&pb, 0, sizeof pb);
            pb.udpStream = c->stream;
            pb.csParam.receive.timeOut = kNTPReadTimeout;
            if (UDPCall(&pb, c->refNum, UDPRead)) break;    /* nobody answered in time: send again */

            data = pb.csParam.receive.rcvBuff;
            from = pb.csParam.receive.remoteHost;
            if (pb.csParam.receive.remotePort == kNTPPort)
                for (i = 0; i < count; i++)
                    if (servers[i] == from) {
                        result = NTPHandler((const unsigned char *)data, pb.csParam.receive.rcvBuffLen, &st);
                        break;
                    }

            memset(&pb, 0, sizeof pb);                      /* give the buffer back */
            pb.udpStream = c->stream;
            pb.csParam.receive.rcvBuff = data;
            UDPCall(&pb, c->refNum, UDPBfrReturn);

            if (result == 1) {
                *ntpSeconds = st.seconds;
                return from;
            }
            /* a refusal from one server, or an unrelated packet: keep waiting for the others */
        }
    }
    return 0;
}
