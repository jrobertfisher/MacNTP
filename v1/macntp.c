/*
 * MacNTP: sets a classic Mac's clock from an NTP server.
 *
 * Built with Retro68 for 68k Macs (System 7, MacTCP or Open Transport).
 * Uses the MacTCP UDP driver interface (.IPP), which Open Transport also
 * provides for compatibility.
 *
 * Put it in System Folder:Startup Items to sync at every startup.
 *   - Success is silent. Hold Option while it starts to see the result.
 *   - Errors are always shown.
 *
 * Settings (editable with ResEdit):
 *   'STR ' 128  NTP server: a host name or IPv4 address (default pool.ntp.org).
 *               Names are looked up with a small built-in DNS client, and every
 *               address returned is tried in turn.
 *   'STR ' 129  Standard-time UTC offset in seconds (default -21600, US Central).
 *               Leave empty to use the Map control panel's time zone.
 *   'STR ' 130  Daylight saving rule: "US" (default), "EU" or "none".
 *   'STR ' 131  DNS server IPv4 address (default 1.1.1.1).
 */

#include <Devices.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <ToolUtils.h>
#include <Windows.h>
#include <MacTCP.h>
#include <stddef.h>
#include <string.h>

/* MacTCP UDP parameter blocks come from Apple's MacTCP.h (Universal Interfaces).
   These checks pin the layout the code relies on. */
#define CHECK(cond) _Static_assert(cond, #cond)
CHECK(offsetof(UDPCreatePB, localPort) == 12);
CHECK(offsetof(UDPCreatePB, userDataPtr) == 14);
CHECK(offsetof(UDPCreatePB, endingPort) == 18);
CHECK(offsetof(UDPSendPB, remoteHost) == 2);
CHECK(offsetof(UDPSendPB, remotePort) == 6);
CHECK(offsetof(UDPSendPB, wdsPtr) == 8);
CHECK(offsetof(UDPSendPB, checkSum) == 12);
CHECK(offsetof(UDPSendPB, sendLength) == 14);
CHECK(offsetof(UDPSendPB, userDataPtr) == 16);
CHECK(offsetof(UDPSendPB, localPort) == 20);
CHECK(offsetof(UDPReceivePB, remoteHost) == 2);
CHECK(offsetof(UDPReceivePB, remotePort) == 6);
CHECK(offsetof(UDPReceivePB, rcvBuff) == 8);
CHECK(offsetof(UDPReceivePB, rcvBuffLen) == 12);
CHECK(offsetof(UDPReceivePB, userDataPtr) == 16);
CHECK(offsetof(UDPReceivePB, destHost) == 20);
CHECK(offsetof(UDPReceivePB, destPort) == 24);
CHECK(offsetof(UDPiopb, ioResult) == 16);
CHECK(offsetof(UDPiopb, ioCRefNum) == 24);
CHECK(offsetof(UDPiopb, csCode) == 26);
CHECK(offsetof(UDPiopb, udpStream) == 28);
CHECK(offsetof(UDPiopb, csParam) == 32);
CHECK(sizeof(wdsEntry) == 6);
CHECK(offsetof(UDPCreatePB, rcvBuffLen) == 4);

/* ------------------------------------------------------------------ */
/* Settings and constants                                             */
/* ------------------------------------------------------------------ */

enum {
    kServerStrID   = 128,
    kOffsetStrID   = 129,
    kDSTRuleStrID  = 130,
    kDNSStrID      = 131,
    kAlertID       = 128,
    kNTPPort       = 123,
    kAttempts      = 4,
    kReadTimeout   = 3,             /* seconds per attempt */
    kRcvBufSize    = 4096,
    kNTPPacketLen  = 48
};

/* Seconds from 1900-01-01 (NTP epoch) to 1904-01-01 (Mac epoch). */
#define NTP_TO_MAC_EPOCH 126230400UL
/* Used only if no offset is set in STR 129 and the Map control panel is unset. */
#define DEFAULT_DNS 0x01010101UL      /* 1.1.1.1 if STR 131 is empty or invalid */
#define FALLBACK_UTC_OFFSET (-21600L)   /* US Central Standard Time */

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void PStrCopyC(Str255 dst, const char *src)
{
    size_t n = strlen(src);
    if (n > 255) n = 255;
    dst[0] = (unsigned char)n;
    memcpy(dst + 1, src, n);
}

static void PStrAppend(Str255 dst, const unsigned char *src)
{
    int n = src[0];
    if (dst[0] + n > 255) n = 255 - dst[0];
    memcpy(dst + 1 + dst[0], src + 1, n);
    dst[0] += n;
}

static void PStrAppendC(Str255 dst, const char *src)
{
    Str255 tmp;
    PStrCopyC(tmp, src);
    PStrAppend(dst, tmp);
}

static void PStrAppendNum(Str255 dst, long n)
{
    Str255 tmp;
    NumToString(n, tmp);
    PStrAppend(dst, tmp);
}

static Boolean OptionKeyDown(void)
{
    KeyMap keys;
    GetKeys(keys);
    /* Option is key code 0x3A: byte 7, bit 2 in the big-endian key map. */
    return (((unsigned char *)keys)[0x3A >> 3] >> (0x3A & 7)) & 1;
}

/* Parse "a.b.c.d" from a Pascal string. Returns 0 on failure. */
static ip_addr ParseIPv4(const unsigned char *s)
{
    unsigned long addr = 0, part = 0;
    int dots = 0, digits = 0, i;
    for (i = 1; i <= s[0]; i++) {
        unsigned char c = s[i];
        if (c >= '0' && c <= '9') {
            part = part * 10 + (c - '0');
            if (part > 255 || ++digits > 3) return 0;
        } else if (c == '.') {
            if (digits == 0 || dots == 3) return 0;
            addr = (addr << 8) | part;
            part = 0; digits = 0; dots++;
        } else if (c != ' ') {
            return 0;
        }
    }
    if (dots != 3 || digits == 0) return 0;
    return (addr << 8) | part;
}

/* Parse an optional signed decimal from a Pascal string. */
static Boolean ParseLong(const unsigned char *s, long *out)
{
    long v = 0;
    int i = 1, neg = 0, digits = 0;
    while (i <= s[0] && s[i] == ' ') i++;
    if (i <= s[0] && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); i++; }
    for (; i <= s[0]; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
        digits++;
    }
    if (!digits) return false;
    *out = neg ? -v : v;
    return true;
}

/* ReadLocation() isn't in Retro68's interfaces: it reads 12 bytes of
   extended parameter RAM at 0xE4 with the _ReadXPRam trap (A051). */
typedef struct {
    long latitude;
    long longitude;
    long gmtDelta;      /* high byte = dlsDelta, low 3 bytes = offset in seconds */
} MacLocation;

static void ReadMacLocation(MacLocation *loc)
{
    register void *a0 asm("a0") = loc;
    register long d0 asm("d0") = 0x000C00E4;    /* 12 bytes at 0xE4 */
    asm volatile(".short 0xA051" : "+r"(d0), "+r"(a0) : : "d1", "d2", "a1", "memory");
}

/* UTC offset in seconds, and a short note about where it came from. */
static long LocalOffset(const char **source)
{
    StringHandle h = GetString(kOffsetStrID);
    long offset;
    MacLocation loc;

    if (h && (*h)[0] && ParseLong(*h, &offset)) {
        *source = "STR 129 setting";
        return offset;
    }
    ReadMacLocation(&loc);
    offset = loc.gmtDelta & 0x00FFFFFF;
    if (offset & 0x00800000) offset |= 0xFF000000;      /* sign-extend 24 bits */
    if (offset == 0 && loc.latitude == 0 && loc.longitude == 0) {
        *source = "default (US Central), Map control panel not set";
        return FALLBACK_UTC_OFFSET;
    }
    *source = "Map control panel";
    return offset;
}

/* ------------------------------------------------------------------ */
/* Daylight saving time                                               */
/* All times here are seconds since 1904-01-01 00:00 UTC.             */
/* ------------------------------------------------------------------ */

enum { kDSTNone, kDSTUS, kDSTEU };

static int DSTRule(void)
{
    StringHandle h = GetString(kDSTRuleStrID);
    const unsigned char *s;
    if (!h || (*h)[0] == 0) return kDSTUS;
    s = *h;
    if (s[0] >= 2 && (s[1] | 0x20) == 'e' && (s[2] | 0x20) == 'u') return kDSTEU;
    if (s[0] >= 2 && (s[1] | 0x20) == 'u' && (s[2] | 0x20) == 's') return kDSTUS;
    return kDSTNone;
}

/* Days from 1904-01-01 to year-month-day (proleptic Gregorian). */
static long LeapYearsBefore(int y) { y--; return y / 4 - y / 100 + y / 400; }

static long DaysFrom1904(int y, int m, int d)
{
    static const short before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    long days = (long)(y - 1904) * 365 + LeapYearsBefore(y) - LeapYearsBefore(1904);
    days += before[m - 1] + (d - 1);
    if (m > 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days++;
    return days;
}

/* Day of the week for a day count from 1904-01-01 (a Friday). 0 = Sunday. */
static int Weekday(long days) { return (int)((days + 5) % 7); }

/* Day of month of the nth Sunday (n >= 1), or the last Sunday if n == 0. */
static int SundayOfMonth(int y, int m, int n)
{
    static const char len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int days_in = len[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
    if (n == 0) {
        int last = days_in;
        return last - Weekday(DaysFrom1904(y, m, last));
    } else {
        int first = 1 + (7 - Weekday(DaysFrom1904(y, m, 1))) % 7;
        return first + 7 * (n - 1);
    }
}

static int YearOf(unsigned long utc)
{
    long days = (long)(utc / 86400UL);
    int y = 1904;
    while (DaysFrom1904(y + 1, 1, 1) <= days) y++;
    return y;
}

/* Is daylight saving time in effect at this UTC moment? */
static Boolean InDST(unsigned long utc, long stdOffset, int rule)
{
    int y = YearOf(utc);
    unsigned long start, end;
    if (rule == kDSTUS) {
        /* 2:00 local standard time on the 2nd Sunday in March,
           until 2:00 local daylight time (1:00 standard) on the 1st Sunday in November. */
        start = DaysFrom1904(y, 3, SundayOfMonth(y, 3, 2)) * 86400UL + 2 * 3600UL - stdOffset;
        end   = DaysFrom1904(y, 11, SundayOfMonth(y, 11, 1)) * 86400UL + 1 * 3600UL - stdOffset;
    } else if (rule == kDSTEU) {
        /* 01:00 UTC on the last Sunday in March until 01:00 UTC on the last Sunday in October. */
        start = DaysFrom1904(y, 3, SundayOfMonth(y, 3, 0)) * 86400UL + 3600UL;
        end   = DaysFrom1904(y, 10, SundayOfMonth(y, 10, 0)) * 86400UL + 3600UL;
    } else {
        return false;
    }
    return utc >= start && utc < end;
}

static void ShowMessage(const unsigned char *msg)
{
    ParamText(msg, "\p", "\p", "\p");
    InitCursor();
    NoteAlert(kAlertID, NULL);
}

static void ShowError(const char *what, OSErr err)
{
    Str255 msg;
    PStrCopyC(msg, "MacNTP could not set the clock: ");
    PStrAppendC(msg, what);
    if (err) {
        PStrAppendC(msg, " (error ");
        PStrAppendNum(msg, err);
        PStrAppendC(msg, ")");
    }
    ShowMessage(msg);
}

/* ------------------------------------------------------------------ */
/* UDP over the MacTCP driver (.IPP)                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    short       refNum;
    StreamPtr   stream;
    Ptr         rcvBuf;
} UDPConn;

/* Reply handler: return 1 = accepted (done), 0 = ignore and keep waiting,
   -1 = definite failure from this host (stop). */
typedef int (*ReplyHandler)(const unsigned char *data, unsigned short len, void *user);

static OSErr UDPCall(UDPiopb *pb, short refNum, short csCode)
{
    pb->ioCompletion = NULL;
    pb->ioCRefNum = refNum;
    pb->csCode = csCode;
    return PBControlSync((ParmBlkPtr)pb);
}

static OSErr UDPOpen(UDPConn *c, const char **failure)
{
    UDPiopb pb;
    OSErr err = OpenDriver("\p.IPP", &c->refNum);
    if (err) { *failure = "network (MacTCP/Open Transport) not available"; return err; }
    c->rcvBuf = NewPtrClear(kRcvBufSize);
    if (!c->rcvBuf) { *failure = "out of memory"; return memFullErr; }
    memset(&pb, 0, sizeof pb);
    pb.csParam.create.rcvBuff = c->rcvBuf;
    pb.csParam.create.rcvBuffLen = kRcvBufSize;
    pb.csParam.create.localPort = 0;                /* any free port */
    err = UDPCall(&pb, c->refNum, UDPCreate);
    if (err) { DisposePtr(c->rcvBuf); *failure = "could not open a UDP port"; return err; }
    c->stream = pb.udpStream;
    return noErr;
}

static void UDPClose(UDPConn *c)
{
    UDPiopb pb;
    memset(&pb, 0, sizeof pb);
    pb.udpStream = c->stream;
    UDPCall(&pb, c->refNum, UDPRelease);
    DisposePtr(c->rcvBuf);
}

/* Sends `req` to host:port and waits for a reply the handler accepts.
   Resends up to `attempts` times. Returns 1 accepted, 0 no reply, -1 refused. */
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
            if (UDPCall(&pb, c->refNum, UDPRead)) break;    /* timeout: resend */

            data = pb.csParam.receive.rcvBuff;
            if (pb.csParam.receive.remoteHost == host && pb.csParam.receive.remotePort == port)
                result = handler((const unsigned char *)data, pb.csParam.receive.rcvBuffLen, user);

            memset(&pb, 0, sizeof pb);                      /* give the buffer back */
            pb.udpStream = c->stream;
            pb.csParam.receive.rcvBuff = data;
            UDPCall(&pb, c->refNum, UDPBfrReturn);

            if (result != 0) return result;
            /* unrelated packet: keep waiting */
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* DNS: minimal A-record lookup over UDP                              */
/* ------------------------------------------------------------------ */

enum { kDNSPort = 53, kMaxAddrs = 4 };

typedef struct {
    unsigned short id;
    ip_addr addrs[kMaxAddrs];
    int count;
    Boolean notFound;
} DNSResult;

/* Skips a (possibly compressed) DNS name. Returns the new offset or -1. */
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
    DNSResult *res = (DNSResult *)user;
    int off, i, qd, an;
    if (len < 12 || ((p[0] << 8) | p[1]) != res->id || !(p[2] & 0x80)) return 0;
    if ((p[3] & 0x0F) == 3) { res->notFound = true; return -1; }   /* NXDOMAIN */
    if ((p[3] & 0x0F) != 0) return -1;
    qd = (p[4] << 8) | p[5];
    an = (p[6] << 8) | p[7];
    off = 12;
    for (i = 0; i < qd; i++) {
        off = DNSSkipName(p, len, off);
        if (off < 0 || off + 4 > len) return -1;
        off += 4;
    }
    for (i = 0; i < an && res->count < kMaxAddrs; i++) {
        unsigned short type, rdlen;
        off = DNSSkipName(p, len, off);
        if (off < 0 || off + 10 > len) break;
        type = (p[off] << 8) | p[off + 1];
        rdlen = (p[off + 8] << 8) | p[off + 9];
        off += 10;
        if (off + rdlen > len) break;
        if (type == 1 && rdlen == 4)                 /* A record; CNAMEs are skipped */
            res->addrs[res->count++] = ((unsigned long)p[off] << 24) | ((unsigned long)p[off + 1] << 16)
                                     | ((unsigned long)p[off + 2] << 8) | p[off + 3];
        off += rdlen;
    }
    return res->count > 0 ? 1 : -1;
}

/* Looks up `name` (Pascal string). Fills res; returns true if any address was found. */
static Boolean DNSLookup(UDPConn *c, ip_addr dnsServer, const unsigned char *name, DNSResult *res)
{
    unsigned char q[300];
    int qlen = 12, i, labelStart;

    memset(res, 0, sizeof *res);
    res->id = (unsigned short)TickCount();
    memset(q, 0, 12);
    q[0] = res->id >> 8; q[1] = res->id & 0xFF;
    q[2] = 0x01;                                     /* recursion desired */
    q[5] = 1;                                        /* one question */

    labelStart = qlen++;
    for (i = 1; i <= name[0]; i++) {
        if (name[i] == '.') {
            q[labelStart] = qlen - labelStart - 1;
            labelStart = qlen++;
        } else if (name[i] != ' ') {
            if (qlen >= (int)sizeof q - 6) return false;
            q[qlen++] = name[i];
        }
    }
    q[labelStart] = qlen - labelStart - 1;
    if (q[labelStart] == 0) qlen--;                  /* trailing dot */
    q[qlen++] = 0;                                   /* root */
    q[qlen++] = 0; q[qlen++] = 1;                    /* type A */
    q[qlen++] = 0; q[qlen++] = 1;                    /* class IN */

    return UDPExchange(c, dnsServer, kDNSPort, q, qlen, kAttempts, DNSHandler, res) == 1;
}

/* ------------------------------------------------------------------ */
/* NTP                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned long sentTicks;
    unsigned long seconds;   /* NTP seconds since 1900, adjusted for the round trip */
} NTPResult;

static int NTPHandler(const unsigned char *r, unsigned short len, void *user)
{
    NTPResult *res = (NTPResult *)user;
    unsigned long secs, halfTrip;
    if (len < kNTPPacketLen || (r[0] & 7) != 4) return 0;      /* not a server reply */
    if (r[1] == 0 || r[1] > 15) return -1;                     /* kiss-o'-death / unsynced */
    secs = ((unsigned long)r[40] << 24) | ((unsigned long)r[41] << 16)
         | ((unsigned long)r[42] << 8) | (unsigned long)r[43];
    if (r[44] & 0x80) secs++;                                  /* round the fraction */
    halfTrip = (TickCount() - res->sentTicks) / 2;
    res->seconds = secs + (halfTrip + 30) / 60;
    return 1;
}

static Boolean QueryNTP(UDPConn *c, ip_addr server, unsigned long *ntpSeconds)
{
    unsigned char req[kNTPPacketLen];
    NTPResult res;
    memset(req, 0, sizeof req);
    req[0] = 0x1B;                                   /* LI 0, version 3, mode 3 (client) */
    res.sentTicks = TickCount();
    res.seconds = 0;
    if (UDPExchange(c, server, kNTPPort, req, kNTPPacketLen, 2, NTPHandler, &res) != 1) return false;
    *ntpSeconds = res.seconds;
    return true;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    Boolean verbose;
    StringHandle serverStr, dnsStr;
    ip_addr servers[kMaxAddrs], dnsServer;
    int nServers = 0, i;
    unsigned long ntpSeconds = 0, oldMac, newMac;
    long offset;
    const char *offsetSource;
    const char *failure = "";
    Boolean dst = false, synced = false;
    UDPConn conn;
    Str255 serverName;
    OSErr err;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    FlushEvents(everyEvent, 0);

    verbose = OptionKeyDown();

    serverStr = GetString(kServerStrID);
    if (!serverStr || (*serverStr)[0] == 0) {
        ShowError("no NTP server is set in STR 128", 0);
        return 0;
    }
    BlockMoveData(*serverStr, serverName, (*serverStr)[0] + 1);

    err = UDPOpen(&conn, &failure);
    if (err) { ShowError(failure, err); return 0; }

    servers[0] = ParseIPv4(serverName);
    if (servers[0]) {
        nServers = 1;
    } else {
        DNSResult dns;
        dnsStr = GetString(kDNSStrID);
        dnsServer = dnsStr ? ParseIPv4(*dnsStr) : 0;
        if (!dnsServer) dnsServer = DEFAULT_DNS;
        if (DNSLookup(&conn, dnsServer, serverName, &dns)) {
            nServers = dns.count;
            for (i = 0; i < nServers; i++) servers[i] = dns.addrs[i];
        } else {
            UDPClose(&conn);
            ShowError(dns.notFound ? "the NTP server name does not exist (STR 128)"
                                   : "could not look up the NTP server name (no reply from the DNS server)", 0);
            return 0;
        }
    }

    for (i = 0; i < nServers && !synced; i++)
        synced = QueryNTP(&conn, servers[i], &ntpSeconds);
    UDPClose(&conn);

    if (!synced) {
        ShowError(nServers > 1 ? "none of the time servers replied" : "no reply from the time server", 0);
        return 0;
    }

    /* The NTP era rolls over in 2036 and the Mac clock in 2040; reject nonsense. */
    if (ntpSeconds <= NTP_TO_MAC_EPOCH) {
        ShowError("the server sent an invalid time", 0);
        return 0;
    }

    offset = LocalOffset(&offsetSource);
    {
        unsigned long utc = ntpSeconds - NTP_TO_MAC_EPOCH;
        dst = InDST(utc, offset, DSTRule());
        newMac = utc + offset + (dst ? 3600L : 0);
    }

    GetDateTime(&oldMac);
    err = SetDateTime(newMac);
    if (err) {
        ShowError("the system refused the new time", err);
        return 0;
    }

    if (verbose) {
        Str255 msg, s;
        long change = (long)(newMac - oldMac);
        PStrCopyC(msg, "Clock set to ");
        IUDateString(newMac, longDate, s);
        PStrAppend(msg, s);
        PStrAppendC(msg, ", ");
        IUTimeString(newMac, false, s);
        PStrAppend(msg, s);
        PStrAppendC(msg, ". Changed by ");
        PStrAppendNum(msg, change);
        PStrAppendC(msg, " seconds. ");
        PStrAppendC(msg, dst ? "Daylight" : "Standard");
        PStrAppendC(msg, " time, UTC");
        if (offset + (dst ? 3600L : 0) >= 0) PStrAppendC(msg, "+");
        PStrAppendNum(msg, (offset + (dst ? 3600L : 0)) / 3600);
        PStrAppendC(msg, " (offset from ");
        PStrAppendC(msg, offsetSource);
        PStrAppendC(msg, ").");
        ShowMessage(msg);
    }
    return 0;
}
