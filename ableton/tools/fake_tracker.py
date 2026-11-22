#!/usr/bin/env python3
"""Pretends to be a Polyend Tracker in DAW Control mode, for testing the
PolyendTrackerDAW remote script without the hardware.

It creates virtual MIDI ports named "Tracker" (macOS / Linux). In Live, pick
PolyendTrackerDAW as a Control Surface with Input and Output set to "Tracker".

    pip install python-rtmidi
    python3 fake_tracker.py selftest      # automated checks, prints PASS / FAIL
    python3 fake_tracker.py monitor       # just print what Live sends back
    python3 fake_tracker.py press Play    # press + release one button

The message numbers are read from ../PolyendTrackerDAW/mapping.json.
"""

import json
import os
import sys
import threading
import time

import rtmidi

HERE = os.path.dirname(os.path.abspath(__file__))
MAPPING_PATH = os.path.join(HERE, "..", "PolyendTrackerDAW", "mapping.json")

BUTTON_IDS = {
    "Screen1": 0, "Screen2": 1, "Screen3": 2, "Screen4": 3,
    "Screen5": 4, "Screen6": 5, "Screen7": 6, "Screen8": 7,
    "Params": 8, "Performance": 9, "File": 10, "Config": 11, "Master": 12,
    "SampleLoad": 13, "SamplePlay": 14, "SampleEdit": 15, "SampleRec": 16,
    "Song": 17, "Note": 18, "Instr": 19, "Vol": 20, "Fx": 21, "Pattern": 22,
    "Play": 23, "Copy": 24, "Insert": 25, "Up": 26, "Delete": 27,
    "Rec": 28, "Shift": 29, "Left": 30, "Down": 31, "Right": 32,
}
STATE_NAMES = ["play", "arrangement_record", "session_record", "overdub", "metronome"]


class FakeTracker:
    def __init__(self, mapping):
        self.m = mapping
        self.ch = int(mapping["channel"]) - 1
        self.fb = mapping["feedback"]
        self.out = rtmidi.MidiOut()
        self.out.open_virtual_port("Tracker")
        self.inp = rtmidi.MidiIn()
        self.inp.open_virtual_port("Tracker")
        self.inp.set_callback(self._on_midi)
        self.lock = threading.Lock()
        self.cc = {}
        self.leds = {}
        self.events = 0
        self.verbose = False

    # ----------------------------------------------------------- receiving
    def _on_midi(self, message, _data=None):
        msg, _delta = message
        if len(msg) < 3:
            return
        status, d1, d2 = msg[0] & 0xF0, msg[1], msg[2]
        if (msg[0] & 0x0F) != self.ch:
            return
        with self.lock:
            self.events += 1
            if status == 0xB0:
                self.cc[d1] = d2
            elif status in (0x90, 0x80):
                self.leds[d1] = 0 if status == 0x80 else d2
        if self.verbose:
            print("  <- %s" % self.describe(status, d1, d2))

    def describe(self, status, d1, d2):
        base = int(self.fb["stateCcBase"])
        if status == 0xB0:
            if base <= d1 < base + len(STATE_NAMES):
                return "state %s = %s" % (STATE_NAMES[d1 - base], "on" if d2 else "off")
            names = {int(self.fb[k]): k for k in self.fb if k != "stateCcBase"}
            names[int(self.m["helloCc"])] = "hello"
            return "CC %d (%s) = %d" % (d1, names.get(d1, "?"), d2)
        pad = d1 - int(self.m["padNoteBase"])
        return "pad %d LED = %d" % (pad, 0 if status == 0x80 else d2)

    def state(self, name):
        cc = int(self.fb["stateCcBase"]) + STATE_NAMES.index(name)
        with self.lock:
            return self.cc.get(cc)

    def value(self, key):
        with self.lock:
            return self.cc.get(int(self.fb[key]))

    # ------------------------------------------------------------- sending
    def send_cc(self, cc, value):
        self.out.send_message([0xB0 | self.ch, cc, value])

    def button(self, name, hold=0.05):
        cc = int(self.m["buttonCcBase"]) + BUTTON_IDS[name]
        self.send_cc(cc, 127)
        time.sleep(hold)
        self.send_cc(cc, 0)

    def shift_button(self, name):
        shift = int(self.m["buttonCcBase"]) + BUTTON_IDS["Shift"]
        self.send_cc(shift, 127)
        time.sleep(0.03)
        self.button(name)
        time.sleep(0.03)
        self.send_cc(shift, 0)

    def pad(self, index, velocity=127):
        note = int(self.m["padNoteBase"]) + index
        self.out.send_message([0x90 | self.ch, note, velocity])
        time.sleep(0.05)
        self.out.send_message([0x80 | self.ch, note, 0])

    def jog(self, delta):
        self.send_cc(int(self.m["jogCc"]), 64 + delta)

    def hello(self, on=True):
        self.send_cc(int(self.m["helloCc"]), 127 if on else 0)


def wait_for(predicate, timeout=2.0):
    end = time.time() + timeout
    while time.time() < end:
        if predicate():
            return True
        time.sleep(0.02)
    return predicate()


def selftest(t, wait_seconds=10):
    results = []

    def check(name, ok):
        results.append(ok)
        print("%s  %s" % ("PASS" if ok else "FAIL", name))

    print("Waiting for Live... (Control Surface: PolyendTrackerDAW, In/Out: Tracker)")
    end = time.time() + wait_seconds
    while t.state("play") is None and time.time() < end:
        t.hello(True)  # repeat until the surface is set up in Live
        wait_for(lambda: t.state("play") is not None, timeout=1.0)
    check("Live answers hello with the transport state", t.state("play") is not None)
    check("tempo is reported", wait_for(lambda: t.value("tempoLsbCc") is not None))

    start_play = t.state("play")
    t.button("Play")
    check("Play toggles playback", wait_for(lambda: t.state("play") == (0 if start_play else 127)))
    t.button("Play")
    check("Play again toggles back", wait_for(lambda: t.state("play") == start_play))

    metro = t.state("metronome")
    t.button("Screen6")
    check("Screen6 toggles the metronome", wait_for(lambda: t.state("metronome") == (0 if metro else 127)))
    t.button("Screen6")
    wait_for(lambda: t.state("metronome") == metro)

    msb, lsb = t.value("tempoMsbCc"), t.value("tempoLsbCc")
    before = ((msb or 0) << 7) | (lsb or 0)
    t.send_cc(int(t.m["buttonCcBase"]) + BUTTON_IDS["Shift"], 127)
    time.sleep(0.05)
    t.jog(1)
    time.sleep(0.05)
    t.send_cc(int(t.m["buttonCcBase"]) + BUTTON_IDS["Shift"], 0)

    def tempo_now():
        return ((t.value("tempoMsbCc") or 0) << 7) | (t.value("tempoLsbCc") or 0)

    check("Shift + jog raises the tempo by 1 BPM", wait_for(lambda: tempo_now() == before + 10))
    t.send_cc(int(t.m["buttonCcBase"]) + BUTTON_IDS["Shift"], 127)
    time.sleep(0.05)
    t.jog(-1)
    time.sleep(0.05)
    t.send_cc(int(t.m["buttonCcBase"]) + BUTTON_IDS["Shift"], 0)
    wait_for(lambda: tempo_now() == before)

    # ring navigation (needs at least 2 tracks / 2 scenes in the Set)
    t.button("Left")
    t.button("Up")
    wait_for(lambda: t.value("ringTrackCc") == 0 and t.value("ringSceneCc") == 0)
    t.button("Right")
    check("Right moves the session ring one track", wait_for(lambda: t.value("ringTrackCc") == 1))
    t.button("Left")
    check("Left moves it back", wait_for(lambda: t.value("ringTrackCc") == 0))
    t.button("Down")
    check("Down moves the ring one scene", wait_for(lambda: t.value("ringSceneCc") == 1))
    t.button("Up")
    wait_for(lambda: t.value("ringSceneCc") == 0)

    sel = t.value("selectedTrackCc")
    t.button("Instr")
    check("Instr selects the next track", wait_for(lambda: t.value("selectedTrackCc") == (sel or 0) + 1))
    t.button("Note")
    wait_for(lambda: t.value("selectedTrackCc") == sel)

    with t.lock:
        t.leds.clear()
    t.hello(True)
    check("hello resends all 48 pad LEDs", wait_for(lambda: len(t.leds) >= 48, timeout=3))

    print()
    print("%d / %d checks passed" % (sum(results), len(results)))
    return all(results)


def main():
    with open(MAPPING_PATH, "r", encoding="utf-8") as f:
        mapping = json.load(f)
    t = FakeTracker(mapping)
    cmd = sys.argv[1] if len(sys.argv) > 1 else "monitor"
    try:
        if cmd == "selftest":
            wait = int(sys.argv[2]) if len(sys.argv) > 2 else 10
            ok = selftest(t, wait)
            t.hello(False)
            sys.exit(0 if ok else 1)
        elif cmd == "press" and len(sys.argv) > 2:
            time.sleep(1.0)  # let Live see the port
            t.button(sys.argv[2])
            time.sleep(0.5)
        elif cmd == "pad" and len(sys.argv) > 2:
            time.sleep(1.0)
            t.pad(int(sys.argv[2]))
            time.sleep(0.5)
        else:
            t.verbose = True
            t.hello(True)
            print("Monitoring (Ctrl+C to stop)...")
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        t.inp.close_port()
        t.out.close_port()


if __name__ == "__main__":
    main()
