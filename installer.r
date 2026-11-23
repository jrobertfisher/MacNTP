/* MacNTP Installer resources. Run Rez from the build directory (for $$read). */
#include "MacTypes.r"
#include "Dialogs.r"
#include "Controls.r"
#include "Menus.r"
#include "Processes.r"
#include "Finder.r"
#include "Icons.r"

resource 'DLOG' (128) {
    { 60, 60, 300, 500 },
    movableDBoxProc,
    invisible,
    noGoAway,
    0,
    128,
    "MacNTP 2.0 Installer",
    centerMainScreen
};

resource 'DITL' (128) {
    {
        /* 1 */ { 206, 340, 226, 420 }, Button { enabled, "Install" };
        /* 2 */ { 206, 250, 226, 330 }, Button { enabled, "Quit" };
        /* 3 */ { 206, 20, 226, 100 },  Button { enabled, "Remove" };
        /* 4 */ { 84, 20, 104, 420 },   Control { enabled, 128 };
        /* 5 */ { 116, 20, 196, 420 },  StaticText { disabled, "" };
        /* 6 */ { 14, 20, 46, 52 },     Icon { disabled, 128 };
        /* 7 */ { 14, 66, 34, 420 },    StaticText { disabled, "MacNTP 2.0" };
        /* 8 */ { 36, 66, 72, 420 },    StaticText { disabled, "Sets your Mac's clock from an internet time server (NTP)." }
    }
};

/* popup CDEF: procID 1008 (popupMenuProc) + 1 (popupFixedWidth); value 0 = title left-justified */
resource 'CNTL' (128) {
    { 84, 20, 104, 420 }, 0, visible, 80, 128, 1009, 0, "Install on:"
};

resource 'MENU' (128) {
    128, textMenuProc, allEnabled, enabled, "Disks", { }
};

resource 'ALRT' (129) {
    { 40, 40, 140, 380 }, 129,
    { OK, visible, sound1; OK, visible, sound1; OK, visible, sound1; OK, visible, sound1 },
    alertPositionMainScreen
};
resource 'DITL' (129) {
    {
        { 70, 260, 90, 330 }, Button { enabled, "OK" };
        { 10, 60, 60, 330 },  StaticText { disabled, "^0" }
    }
};

/* The two MacNTP files, embedded. */
data 'MNfk' (128, "MacNTP (control panel) resource fork")  { $$read("MacNTP.rsrc") };
data 'MNdk' (128, "MacNTP (control panel) data fork")      { $$read("MacNTP.data") };
data 'MNfk' (129, "MacNTP Startup resource fork")          { $$read("MacNTPStartup.rsrc") };
data 'MNdk' (129, "MacNTP Startup data fork")              { $$read("MacNTPStartup.data") };

#define MACNTP_ICON_ID 128
#include "icons.r"

resource 'BNDL' (128) {
    'MNti', 0,
    { 'ICN#', { 0, 128 }; 'FREF', { 0, 128 } }
};
resource 'FREF' (128) { 'APPL', 0, "" };
data 'MNti' (0, "Owner resource") { $"10" "MacNTP Installer" };

resource 'vers' (1) {
    0x02, 0x00, release, 0x00, verUS, "2.0",
    "2.0, MacNTP Installer"
};

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    notHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    256 * 1024, 192 * 1024
};
