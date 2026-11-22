#!/usr/bin/env python3
"""Offline test of PolyendTrackerDAW.py with a stubbed _Framework and Live Song.

It checks the script's own logic (button -> action mapping, Shift, jog,
feedback messages). It does not prove the script loads in Live; for that use
fake_tracker.py selftest with Live running.

    python3 test_script_offline.py
"""

import importlib.util
import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "PolyendTrackerDAW", "PolyendTrackerDAW.py")


# ---------------------------------------------------------------- stubs
class Element:
    def __init__(self, momentary, msg_type, channel, identifier):
        self.channel, self.identifier, self.msg_type = channel, identifier, msg_type
        self.listeners = []
        self.sent = []
        self.name = ""

    def add_value_listener(self, fn):
        self.listeners.append(fn)

    def receive(self, value):
        for fn in self.listeners:
            fn(value)

    def clear_send_cache(self):
        pass


class Matrix:
    def __init__(self, rows):
        self.rows = rows


class ClipSlot:
    def __init__(self):
        self.values = {}
        self.updates = 0

    def __getattr__(self, name):
        if name.startswith("set_"):
            return lambda v, n=name: self.values.__setitem__(n, v)
        raise AttributeError(name)

    def update(self):
        self.updates += 1


class Scene:
    def __init__(self, n):
        self.slots = [ClipSlot() for _ in range(n)]

    def clip_slot(self, i):
        return self.slots[i]


class Session:
    def __init__(self, tracks, scenes):
        self._t, self._s = 0, 0
        self.scenes = [Scene(tracks) for _ in range(scenes)]
        self.name = ""

    def set_clip_launch_buttons(self, m):
        self.matrix = m

    def scene(self, i):
        return self.scenes[i]

    def set_offsets(self, t, s):
        self._t, self._s = t, s

    def track_offset(self):
        return self._t

    def scene_offset(self):
        return self._s


class Obj:
    pass


class FakeSong:
    def __init__(self):
        self.is_playing = False
        self.record_mode = False
        self.session_record = False
        self.arrangement_overdub = False
        self.metronome = False
        self.tempo = 120.0
        self.can_undo = self.can_redo = self.can_capture_midi = True
        self.calls = []
        self.tracks = [Obj() for _ in range(20)]
        for i, t in enumerate(self.tracks):
            t.arm, t.can_be_armed, t.idx = False, True, i
            t.stop_all_clips = (lambda i=i: self.calls.append("stop_track_%d" % i))
        self.visible_tracks = self.tracks
        self.scenes = [Obj() for _ in range(8)]
        for i, s in enumerate(self.scenes):
            s.fire = (lambda i=i: self.calls.append("fire_scene_%d" % i))
        self.view = Obj()
        self.view.selected_track = self.tracks[0]
        self.view.selected_scene = self.scenes[0]

    def start_playing(self):
        self.is_playing = True

    def stop_playing(self):
        self.is_playing = False

    def continue_playing(self):
        self.calls.append("continue")
        self.is_playing = True

    def tap_tempo(self):
        self.calls.append("tap")

    def undo(self):
        self.calls.append("undo")

    def redo(self):
        self.calls.append("redo")

    def capture_midi(self):
        self.calls.append("capture")

    def stop_all_clips(self):
        self.calls.append("stop_all")


SONG = FakeSong()


class ControlSurface:
    def __init__(self, c_instance):
        self.midi_out = []
        self.slots = []
        self.highlight = None

    def component_guard(self):
        class G:
            def __enter__(s):
                return s

            def __exit__(s, *a):
                return False
        return G()

    def song(self):
        return SONG

    def log_message(self, *m):
        pass

    def show_message(self, m):
        pass

    def _send_midi(self, msg):
        self.midi_out.append(tuple(msg))

    def register_slot(self, subject, fn, event):
        self.slots.append((event, fn))

    def set_highlighting_session_component(self, s):
        self.highlight = s

    def disconnect(self):
        pass


def install_stubs():
    fw = types.ModuleType("_Framework")
    mods = {
        "_Framework": fw,
        "_Framework.ButtonElement": types.SimpleNamespace(ButtonElement=Element),
        "_Framework.ButtonMatrixElement": types.SimpleNamespace(ButtonMatrixElement=Matrix),
        "_Framework.ControlSurface": types.SimpleNamespace(ControlSurface=ControlSurface),
        "_Framework.InputControlElement": types.SimpleNamespace(MIDI_CC_TYPE=1, MIDI_NOTE_TYPE=0),
        "_Framework.SessionComponent": types.SimpleNamespace(SessionComponent=Session),
    }
    for name, mod in mods.items():
        sys.modules[name] = mod


# ---------------------------------------------------------------- tests
def main():
    install_stubs()
    spec = importlib.util.spec_from_file_location("ptd", SCRIPT)
    ptd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ptd)

    s = ptd.PolyendTrackerDAW(None)
    fails = []

    def check(name, ok):
        print("%s  %s" % ("PASS" if ok else "FAIL", name))
        if not ok:
            fails.append(name)

    def press(name, shift=False):
        if shift:
            s._buttons[ptd.BUTTON_IDS["Shift"]].receive(127)
        b = s._buttons[ptd.BUTTON_IDS[name]]
        b.receive(127)
        b.receive(0)
        if shift:
            s._buttons[ptd.BUTTON_IDS["Shift"]].receive(0)

    def last_cc(cc):
        for msg in reversed(s.midi_out):
            if msg[0] == 0xBF and msg[1] == cc:
                return msg[2]
        return None

    def fire_listeners(event):
        for ev, fn in s.slots:
            if ev == event:
                fn()

    check("channel 16 -> framework channel 15", s._channel == 15)
    check("button CC numbers start at 20", s._buttons[0].identifier == 20 and s._buttons[23].identifier == 43)
    check("pads are notes 36..83 in 4 rows", s._pads.rows[0][0].identifier == 36 and s._pads.rows[3][11].identifier == 83)
    check("startup sends hello", (0xBF, 119, 127) in s.midi_out)
    check("startup sends tempo 1200 tenths", last_cc(110) == 1200 >> 7 and last_cc(111) == 1200 & 127)
    check("LED values come from mapping.json", s._session.scene(0).clip_slot(0).values["set_started_value"] == 31)
    check("session ring is highlighted", s.highlight is s._session)

    press("Play")
    check("Play starts playback", SONG.is_playing)
    fire_listeners("is_playing")
    check("play state is sent back (CC100=127)", last_cc(100) == 127)
    press("Play")
    check("Play again stops", not SONG.is_playing)
    press("Play", shift=True)
    check("Shift+Play continues", SONG.calls[-1] == "continue")

    press("Rec")
    check("Rec toggles arrangement record", SONG.record_mode)
    press("Rec", shift=True)
    check("Shift+Rec toggles session record", SONG.session_record)
    fire_listeners("record_mode")
    check("record states sent (CC101, CC102)", last_cc(101) == 127 and last_cc(102) == 127)

    press("Screen6")
    check("Screen6 toggles metronome", SONG.metronome)
    press("Screen6", shift=True)
    check("Shift+Screen6 taps tempo", SONG.calls[-1] == "tap")

    press("Right")
    check("Right moves ring one track", s._session.track_offset() == 1)
    check("ring position sent (CC112=1)", last_cc(112) == 1)
    press("Right", shift=True)
    check("Shift+Right moves one page", s._session.track_offset() == 13)
    press("Right", shift=True)
    check("ring clamps at the last track", s._session.track_offset() == 19)
    press("Left", shift=True)
    press("Left", shift=True)
    check("ring clamps at 0", s._session.track_offset() == 0)
    press("Down")
    press("Screen2")
    check("Screen2 fires scene offset+1", SONG.calls[-1] == "fire_scene_2")

    press("Instr")
    check("Instr selects next track", SONG.view.selected_track.idx == 1)
    fire_listeners("selected_track")
    check("selected track sent (CC114=1)", last_cc(114) == 1)
    press("Params")
    check("Params arms selected track", SONG.tracks[1].arm)
    press("Delete")
    check("Delete stops the selected track", SONG.calls[-1] == "stop_track_1")
    press("Pattern")
    press("Pattern", shift=True)
    check("Pattern undo / Shift redo", SONG.calls[-2:] == ["undo", "redo"])

    s._jog.receive(65)
    check("jog selects next scene", SONG.view.selected_scene is SONG.scenes[1])
    s._buttons[ptd.BUTTON_IDS["Shift"]].receive(127)
    s._jog.receive(66)
    s._buttons[ptd.BUTTON_IDS["Shift"]].receive(0)
    check("Shift+jog changes tempo by +2", SONG.tempo == 122.0)
    fire_listeners("tempo")
    check("tempo sent (1220 tenths)", (last_cc(110) << 7 | last_cc(111)) == 1220)

    before = s._session.scene(0).clip_slot(0).updates
    s._hello.receive(127)
    check("hello refreshes all clip slots", s._session.scene(0).clip_slot(0).updates == before + 1)
    s._buttons[ptd.BUTTON_IDS["Shift"]].receive(127)
    s._hello.receive(0)
    check("hello off clears Shift", s._shift is False)

    press("Fx")  # not mapped: must not raise
    check("unmapped button is ignored", True)

    print()
    print("%d failed" % len(fails) if fails else "all offline checks passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
