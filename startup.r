/* MacNTP Startup application resources. */
#include "MacTypes.r"
#include "Dialogs.r"
#include "Processes.r"
#include "Finder.r"
#include "Icons.r"

resource 'ALRT' (128) {
    { 40, 40, 190, 450 },
    128,
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (128) {
    {
        { 118, 320, 138, 390 },
        Button { enabled, "OK" };

        { 10, 70, 110, 400 },
        StaticText { disabled, "^0" };

        { 10, 20, 42, 52 },
        Icon { disabled, 1 };
    }
};

#define MACNTP_ICON_ID 128
#include "icons.r"

resource 'BNDL' (128) {
    'MNtp', 0,
    { 'ICN#', { 0, 128 }; 'FREF', { 0, 128 } }
};
resource 'FREF' (128) { 'APPL', 0, "" };
data 'MNtp' (0, "Owner resource") {
    $"0E" "MacNTP Startup"
};

resource 'vers' (1) {
    0x02, 0x00, release, 0x00, verUS, "2.0",
    "2.0, MacNTP: sets your clock from an NTP server"
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    384 * 1024,
    256 * 1024
};
