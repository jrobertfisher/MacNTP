/* MacNTP control panel resources. */
#include "MacTypes.r"
#include "Dialogs.r"
#include "Controls.r"
#include "Menus.r"
#include "Finder.r"
#include "Icons.r"
#include "Retro68.r"

type 'cdev' { RETRO68_CODE_TYPE };
resource 'cdev' (-4064, locked) {
    dontBreakAtEntry, $$read("MacNTPcdev.flt");
};

/* popup CDEF: procID 1008 (popupMenuProc) + 1 (popupFixedWidth); value 0 = title left-justified. */

/* Show on every Mac without asking (softMask $FFFF, hardMask 0). */
resource 'mach' (-4064) { 0xFFFF, 0x0000 };

/* Panel area (System 6/7 control panel coordinates start at left = 87). */
resource 'nrct' (-4064) { { {-1, 87, 250, 442} } };

resource 'DITL' (-4064) { {
    { 13, 100, 29, 185 },   StaticText { disabled, "NTP server:" };
    { 13, 192, 29, 428 },   EditText { enabled, "" };
    { 40, 100, 56, 185 },   StaticText { disabled, "DNS server:" };
    { 40, 192, 56, 300 },   EditText { enabled, "" };
    { 40, 308, 56, 438 },   StaticText { disabled, "(blank = automatic)" };
    { 66, 100, 86, 430 },   Control { enabled, -4064 };
    { 92, 100, 112, 430 },  Control { enabled, -4063 };
    { 120, 100, 138, 430 }, CheckBox { enabled, "Sync at startup" };
    { 150, 340, 170, 430 }, Button { enabled, "Sync Now" };
    { 152, 100, 168, 335 }, StaticText { disabled, "" };
    { 182, 100, 246, 440 }, StaticText { disabled, "" }
} };

resource 'CNTL' (-4064) {
    { 66, 100, 86, 430 }, 0, visible, 110, -4064,
    1009, 0, "Time zone:"
};
resource 'CNTL' (-4063) {
    { 92, 100, 112, 430 }, 0, visible, 110, -4063,
    1009, 0, "Daylight saving:"
};

resource 'MENU' (-4064) {
    -4064, textMenuProc, allEnabled, enabled, "Time zone",
    {
        "UTC-12", noIcon, noKey, noMark, plain,
        "UTC-11", noIcon, noKey, noMark, plain,
        "UTC-10  Hawaii", noIcon, noKey, noMark, plain,
        "UTC-9  Alaska", noIcon, noKey, noMark, plain,
        "UTC-8  US Pacific", noIcon, noKey, noMark, plain,
        "UTC-7  US Mountain, Arizona", noIcon, noKey, noMark, plain,
        "UTC-6  US Central", noIcon, noKey, noMark, plain,
        "UTC-5  US Eastern", noIcon, noKey, noMark, plain,
        "UTC-4  Atlantic", noIcon, noKey, noMark, plain,
        "UTC-3  Buenos Aires, Brasilia", noIcon, noKey, noMark, plain,
        "UTC-2", noIcon, noKey, noMark, plain,
        "UTC-1  Azores", noIcon, noKey, noMark, plain,
        "UTC  London, Lisbon", noIcon, noKey, noMark, plain,
        "UTC+1  Central Europe", noIcon, noKey, noMark, plain,
        "UTC+2  Eastern Europe", noIcon, noKey, noMark, plain,
        "UTC+3  Moscow, Istanbul", noIcon, noKey, noMark, plain,
        "UTC+4  Dubai", noIcon, noKey, noMark, plain,
        "UTC+5  Pakistan", noIcon, noKey, noMark, plain,
        "UTC+6  Bangladesh", noIcon, noKey, noMark, plain,
        "UTC+7  Bangkok", noIcon, noKey, noMark, plain,
        "UTC+8  China, Singapore", noIcon, noKey, noMark, plain,
        "UTC+9  Japan, Korea", noIcon, noKey, noMark, plain,
        "UTC+10  Sydney (std)", noIcon, noKey, noMark, plain,
        "UTC+11", noIcon, noKey, noMark, plain,
        "UTC+12  New Zealand (std)", noIcon, noKey, noMark, plain,
        "UTC+13", noIcon, noKey, noMark, plain,
        "UTC+14", noIcon, noKey, noMark, plain
    }
};
resource 'MENU' (-4063) {
    -4063, textMenuProc, allEnabled, enabled, "Daylight saving",
    {
        "US rules", noIcon, noKey, noMark, plain,
        "EU rules", noIcon, noKey, noMark, plain,
        "None", noIcon, noKey, noMark, plain
    }
};

#define MACNTP_ICON_ID -4064
#include "icons.r"

resource 'BNDL' (-4064) {
    'MNtc', 0,
    { 'ICN#', { 0, -4064 }; 'FREF', { 0, -4064 } }
};
resource 'FREF' (-4064) { 'cdev', 0, "" };
data 'MNtc' (0, "Owner resource") {
    $"0A" "MacNTP 2.0"
};

resource 'vers' (1) {
    0x02, 0x00, release, 0x00, verUS, "2.0",
    "2.0, MacNTP: sets your clock from an NTP server"
};
