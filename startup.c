/*
 * MacNTP Startup: sets the clock from an NTP server, using the settings
 * from the MacNTP control panel.
 *
 * Runs in three situations:
 *   - At startup (in the Startup Items folder): syncs if "Sync at startup" is on.
 *     Silent unless something goes wrong.
 *   - When the control panel's Sync Now button launches it: syncs silently and
 *     writes the result to MacNTP Prefs for the control panel to show.
 *   - When opened by hand (or with Option held at startup): syncs and shows the result.
 */
#include "common.h"
#include "net.h"
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>
#include <OpenTransport.h>
#include <OpenTptInternet.h>
#include <string.h>

#define NTP_TO_MAC_EPOCH 126230400UL    /* seconds from 1900-01-01 to 1904-01-01 */
#define FALLBACK_DNS     0x01010101UL   /* 1.1.1.1, if nothing else is available */
#define STARTUP_WINDOW   (60UL * 180)   /* launched within 3 minutes of boot = "at startup" */

enum { kAlertID = 128 };

static Boolean OptionKeyDown(void)
{
    KeyMap keys;
    GetKeys(keys);
    return (((unsigned char *)keys)[0x3A >> 3] >> (0x3A & 7)) & 1;
}

static void ShowMessage(ConstStringPtr msg)
{
    ParamText(msg, "\p", "\p", "\p");
    InitCursor();
    NoteAlert(kAlertID, NULL);
}

/* ------------------------------------------------------------------ */
/* Open Transport name lookup                                         */
/* ------------------------------------------------------------------ */

static Boolean HaveOTTCP(void)
{
    long resp;
    return Gestalt(gestaltOpenTpt, &resp) == noErr
        && (resp & gestaltOpenTptPresentMask) && (resp & gestaltOpenTptTCPPresentMask);
}

/* Resolves `name` with Open Transport. Also reports OT's DNS server (from DHCP
   or the TCP/IP control panel) for the fallback lookup. Returns the number of
   addresses found. */
static int OTLookup(ConstStringPtr name, ip_addr addrs[kMaxAddrs], ip_addr *otDNS)
{
    InetInterfaceInfo info;
    InetSvcRef svc;
    InetHostInfo host;
    OSStatus err;
    char cname[256];
    int i, n = 0;

    *otDNS = 0;
    if (InitOpenTransport() != noErr) return 0;
    if (OTInetGetInterfaceInfo(&info, kDefaultInetInterface) == noErr)
        *otDNS = info.fDNSAddr;

    svc = OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err);
    if (svc) {
        BlockMoveData(name + 1, cname, name[0]);
        cname[name[0]] = 0;
        OTSetSynchronous(svc);
        if (OTInetStringToAddress(svc, cname, &host) == noErr)
            for (i = 0; i < kMaxHostAddrs && i < kMaxAddrs && host.addrs[i]; i++)
                addrs[n++] = host.addrs[i];
        OTCloseProvider(svc);
    }
    CloseOpenTransport();
    return n;
}

/* ------------------------------------------------------------------ */
/* Sync                                                               */
/* ------------------------------------------------------------------ */

/* Does the whole sync. Fills in the result fields of *p. */
static void DoSync(MacNTPPrefs *p)
{
    ip_addr addrs[kMaxAddrs];
    int n = 0;
    Boolean notFound = false, viaOT = false, dst;
    ip_addr dnsServer = 0, otDNS = 0, used = 0;
    unsigned long ntpSeconds = 0, oldMac, newMac;
    UDPConn conn;
    Str255 msg;
    OSErr err;

    p->lastErr = -1;
    p->lastAdjust = 0;
    msg[0] = 0;

    if (p->server[0] == 0) {
        PStrCopyC(p->lastMessage, "No NTP server is set.");
        return;
    }

    /* 1. Find the server's address(es). */
    addrs[0] = ParseIPv4(p->server);
    if (addrs[0]) {
        n = 1;
    } else if (p->dns[0] == 0 && HaveOTTCP()) {
        n = OTLookup(p->server, addrs, &otDNS);              /* Open Transport's own resolver */
        viaOT = n > 0;
    }

    memset(&conn, 0, sizeof conn);
    err = UDPOpen(&conn);
    if (err) {
        PStrCopyC(p->lastMessage, "The network isn't available (MacTCP/Open Transport error ");
        PStrAppendNum(p->lastMessage, err);
        PStrAppendC(p->lastMessage, ").");
        p->lastErr = err;
        return;
    }

    if (n == 0) {                                            /* built-in DNS client */
        dnsServer = ParseIPv4(p->dns);
        if (!dnsServer) dnsServer = otDNS ? otDNS : FALLBACK_DNS;
        n = DNSLookup(&conn, dnsServer, p->server, addrs, &notFound);
        if (n == 0) {
            UDPClose(&conn);
            PStrCopyC(p->lastMessage, notFound ? "The NTP server name \"" : "Couldn't look up \"");
            PStrAppend(p->lastMessage, p->server);
            PStrAppendC(p->lastMessage, notFound ? "\" doesn't exist." : "\" (no answer from DNS server ");
            if (!notFound) { PStrAppendIP(p->lastMessage, dnsServer); PStrAppendC(p->lastMessage, ")."); }
            return;
        }
    }

    /* 2. Ask all the addresses at once and use the first answer. */
    used = NTPQueryAny(&conn, addrs, n, &ntpSeconds);
    UDPClose(&conn);

    if (!used || ntpSeconds <= NTP_TO_MAC_EPOCH) {
        PStrCopyC(p->lastMessage, n > 1 ? "None of the time servers for \"" : "No answer from the time server \"");
        PStrAppend(p->lastMessage, p->server);
        PStrAppendC(p->lastMessage, "\".");
        return;
    }

    /* 3. Set the clock. */
    newMac = UTCToLocal(ntpSeconds - NTP_TO_MAC_EPOCH, p->stdOffset, p->dstRule, &dst);
    GetDateTime(&oldMac);
    err = SetDateTime(newMac);
    if (err) {
        PStrCopyC(p->lastMessage, "The system refused the new time (error ");
        PStrAppendNum(p->lastMessage, err);
        PStrAppendC(p->lastMessage, ").");
        p->lastErr = err;
        return;
    }

    p->lastErr = noErr;
    p->lastSyncTime = newMac;
    p->lastAdjust = (long)(newMac - oldMac);
    PStrCopyC(p->lastMessage, "Synced ");
    AppendDateTime(p->lastMessage, newMac, false);
    PStrAppendC(p->lastMessage, dst ? " (daylight time) " : " (standard time) ");
    PStrAppendC(p->lastMessage, "from ");
    PStrAppendIP(p->lastMessage, used);
    PStrAppendC(p->lastMessage, viaOT ? " via Open Transport DNS. Adjusted " : ". Adjusted ");
    PStrAppendSignedNum(p->lastMessage, p->lastAdjust);
    PStrAppendC(p->lastMessage, p->lastAdjust == 1 || p->lastAdjust == -1 ? " second." : " seconds.");
    (void)msg;
}

int main(void)
{
    MacNTPPrefs p;
    Boolean requested, atStartup, show;
    Str255 msg;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    FlushEvents(everyEvent, 0);

    PrefsLoad(&p);
    requested = p.requestStamp != p.resultStamp;          /* Sync Now in the control panel */
    atStartup = TickCount() < STARTUP_WINDOW;
    show = !requested && (OptionKeyDown() || !atStartup);

    if (!requested && atStartup && !p.syncAtStartup && !show)
        return 0;                                          /* startup sync is turned off */

    DoSync(&p);
    p.resultStamp = p.requestStamp;
    PrefsSaveResult(&p);

    if (show || (!requested && p.lastErr != noErr)) {
        PStrCopyC(msg, p.lastErr == noErr ? "" : "MacNTP couldn't set the clock. ");
        PStrAppend(msg, p.lastMessage);
        ShowMessage(msg);
    }
    return 0;
}
