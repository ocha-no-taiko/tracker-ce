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
            t.mute = t.solo = False
            t.stop_all_clips = (lambda i=i: self.calls.append("stop_track_%d" % i))
            t.devices = [Obj() for _ in range(3)] if i < 2 else []
            t.view = Obj()
            t.view.selected_device = t.devices[0] if t.devices else None
        self.visible_tracks = self.tracks
        self.return_tracks = []
        self.master_track = Obj()
        self.master_track.idx = "main"
        self.master_track.devices = [Obj()]
        self.master_track.view = Obj()
        self.master_track.view.selected_device = None
        self.re_enable_automation_enabled = True
        self.scenes = [Obj() for _ in range(8)]
        for i, s in enumerate(self.scenes):
            s.fire = (lambda i=i: self.calls.append("fire_scene_%d" % i))
        self.view = Obj()
        self.view.selected_track = self.tracks[0]
        self.view.selected_scene = self.scenes[0]
        self.view.highlighted_clip_slot = Obj()
        self.view.highlighted_clip_slot.has_clip = True
        self.view.highlighted_clip_slot.clip = "clip-A"
        self.view.detail_clip = None
        song = self

        def select_device(device):
            song.view.selected_track.view.selected_device = device
        self.view.select_device = select_device

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

    def re_enable_automation(self):
        self.calls.append("re_enable")


class FakeAppView:
    """Application.View: only the calls the script uses."""

    def __init__(self):
        self.visible = {"Session": True, "Arranger": False, "Browser": True, "Detail": False,
                        "Detail/Clip": False, "Detail/DeviceChain": False}
        self.zooms = []

    def is_view_visible(self, name):
        return self.visible.get(name, False)

    def focus_view(self, name):
        self.visible["Session"] = name == "Session"
        self.visible["Arranger"] = name == "Arranger"

    def show_view(self, name):
        self.visible[name] = True
        if name.startswith("Detail/"):
            self.visible["Detail"] = True
            for other in ("Detail/Clip", "Detail/DeviceChain"):
                self.visible[other] = other == name

    focused_document_view = "Session"

    def zoom_view(self, direction, name, modifier):
        self.zooms.append((direction, name, modifier))

    def hide_view(self, name):
        self.visible[name] = False
        if name == "Detail":
            self.visible["Detail/Clip"] = self.visible["Detail/DeviceChain"] = False


APP = Obj()
APP.view = FakeAppView()


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

    def application(self):
        return APP

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
    nav = types.SimpleNamespace(up="up", down="down", left="left", right="right")
    live = types.ModuleType("Live")
    live.Application = types.SimpleNamespace(
        Application=types.SimpleNamespace(View=types.SimpleNamespace(NavDirection=nav)))
    mods["Live"] = live
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

    keys = []

    class FakeKeys:
        def send(self, code, flags):
            keys.append((code, flags))

        def close(self):
            pass

    s._keys = FakeKeys()
    CMD, SHIFT = 0x100000, 0x20000
    view = APP.view

    def last_key():
        return keys[-1] if keys else None

    # keyboard shortcuts
    ALT = 0x80000
    press("Params")
    check("Params sends Cmd+Option+M (Hide/Show Mixer)", last_key() == (46, CMD | ALT))
    press("Config")
    check("Config sends Cmd+, (Settings)", last_key() == (43, CMD))
    press("Copy")
    check("Copy sends Cmd+C", last_key() == (8, CMD))
    press("Copy", shift=True)
    check("Shift+Copy sends Cmd+V", last_key() == (9, CMD))
    press("Delete")
    check("Delete sends Backspace", last_key() == (51, 0))
    press("Insert", shift=True)
    check("Shift+Insert sends Home", last_key() == (115, 0))
    for name, code in (("Up", 126), ("Down", 125), ("Left", 123), ("Right", 124)):
        press(name)
        check("%s sends the %s arrow key" % (name, name.lower()), last_key() == (code, 0))
    check("parse_key handles modifiers", ptd.parse_key("cmd+shift+c") == (8, CMD | SHIFT))
    check("parse_key rejects unknown keys", ptd.parse_key("cmd+nope") is None)
    s._run_action("key:cmd+shift+c")
    check("key: actions from mapping.json are sent", last_key() == (8, CMD | SHIFT))

    # Shift + arrows extend the selection
    for name, code in (("Up", 126), ("Down", 125), ("Left", 123), ("Right", 124)):
        press(name, shift=True)
        check("Shift+%s sends Shift+%s arrow" % (name, name.lower()), last_key() == (code, SHIFT))
    s._run_action("ring_page_right")
    check("ring page actions still work from mapping.json", s._session.track_offset() == 12)
    s._run_action("ring_page_left")

    # views
    press("Performance")
    check("Perform switches to Arrangement", view.is_view_visible("Arranger"))
    press("Performance")
    check("Perform switches back to Session", view.is_view_visible("Session"))
    press("File")
    check("File hides the browser", not view.is_view_visible("Browser"))
    press("File")
    check("File shows the browser again", view.is_view_visible("Browser"))

    press("Note")
    check("Note opens Clip View with the highlighted clip",
          view.is_view_visible("Detail/Clip") and SONG.view.detail_clip == "clip-A")
    press("Note")
    check("Note again closes the Detail View", not view.is_view_visible("Detail"))
    press("Instr")
    check("Instr opens Device View", view.is_view_visible("Detail/DeviceChain"))
    press("Instr")
    check("Instr again closes it", not view.is_view_visible("Detail"))

    SONG.view.selected_track = SONG.tracks[1]
    press("Master")
    check("Master selects the Main track and opens its devices",
          SONG.view.selected_track is SONG.master_track and view.is_view_visible("Detail/DeviceChain"))
    press("Master")
    check("Master again closes it and returns to the previous track",
          SONG.view.selected_track is SONG.tracks[1] and not view.is_view_visible("Detail"))

    track = SONG.tracks[1]
    press("Fx")
    check("FX2 selects the next device", track.view.selected_device is track.devices[1])
    press("Fx")
    press("Fx")
    check("FX2 stops at the last device", track.view.selected_device is track.devices[2])
    press("Vol")
    check("FX1 selects the previous device", track.view.selected_device is track.devices[1])
    SONG.view.selected_track = SONG.tracks[5]
    press("Vol")  # no devices: must not raise
    check("FX1 on a track without devices is ignored", True)
    SONG.view.selected_track = SONG.tracks[1]

    press("Insert")
    check("Insert re-enables automation", SONG.calls[-1] == "re_enable")

    # bottom buttons
    press("Screen1")
    check("Screen1 toggles the Track Activator", track.mute)
    press("Screen1")
    check("Screen1 again turns it back on", not track.mute)
    press("Screen2")
    check("Screen2 toggles solo", track.solo)
    press("Screen3")
    check("Screen3 arms the selected track", track.arm)
    press("Screen4")
    check("Screen4 undoes", SONG.calls[-1] == "undo")
    press("Screen4", shift=True)
    check("Shift+Screen4 redoes", SONG.calls[-1] == "redo")
    press("Screen5")
    check("Screen5 redoes", SONG.calls[-2:] == ["redo", "redo"])
    SONG.view.selected_track = SONG.master_track
    press("Screen1")
    press("Screen2")
    check("Act / Solo do nothing on the Main track", not hasattr(SONG.master_track, "mute"))

    # the session ring follows the selection
    SONG.view.selected_track = SONG.tracks[14]
    fire_listeners("selected_track")
    check("selected track sent (CC114=14)", last_cc(114) == 14)
    check("ring follows a selection to the right", s._session.track_offset() == 3)
    SONG.view.selected_scene = SONG.scenes[6]
    fire_listeners("selected_scene")
    check("ring follows a selection downwards", s._session.scene_offset() == 3)
    SONG.view.selected_track = SONG.tracks[3]
    fire_listeners("selected_track")
    check("ring does not move while the selection is inside", s._session.track_offset() == 3)
    SONG.view.selected_track = SONG.tracks[0]
    SONG.view.selected_scene = SONG.scenes[0]
    fire_listeners("selected_track")
    fire_listeners("selected_scene")
    check("ring follows back to the start", s._session.track_offset() == 0 and s._session.scene_offset() == 0)

    view.focus_view("Arranger")
    s._jog.receive(65)
    check("jog right zooms in (NavDirection.right) in the Arrangement",
          view.zooms[-1] == ("right", "Arranger", False))
    s._jog.receive(61)
    check("jog left zooms out 3 steps", view.zooms[-3:] == [("left", "Arranger", False)] * 3)
    view.focus_view("Session")
    s._jog.receive(65)
    check("in Session View it zooms the focused document view", view.zooms[-1][1] == "Session")
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

    press("SampleLoad")  # not mapped: must not raise
    check("unmapped button is ignored", True)

    print()
    print("%d failed" % len(fails) if fails else "all offline checks passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
