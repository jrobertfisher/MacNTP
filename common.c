/*
 * MacNTP: code shared by the control panel and the MacNTP Startup application.
 * Uses no global variables, so it is safe inside the control panel's code resource.
 */
#include "common.h"
#include <Folders.h>
#include <Resources.h>
#include <Memory.h>
#include <Script.h>
#include <TextUtils.h>
#include <DateTimeUtils.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Preferences file                                                   */
/* ------------------------------------------------------------------ */

void PrefsDefaults(MacNTPPrefs *p)
{
    memset(p, 0, sizeof *p);
    p->version = kPrefsVersion;
    PStrCopyC(p->server, "pool.ntp.org");
    p->dns[0] = 0;                      /* automatic */
    p->stdOffset = -6L * 3600;          /* US Central */
    p->dstRule = kDSTUS;
    p->syncAtStartup = true;
    PStrCopyC(p->lastMessage, "Not synced yet.");
}

static OSErr PrefsSpec(FSSpec *spec, Boolean create)
{
    short vRefNum;
    long dirID;
    OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType, create, &vRefNum, &dirID);
    if (err) return err;
    return FSMakeFSSpec(vRefNum, dirID, "\pMacNTP Prefs", spec);   /* fnfErr if it doesn't exist yet */
}

void PrefsLoad(MacNTPPrefs *p)
{
    FSSpec spec;
    short ref, saved = CurResFile();
    Handle h;

    PrefsDefaults(p);
    if (PrefsSpec(&spec, kDontCreateFolder) != noErr) return;
    ref = FSpOpenResFile(&spec, fsRdPerm);
    if (ref == -1) return;
    UseResFile(ref);
    h = Get1Resource(kPrefsResType, kPrefsResID);
    if (h && GetHandleSize(h) == sizeof(MacNTPPrefs) && ((MacNTPPrefs *)*h)->version == kPrefsVersion)
        BlockMoveData(*h, p, sizeof *p);
    CloseResFile(ref);
    UseResFile(saved);
}

/* Merges either the settings or the result into what is stored, then writes it. */
static OSErr PrefsWrite(const MacNTPPrefs *src, Boolean settings)
{
    MacNTPPrefs merged;
    FSSpec spec;
    short ref, saved = CurResFile();
    Handle h;
    OSErr err;

    PrefsLoad(&merged);
    if (settings) {
        PStrCopy(merged.server, src->server, 63);
        PStrCopy(merged.dns, src->dns, 31);
        merged.stdOffset = src->stdOffset;
        merged.dstRule = src->dstRule;
        merged.syncAtStartup = src->syncAtStartup;
        merged.requestStamp = src->requestStamp;
    } else {
        merged.resultStamp = src->resultStamp;
        merged.lastSyncTime = src->lastSyncTime;
        merged.lastErr = src->lastErr;
        merged.lastAdjust = src->lastAdjust;
        PStrCopy(merged.lastMessage, src->lastMessage, 255);
    }

    err = PrefsSpec(&spec, kCreateFolder);
    if (err == fnfErr) {
        FSpCreateResFile(&spec, kMacNTPAppCreator, kPrefsFileType, smSystemScript);
        err = ResError();
    }
    if (err) return err;
    ref = FSpOpenResFile(&spec, fsRdWrPerm);
    if (ref == -1) return ResError();
    UseResFile(ref);
    h = Get1Resource(kPrefsResType, kPrefsResID);
    if (h) {
        SetHandleSize(h, sizeof merged);
        if (MemError() == noErr) {
            BlockMoveData(&merged, *h, sizeof merged);
            ChangedResource(h);
        }
    } else {
        h = NewHandle(sizeof merged);
        if (h) {
            BlockMoveData(&merged, *h, sizeof merged);
            AddResource(h, kPrefsResType, kPrefsResID, "\pMacNTP settings");
        }
    }
    err = ResError();
    UpdateResFile(ref);
    CloseResFile(ref);
    UseResFile(saved);
    return err;
}

OSErr PrefsSaveSettings(const MacNTPPrefs *p) { return PrefsWrite(p, true); }
OSErr PrefsSaveResult(const MacNTPPrefs *p)   { return PrefsWrite(p, false); }

/* ------------------------------------------------------------------ */
/* Pascal strings                                                     */
/* ------------------------------------------------------------------ */

void PStrCopy(StringPtr dst, ConstStringPtr src, int maxLen)
{
    int n = src[0] > maxLen ? maxLen : src[0];
    BlockMoveData(src + 1, dst + 1, n);
    dst[0] = n;
}

void PStrCopyC(StringPtr dst, const char *src)
{
    size_t n = strlen(src);
    if (n > 255) n = 255;
    BlockMoveData(src, dst + 1, n);
    dst[0] = (unsigned char)n;
}

void PStrAppend(StringPtr dst, ConstStringPtr src)
{
    int n = src[0];
    if (dst[0] + n > 255) n = 255 - dst[0];
    BlockMoveData(src + 1, dst + 1 + dst[0], n);
    dst[0] += n;
}

void PStrAppendC(StringPtr dst, const char *src)
{
    Str255 tmp;
    PStrCopyC(tmp, src);
    PStrAppend(dst, tmp);
}

void PStrAppendNum(StringPtr dst, long n)
{
    Str255 tmp;
    NumToString(n, tmp);
    PStrAppend(dst, tmp);
}

void PStrAppendSignedNum(StringPtr dst, long n)
{
    if (n >= 0) PStrAppendC(dst, "+");
    PStrAppendNum(dst, n);
}

void PStrAppendIP(StringPtr dst, unsigned long ip)
{
    PStrAppendNum(dst, (ip >> 24) & 255); PStrAppendC(dst, ".");
    PStrAppendNum(dst, (ip >> 16) & 255); PStrAppendC(dst, ".");
    PStrAppendNum(dst, (ip >> 8) & 255);  PStrAppendC(dst, ".");
    PStrAppendNum(dst, ip & 255);
}

void PStrTrim(StringPtr s)
{
    int start = 1, end = s[0];
    while (start <= end && (s[start] == ' ' || s[start] == '\t')) start++;
    while (end >= start && (s[end] == ' ' || s[end] == '\t')) end--;
    if (start > 1) BlockMoveData(s + start, s + 1, end - start + 1);
    s[0] = end >= start ? end - start + 1 : 0;
}

unsigned long ParseIPv4(ConstStringPtr s)
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
        } else {
            return 0;
        }
    }
    if (dots != 3 || digits == 0) return 0;
    return (addr << 8) | part;
}

/* ------------------------------------------------------------------ */
/* Time zones and daylight saving time                                */
/* (Checked against the tz database for 2007-2039, US and EU rules.)  */
/* ------------------------------------------------------------------ */

static Boolean IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static long LeapYearsBefore(int y) { y--; return y / 4 - y / 100 + y / 400; }

static long DaysFrom1904(int y, int m, int d)
{
    static const short before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    long days = (long)(y - 1904) * 365 + LeapYearsBefore(y) - LeapYearsBefore(1904);
    days += before[m - 1] + (d - 1);
    if (m > 2 && IsLeap(y)) days++;
    return days;
}

static int Weekday(long days) { return (int)((days + 5) % 7); }     /* 1904-01-01 was a Friday; 0 = Sunday */

static int SundayOfMonth(int y, int m, int n)                       /* nth Sunday, or last if n == 0 */
{
    static const char len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (n == 0) {
        int last = len[m - 1] + (m == 2 && IsLeap(y));
        return last - Weekday(DaysFrom1904(y, m, last));
    }
    return 1 + (7 - Weekday(DaysFrom1904(y, m, 1))) % 7 + 7 * (n - 1);
}

static int YearOf(unsigned long utc)
{
    long days = (long)(utc / 86400UL);
    int y = 1904;
    while (DaysFrom1904(y + 1, 1, 1) <= days) y++;
    return y;
}

Boolean InDST(unsigned long utc, long stdOffset, int rule)
{
    int y = YearOf(utc);
    unsigned long start, end;
    if (rule == kDSTUS) {
        start = DaysFrom1904(y, 3, SundayOfMonth(y, 3, 2)) * 86400UL + 2 * 3600UL - stdOffset;
        end   = DaysFrom1904(y, 11, SundayOfMonth(y, 11, 1)) * 86400UL + 1 * 3600UL - stdOffset;
    } else if (rule == kDSTEU) {
        start = DaysFrom1904(y, 3, SundayOfMonth(y, 3, 0)) * 86400UL + 3600UL;
        end   = DaysFrom1904(y, 10, SundayOfMonth(y, 10, 0)) * 86400UL + 3600UL;
    } else {
        return false;
    }
    return utc >= start && utc < end;
}

unsigned long UTCToLocal(unsigned long utc, long stdOffset, int rule, Boolean *dstOut)
{
    Boolean dst = InDST(utc, stdOffset, rule);
    if (dstOut) *dstOut = dst;
    return utc + stdOffset + (dst ? 3600L : 0);
}

void AppendDateTime(StringPtr out, unsigned long macLocal, Boolean withSeconds)
{
    Str255 s;
    IUDateString(macLocal, abbrevDate, s);
    PStrAppend(out, s);
    PStrAppendC(out, "  ");
    IUTimeString(macLocal, withSeconds, s);
    PStrAppend(out, s);
}
