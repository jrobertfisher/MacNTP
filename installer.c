/*
 * MacNTP Installer: installs or removes MacNTP on a chosen disk.
 *
 *   MacNTP          -> <disk>:System Folder:Control Panels:
 *   MacNTP Startup  -> <disk>:System Folder:Startup Items:
 *
 * Both files travel inside this application as resources:
 *   'MNfk' 128/129 = resource forks of MacNTP / MacNTP Startup
 *   'MNdk' 128/129 = data forks (normally empty)
 * Folders are located with FindFolder, so localized or renamed System
 * Folders work, and missing folders are created.
 */
#include "common.h"
#include <Controls.h>
#include <ControlDefinitions.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <Folders.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Memory.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Windows.h>
#include <string.h>

enum {
    kDialogID     = 128,
    kInstallBtn   = 1,
    kQuitBtn      = 2,
    kRemoveBtn    = 3,
    kDiskPopup    = 4,
    kStatusText   = 5,
    kDiskMenuID   = 128,
    kMaxDisks     = 20
};



typedef struct {
    short           resID;          /* 'MNfk' / 'MNdk' resource id */
    OSType          folder;         /* FindFolder type */
    OSType          type, creator;
    const char     *name;
} MNPart;

static const MNPart kParts[2] = {
    { 128, kControlPanelFolderType, 'cdev', kMacNTPPanelCreator, "MacNTP" },
    { 129, kStartupFolderType,      'APPL', kMacNTPAppCreator,   "MacNTP Startup" }
};

static short gDisks[kMaxDisks];        /* vRefNums, in menu order */
static short gDiskCount;

/* ------------------------------------------------------------------ */

static Handle Item(DialogPtr d, short i)
{
    short type; Handle h; Rect r;
    GetDialogItem(d, i, &type, &h, &r);
    return h;
}

static void Status(DialogPtr d, ConstStringPtr s)
{
    SetDialogItemText(Item(d, kStatusText), s);
}

static void StatusC(DialogPtr d, const char *s)
{
    Str255 p;
    PStrCopyC(p, s);
    Status(d, p);
}

/* The System 7 popup control keeps its menu in its private data; the menu is
   not in the menu list, so GetMenuHandle() can't find it. */
static MenuHandle PopupMenu(ControlHandle popup)
{
    PopupPrivateDataHandle data = (PopupPrivateDataHandle)(**popup).contrlData;
    return data ? (**data).mHandle : NULL;
}

/* Fills the disk menu with mounted disks that have a System Folder. */
static void BuildDiskMenu(DialogPtr d)
{
    ControlHandle popup = (ControlHandle)Item(d, kDiskPopup);
    MenuHandle menu = PopupMenu(popup);
    HParamBlockRec pb;
    Str255 name;
    short index, startupVRef = 0, selected = 1;
    long dirID;

    if (!menu) return;
    while (CountMenuItems(menu) > 0) DeleteMenuItem(menu, 1);
    FindFolder(kOnSystemDisk, kSystemFolderType, kDontCreateFolder, &startupVRef, &dirID);

    gDiskCount = 0;
    for (index = 1; gDiskCount < kMaxDisks; index++) {
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioNamePtr = name;
        pb.volumeParam.ioVolIndex = index;
        if (PBHGetVInfoSync(&pb) != noErr) break;
        if (pb.volumeParam.ioVFndrInfo[0] == 0) continue;           /* no blessed System Folder */
        if (pb.volumeParam.ioVAtrb & 0x8080) continue;              /* locked (hardware or software) */
        AppendMenu(menu, "\px");                                    /* then set the name literally */
        SetMenuItemText(menu, CountMenuItems(menu), name);
        gDisks[gDiskCount++] = pb.volumeParam.ioVRefNum;
        if (pb.volumeParam.ioVRefNum == startupVRef) selected = gDiskCount;
    }
    SetControlMaximum(popup, gDiskCount > 0 ? gDiskCount : 1);
    SetControlValue(popup, selected);
    if (gDiskCount == 0) {
        AppendMenu(menu, "\p(no disk with a System Folder)");
        HiliteControl((ControlHandle)Item(d, kInstallBtn), 255);
        HiliteControl((ControlHandle)Item(d, kRemoveBtn), 255);
    }
    Draw1Control(popup);
}

static short ChosenDisk(DialogPtr d, StringPtr name)
{
    ControlHandle popup = (ControlHandle)Item(d, kDiskPopup);
    short i = GetControlValue(popup);
    MenuHandle menu = PopupMenu(popup);
    if (i < 1 || i > gDiskCount) return 0;
    if (name && menu) GetMenuItemText(menu, i, name);
    return gDisks[i - 1];
}

/* ------------------------------------------------------------------ */

static OSErr PartSpec(short vRefNum, const MNPart *c, Boolean create, FSSpec *spec)
{
    short fv; long dir; Str255 name;
    OSErr err = FindFolder(vRefNum, c->folder, create, &fv, &dir);
    if (err) return err;
    PStrCopyC(name, c->name);
    return FSMakeFSSpec(fv, dir, name, spec);       /* fnfErr = not installed */
}

static OSErr WriteFork(const FSSpec *spec, Boolean rsrc, Handle h)
{
    short ref;
    long len = h ? GetHandleSize(h) : 0;
    OSErr err = rsrc ? FSpOpenRF(spec, fsWrPerm, &ref) : FSpOpenDF(spec, fsWrPerm, &ref);
    if (err) return err;
    if (len > 0) {
        HLock(h);
        err = FSWrite(ref, &len, *h);
        HUnlock(h);
    }
    if (!err) err = SetEOF(ref, len);
    FSClose(ref);
    return err;
}

static OSErr InstallPart(short vRefNum, const MNPart *c, Boolean *replaced)
{
    FSSpec spec;
    FInfo fi;
    Handle rsrc, data;
    OSErr err = PartSpec(vRefNum, c, kCreateFolder, &spec);

    *replaced = false;
    if (err == noErr) {                             /* an older copy is there */
        err = FSpDelete(&spec);
        if (err) return err;
        *replaced = true;
    } else if (err != fnfErr) {
        return err;
    }

    rsrc = Get1Resource('MNfk', c->resID);
    data = Get1Resource('MNdk', c->resID);
    if (!rsrc) return resNotFound;
    err = FSpCreate(&spec, c->creator, c->type, smSystemScript);
    if (!err) err = WriteFork(&spec, true, rsrc);
    if (!err && data && GetHandleSize(data) > 0) err = WriteFork(&spec, false, data);
    if (!err && FSpGetFInfo(&spec, &fi) == noErr) {
        fi.fdFlags = (fi.fdFlags | kHasBundle) & ~kHasBeenInited;     /* show the icon */
        err = FSpSetFInfo(&spec, &fi);
    }
    if (err) FSpDelete(&spec);                      /* don't leave half a file behind */
    return err;
}

/* Version 1 of MacNTP was an application called "MacNTP" kept in Startup Items. */
static void RemoveVersion1(short vRefNum)
{
    short fv; long dir;
    FSSpec spec;
    FInfo fi;
    if (FindFolder(vRefNum, kStartupFolderType, kDontCreateFolder, &fv, &dir) == noErr
        && FSMakeFSSpec(fv, dir, "\pMacNTP", &spec) == noErr
        && FSpGetFInfo(&spec, &fi) == noErr
        && fi.fdType == 'APPL' && fi.fdCreator == kMacNTPAppCreator)
        FSpDelete(&spec);
}

static void Explain(StringPtr msg, OSErr err, const MNPart *c)
{
    PStrAppendC(msg, "Couldn't ");
    PStrAppendC(msg, c ? "install " : "remove ");
    if (c) PStrAppendC(msg, c->name);
    if (err == fBsyErr) {
        PStrAppendC(msg, c && c->type == 'APPL'
            ? ": MacNTP Startup is running. Wait a moment and try again."
            : ": it's open. Close the MacNTP control panel and try again.");
    } else if (err == wPrErr || err == vLckdErr) {
        PStrAppendC(msg, ": the disk is locked.");
    } else if (err == dskFulErr) {
        PStrAppendC(msg, ": the disk is full.");
    } else {
        PStrAppendC(msg, " (error ");
        PStrAppendNum(msg, err);
        PStrAppendC(msg, ").");
    }
}

static void DoInstall(DialogPtr d)
{
    Str255 disk, msg;
    short vRefNum = ChosenDisk(d, disk);
    Boolean replaced, anyReplaced = false;
    int i;
    OSErr err;

    if (!vRefNum) return;
    SetCursor(*GetCursor(watchCursor));
    for (i = 0; i < 2; i++) {
        err = InstallPart(vRefNum, &kParts[i], &replaced);
        if (err) {
            msg[0] = 0;
            Explain(msg, err, &kParts[i]);
            InitCursor();
            Status(d, msg);
            return;
        }
        anyReplaced |= replaced;
    }
    RemoveVersion1(vRefNum);
    InitCursor();

    PStrCopyC(msg, anyReplaced ? "MacNTP was updated on \"" : "MacNTP is installed on \"");
    PStrAppend(msg, disk);
    PStrAppendC(msg, "\". Open it from Apple menu > Control Panels > MacNTP. "
                     "MacNTP Startup is in Startup Items, so the clock syncs each time the Mac starts.");
    Status(d, msg);
}

static void DoRemove(DialogPtr d)
{
    Str255 disk, msg;
    short vRefNum = ChosenDisk(d, disk);
    FSSpec spec;
    int i, removed = 0;
    OSErr err;

    if (!vRefNum) return;
    for (i = 0; i < 2; i++) {
        if (PartSpec(vRefNum, &kParts[i], kDontCreateFolder, &spec) != noErr) continue;
        err = FSpDelete(&spec);
        if (err) {
            msg[0] = 0;
            Explain(msg, err, NULL);
            PStrAppendC(msg, " ");
            PStrAppendC(msg, kParts[i].name);
            Status(d, msg);
            return;
        }
        removed++;
    }
    RemoveVersion1(vRefNum);
    if (removed) {
        PStrCopyC(msg, "MacNTP was removed from \"");
        PStrAppend(msg, disk);
        PStrAppendC(msg, "\". Your settings (MacNTP Prefs) were kept.");
    } else {
        PStrCopyC(msg, "MacNTP isn't installed on \"");
        PStrAppend(msg, disk);
        PStrAppendC(msg, "\".");
    }
    Status(d, msg);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    DialogPtr d;
    short hit = 0;
    long resp;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    FlushEvents(everyEvent, 0);
    InitCursor();

    if (Gestalt(gestaltFindFolderAttr, &resp) != noErr || !(resp & (1L << gestaltFindFolderPresent))) {
        ParamText("\pMacNTP needs System 7 or later.", "\p", "\p", "\p");
        StopAlert(129, NULL);
        return 0;
    }

    d = GetNewDialog(kDialogID, NULL, (WindowPtr)-1);
    if (!d) return 0;
    SetDialogDefaultItem(d, kInstallBtn);
    SetDialogCancelItem(d, kQuitBtn);
    BuildDiskMenu(d);
    StatusC(d, "Choose a disk, then click Install. The files go into its System Folder: "
               "MacNTP in Control Panels and MacNTP Startup in Startup Items.");
    ShowWindow(d);

    while (hit != kQuitBtn) {
        ModalDialog(NULL, &hit);
        if (hit == kInstallBtn) DoInstall(d);
        else if (hit == kRemoveBtn) DoRemove(d);
    }
    DisposeDialog(d);
    return 0;
}
