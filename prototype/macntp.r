#include "MacTypes.r"
#include "Dialogs.r"
#include "Processes.r"

/* NTP server: a host name or an IPv4 address.
   Names are looked up with MacNTP's built-in DNS client (see STR 131),
   and every address returned is tried in turn. */
resource 'STR ' (128, "NTP server") {
    "pool.ntp.org"
};

/* DNS server used to look up the NTP server name.
   1.1.1.1 is Cloudflare; your router (e.g. 192.168.1.1) also works. */
resource 'STR ' (131, "DNS server") {
    "1.1.1.1"
};

/* Standard-time UTC offset in seconds. Leave empty to use the Map control panel.
   Examples: -18000 Eastern, -21600 Central, -25200 Mountain, -28800 Pacific. */
resource 'STR ' (129, "Standard UTC offset") {
    ""
};

/* Daylight saving rule: "US", "EU" or "none". */
resource 'STR ' (130, "DST rule") {
    "US"
};

resource 'ALRT' (128) {
    { 40, 40, 170, 440 },
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
        { 95, 310, 115, 380 },
        Button { enabled, "OK" };

        { 10, 70, 85, 390 },
        StaticText { disabled, "^0" };

        { 10, 20, 42, 52 },
        Icon { disabled, 1 };
    }
};

resource 'vers' (1) {
    0x01, 0x00, release, 0x00,
    verUS,
    "1.0",
    "1.0, MacNTP: sets the clock from an NTP server"
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
    96 * 1024,
    96 * 1024
};
