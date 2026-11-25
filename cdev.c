/*
 * MacNTP control panel.
 *
 * Edits the settings in MacNTP Prefs, shows the current time and the last
 * sync result, and launches MacNTP Startup (in the background) for Sync Now.
 * The network work happens in MacNTP Startup because Open Transport's client
 * interface needs an application.
 *
 * Built as a 68k code resource ('cdev' -4064). No global variables: all state
 * lives in the handle the Control Panel Manager keeps for us (cdevStorage).
 */
#include "common.h"
#include <Aliases.h>
#include <Controls.h>
#include <Devices.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <Folders.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <TextUtils.h>
#include <ToolUtils.h>
#include <Retro68Runtime.h>
#include <string.h>

/* Our dialog items, numbered from 1 (the Control Panel adds numItems). */
enum {
    kServerLabel = 1,
    kServerField,
    kDNSLabel,
    kDNSField,
    kDNSHint,
    kZonePopup,
    kDSTPopup,
    kStartupCheck,
    kSyncButton,
    kClockText,
    kStatusText,
    kAboutButton
};

enum { kAboutDialog = -4062 };

enum {
    kZoneUTCItem   = 13,            /* zone menu item for UTC; item n = UTC + (n - 13) hours */
    kZoneItems     = 27,
    kDSTUSItem     = 1,
    kDSTEUItem     = 2,
    kDSTNoneItem   = 3,
    kPollTicks     = 30,
    kSyncTimeout   = 60UL * 120     /* give up waiting after 2 minutes */
};

typedef struct {
    MacNTPPrefs     prefs;
    unsigned long   lastClockSecs;
    unsigned long   lastPollTicks;
    unsigned long   syncStartTicks;
    Boolean         syncing;
} CPState, **CPStateHandle;

/* ------------------------------------------------------------------ */
/* Dialog item helpers                                                */
/* ------------------------------------------------------------------ */

static Handle ItemHandle(DialogPtr d, short numItems, short item)
{
    short type;
    Handle h;
    Rect r;
    GetDialogItem(d, numItems + item, &type, &h, &r);
    return h;
}

static void SetText(DialogPtr d, short numItems, short item, ConstStringPtr s)
{
    SetDialogItemText(ItemHandle(d, numItems, item), s);
}

static void GetText(DialogPtr d, short numItems, short item, StringPtr s)
{
    GetDialogItemText(ItemHandle(d, numItems, item), s);
}

static void SetValue(DialogPtr d, short numItems, short item, short v)
{
    SetControlValue((ControlHandle)ItemHandle(d, numItems, item), v);
}

static short GetValue(DialogPtr d, short numItems, short item)
{
    return GetControlValue((ControlHandle)ItemHandle(d, numItems, item));
}

/* ------------------------------------------------------------------ */
/* Settings <-> dialog                                                */
/* ------------------------------------------------------------------ */

static void ShowSettings(DialogPtr d, short n, const MacNTPPrefs *p)
{
    long zoneItem = kZoneUTCItem + p->stdOffset / 3600;
    if (zoneItem < 1) zoneItem = 1;
    if (zoneItem > kZoneItems) zoneItem = kZoneItems;

    SetText(d, n, kServerField, p->server);
    SetText(d, n, kDNSField, p->dns);
    SetValue(d, n, kZonePopup, (short)zoneItem);
    SetValue(d, n, kDSTPopup, p->dstRule == kDSTUS ? kDSTUSItem : p->dstRule == kDSTEU ? kDSTEUItem : kDSTNoneItem);
    SetValue(d, n, kStartupCheck, p->syncAtStartup ? 1 : 0);
}

static void ReadSettings(DialogPtr d, short n, MacNTPPrefs *p)
{
    Str255 s;
    short dst;

    GetText(d, n, kServerField, s);
    PStrTrim(s);
    PStrCopy(p->server, s, 63);
    GetText(d, n, kDNSField, s);
    PStrTrim(s);
    PStrCopy(p->dns, s, 31);
    p->stdOffset = (long)(GetValue(d, n, kZonePopup) - kZoneUTCItem) * 3600;
    dst = GetValue(d, n, kDSTPopup);
    p->dstRule = dst == kDSTUSItem ? kDSTUS : dst == kDSTEUItem ? kDSTEU : kDSTNone;
    p->syncAtStartup = GetValue(d, n, kStartupCheck) != 0;
}

static void ShowStatus(DialogPtr d, short n, const MacNTPPrefs *p)
{
    Str255 s;
    if (p->lastErr == noErr || p->lastSyncTime == 0) {
        PStrCopy(s, p->lastMessage, 255);
    } else {
        PStrCopyC(s, "Last attempt failed: ");
        PStrAppend(s, p->lastMessage);
    }
    SetText(d, n, kStatusText, s);
}

static void ShowClock(DialogPtr d, short n, CPState *st)
{
    unsigned long now;
    Str255 s;
    GetDateTime(&now);
    if (now == st->lastClockSecs) return;
    st->lastClockSecs = now;
    PStrCopyC(s, "Now: ");
    AppendDateTime(s, now, true);
    SetText(d, n, kClockText, s);
}

/* ------------------------------------------------------------------ */
/* Sync Now: launch MacNTP Startup and wait for its result            */
/* ------------------------------------------------------------------ */

static OSErr FindStartupApp(FSSpec *spec)
{
    short vRefNum, index;
    long dirID;
    Boolean isFolder, wasAlias;
    HParamBlockRec vpb;
    DTPBRec dt;
    Str255 name;

    /* The usual place: Startup Items (a copy or an alias). */
    if (FindFolder(kOnSystemDisk, kStartupFolderType, kDontCreateFolder, &vRefNum, &dirID) == noErr
        && FSMakeFSSpec(vRefNum, dirID, "\pMacNTP Startup", spec) == noErr
        && ResolveAliasFile(spec, true, &isFolder, &wasAlias) == noErr && !isFolder)
        return noErr;

    /* Anywhere else: ask each disk's desktop database for our creator code. */
    for (index = 1; ; index++) {
        memset(&vpb, 0, sizeof vpb);
        vpb.volumeParam.ioVolIndex = index;
        if (PBHGetVInfoSync(&vpb) != noErr) break;
        memset(&dt, 0, sizeof dt);
        dt.ioVRefNum = vpb.volumeParam.ioVRefNum;
        if (PBDTGetPath(&dt) != noErr) continue;
        dt.ioNamePtr = name;
        dt.ioIndex = 0;
        dt.ioFileCreator = kMacNTPAppCreator;
        if (PBDTGetAPPLSync(&dt) == noErr
            && FSMakeFSSpec(vpb.volumeParam.ioVRefNum, dt.ioAPPLParID, name, spec) == noErr)
            return noErr;
    }
    return fnfErr;
}

static void StartSync(DialogPtr d, short n, CPState *st)
{
    FSSpec app;
    LaunchParamBlockRec lp;
    OSErr err;
    Str255 s;

    if (st->syncing) return;
    ReadSettings(d, n, &st->prefs);
    st->prefs.requestStamp = TickCount() | 1;
    if (st->prefs.requestStamp == st->prefs.resultStamp) st->prefs.requestStamp += 2;
    PrefsSaveSettings(&st->prefs);

    if (FindStartupApp(&app) != noErr) {
        PStrCopyC(s, "Can't find MacNTP Startup. Put it in the Startup Items folder in the System Folder.");
        SetText(d, n, kStatusText, s);
        return;
    }

    memset(&lp, 0, sizeof lp);
    lp.launchBlockID = extendedBlock;
    lp.launchEPBLength = extendedBlockLen;
    lp.launchControlFlags = launchContinue | launchNoFileFlags | launchDontSwitch;
    lp.launchAppSpec = &app;
    err = LaunchApplication(&lp);
    if (err) {
        PStrCopyC(s, "Couldn't start MacNTP Startup (error ");
        PStrAppendNum(s, err);
        PStrAppendC(s, err == memFullErr ? "; not enough memory)." : ").");
        SetText(d, n, kStatusText, s);
        return;
    }

    st->syncing = true;
    st->syncStartTicks = TickCount();
    st->lastPollTicks = TickCount();
    PStrCopyC(s, "Syncing...");
    SetText(d, n, kStatusText, s);
    HiliteControl((ControlHandle)ItemHandle(d, n, kSyncButton), 255);
}

static void PollSync(DialogPtr d, short n, CPState *st)
{
    MacNTPPrefs now;
    Str255 s;

    if (!st->syncing || TickCount() - st->lastPollTicks < kPollTicks) return;
    st->lastPollTicks = TickCount();

    PrefsLoad(&now);
    if (now.resultStamp == st->prefs.requestStamp) {
        st->prefs.resultStamp = now.resultStamp;
        st->prefs.lastSyncTime = now.lastSyncTime;
        st->prefs.lastErr = now.lastErr;
        st->prefs.lastAdjust = now.lastAdjust;
        PStrCopy(st->prefs.lastMessage, now.lastMessage, 255);
        ShowStatus(d, n, &st->prefs);
    } else if (TickCount() - st->syncStartTicks > kSyncTimeout) {
        PStrCopyC(s, "No answer from MacNTP Startup.");
        SetText(d, n, kStatusText, s);
    } else {
        return;                                     /* still waiting */
    }
    st->syncing = false;
    HiliteControl((ControlHandle)ItemHandle(d, n, kSyncButton), 0);
}

/* ------------------------------------------------------------------ */
/* Control Panel entry point                                          */
/* ------------------------------------------------------------------ */

/* About MacNTP, like MacShell's: shown until a click or a key. */
static void ShowAbout(void)
{
    GrafPtr port;
    DialogPtr d;
    EventRecord ev;

    GetPort(&port);
    d = GetNewDialog(kAboutDialog, NULL, (WindowPtr)-1);
    if (!d) return;
    ShowWindow(d);
    SetPort(d);
    DrawDialog(d);
    FlushEvents(mDownMask | keyDownMask | mUpMask, 0);
    for (;;) {
        if (WaitNextEvent(mDownMask | keyDownMask | updateMask, &ev, 20, NULL)) {
            if (ev.what == updateEvt && (DialogPtr)ev.message == d) {
                BeginUpdate(d);
                DrawDialog(d);
                EndUpdate(d);
            } else if (ev.what == mouseDown || ev.what == keyDown) {
                break;
            }
        }
    }
    DisposeDialog(d);
    SetPort(port);
}

pascal long MacNTPcdev(short message, short item, short numItems, short cpID,
                       EventRecord *ev, long storage, DialogPtr d)
{
    CPStateHandle h = (CPStateHandle)storage;
    long result = storage;
    (void)cpID;

    RETRO68_RELOCATE();

    if (message != initDev && message != macDev && (storage == 0 || storage == cdevUnset))
        return storage;

    switch (message) {
    case macDev:
        return 1;                                   /* works on any Mac */

    case initDev:
        h = (CPStateHandle)NewHandleClear(sizeof(CPState));
        if (!h) return cdevMemErr;
        HLock((Handle)h);
        PrefsLoad(&(*h)->prefs);
        ShowSettings(d, numItems, &(*h)->prefs);
        ShowStatus(d, numItems, &(*h)->prefs);
        ShowClock(d, numItems, *h);
        result = (long)h;
        break;

    case hitDev:
        switch (item - numItems) {
        case kStartupCheck:
            SetValue(d, numItems, kStartupCheck, !GetValue(d, numItems, kStartupCheck));
            break;
        case kSyncButton:
            StartSync(d, numItems, *h);
            break;
        case kAboutButton:
            ShowAbout();
            break;
        }
        break;

    case keyEvtDev:
        if (ev && ((ev->message & charCodeMask) == '\r' || (ev->message & charCodeMask) == 3)) {
            StartSync(d, numItems, *h);             /* Return or Enter = Sync Now */
            ev->what = nullEvent;
        }
        break;

    case nulDev:
        ShowClock(d, numItems, *h);
        PollSync(d, numItems, *h);
        break;

    case cutDev:   DialogCut(d);    break;
    case copyDev:  DialogCopy(d);   break;
    case pasteDev: DialogPaste(d);  break;
    case clearDev: DialogDelete(d); break;

    case closeDev:
        ReadSettings(d, numItems, &(*h)->prefs);
        PrefsSaveSettings(&(*h)->prefs);
        DisposeHandle((Handle)h);
        result = 0;
        break;
    }
    return result;
}
