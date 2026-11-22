// Sends computer-keyboard shortcuts to Ableton Live on behalf of the
// PolyendTrackerDAW remote script (macOS only).
//
// Live's API has no call for some commands (Copy, Paste, Delete, Settings,
// Automation Mode, arrow-key cursor moves), so the script starts this helper
// once and writes one line per key press to its stdin:
//
//     <macOS virtual key code> <CGEventFlags>
//
// A key is only sent while Live is the frontmost app, and it is posted to
// Live's process only, so it can never land in another app.
// Live needs the Accessibility permission (System Settings > Privacy &
// Security > Accessibility) for this to work.
//
//     osascript -l JavaScript keysender.js <Live pid>

ObjC.import("Foundation");
ObjC.import("AppKit");
ObjC.import("CoreGraphics");
ObjC.import("ApplicationServices");

function log(text) {
    var line = $.NSString.alloc.initWithUTF8String(text + "\n");
    $.NSFileHandle.fileHandleWithStandardOutput.writeData(
        line.dataUsingEncoding($.NSUTF8StringEncoding));
}

function frontmostPid() {
    var app = $.NSWorkspace.sharedWorkspace.frontmostApplication;
    return app.isNil() ? -1 : app.processIdentifier;
}

function postKey(pid, keyCode, flags) {
    var down = $.CGEventCreateKeyboardEvent(null, keyCode, true);
    var up = $.CGEventCreateKeyboardEvent(null, keyCode, false);
    $.CGEventSetFlags(down, flags);
    $.CGEventSetFlags(up, flags);
    $.CGEventPostToPid(pid, down);
    $.CGEventPostToPid(pid, up);
}

function handle(pid, line) {
    var parts = line.trim().split(/\s+/);
    if (parts.length < 1 || parts[0] === "")
        return;
    var keyCode = parseInt(parts[0], 10);
    var flags = parts.length > 1 ? parseInt(parts[1], 10) : 0;
    if (isNaN(keyCode) || isNaN(flags))
        return;
    var front = frontmostPid();
    if (front !== pid) {
        log("skip key " + keyCode + ": Live is not the frontmost app");
        return;
    }
    postKey(pid, keyCode, flags);
}

function run(argv) {
    var pid = parseInt(argv[0], 10);
    if (isNaN(pid)) {
        log("usage: keysender.js <Live pid>");
        return;
    }

    // Asks macOS to show the Accessibility prompt for Live if it is missing
    // (pass "noprompt" as the second argument when testing from a terminal).
    var prompt = argv.length < 2 || argv[1] !== "noprompt";
    var options = $.NSDictionary.dictionaryWithObjectForKey($.NSNumber.numberWithBool(prompt),
                                                             $("AXTrustedCheckOptionPrompt"));
    var trusted = $.AXIsProcessTrustedWithOptions(options);
    log("keysender started for pid " + pid + ", accessibility " + (trusted ? "granted" : "NOT granted"));

    var input = $.NSFileHandle.fileHandleWithStandardInput;
    var pending = "";
    while (true) {
        var data = input.availableData;
        if (Number(data.length) === 0)
            break; // the script closed the pipe (Live quit or the surface was removed)
        pending += $.NSString.alloc.initWithDataEncoding(data, $.NSUTF8StringEncoding).js;
        var lines = pending.split("\n");
        pending = lines.pop();
        for (var i = 0; i < lines.length; i++)
            handle(pid, lines[i]);
    }
    log("keysender stopped");
}
