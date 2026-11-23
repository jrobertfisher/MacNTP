/*
 * MacNTP: code shared by the control panel and the MacNTP Startup application.
 */
#ifndef MACNTP_COMMON_H
#define MACNTP_COMMON_H

#include <MacTypes.h>
#include <Files.h>

#define kMacNTPAppCreator   'MNtp'      /* MacNTP Startup (application) */
#define kMacNTPPanelCreator 'MNtc'      /* MacNTP (control panel) */
#define kPrefsFileType      'pref'
#define kPrefsResType       'MNpf'
#define kPrefsResID         128
#define kPrefsVersion       1

enum { kDSTNone = 0, kDSTUS = 1, kDSTEU = 2 };

/* Settings and the last sync result, stored in "MacNTP Prefs"
   (a resource of type 'MNpf' in the Preferences folder). */
typedef struct {
    short           version;
    Str63           server;         /* host name or IPv4 address */
    Str31           dns;            /* empty = automatic (Open Transport's DNS server) */
    long            stdOffset;      /* standard-time offset from UTC, in seconds */
    short           dstRule;        /* kDSTNone / kDSTUS / kDSTEU */
    Boolean         syncAtStartup;
    Boolean         unused;
    unsigned long   requestStamp;   /* set by the control panel to ask for a sync */
    unsigned long   resultStamp;    /* set to requestStamp by MacNTP Startup when done */
    unsigned long   lastSyncTime;   /* local Mac time of the last successful sync; 0 = never */
    short           lastErr;        /* 0 = the last attempt succeeded */
    long            lastAdjust;     /* seconds the clock was moved by */
    Str255          lastMessage;    /* human-readable result of the last attempt */
} MacNTPPrefs;

void    PrefsDefaults(MacNTPPrefs *p);
void    PrefsLoad(MacNTPPrefs *p);                  /* defaults if missing or unreadable */
OSErr   PrefsSaveSettings(const MacNTPPrefs *p);    /* writes the settings, keeps the stored result */
OSErr   PrefsSaveResult(const MacNTPPrefs *p);      /* writes the result, keeps the stored settings */

/* Pascal string helpers */
void    PStrCopy(StringPtr dst, ConstStringPtr src, int maxLen);
void    PStrCopyC(StringPtr dst, const char *src);
void    PStrAppend(StringPtr dst, ConstStringPtr src);
void    PStrAppendC(StringPtr dst, const char *src);
void    PStrAppendNum(StringPtr dst, long n);
void    PStrAppendSignedNum(StringPtr dst, long n);   /* always shows + or - */
void    PStrAppendIP(StringPtr dst, unsigned long ip);
void    PStrTrim(StringPtr s);
unsigned long ParseIPv4(ConstStringPtr s);          /* 0 if not a dotted IPv4 address */

/* Time. "utc" values are seconds since 1904-01-01 00:00 UTC. */
Boolean InDST(unsigned long utc, long stdOffset, int rule);
unsigned long UTCToLocal(unsigned long utc, long stdOffset, int rule, Boolean *dstOut);
void    AppendDateTime(StringPtr out, unsigned long macLocal, Boolean withSeconds);

#endif
