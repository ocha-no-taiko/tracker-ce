"""Ableton Live control surface script for the Polyend Tracker DAW Control screen.

The Tracker firmware only sends raw button / pad / jog messages and shows what
it receives. Everything else (what each button does, Shift combinations, LED
values) is decided here and configured in mapping.json next to this file.
"""

import json
import os
import subprocess
import sys
from urllib.parse import unquote, urlparse

from _Framework.ButtonElement import ButtonElement
from _Framework.ButtonMatrixElement import ButtonMatrixElement
from _Framework.ControlSurface import ControlSurface
from _Framework.InputControlElement import MIDI_CC_TYPE, MIDI_NOTE_TYPE
from _Framework.SessionComponent import SessionComponent

SCRIPT_VERSION = "1.3.0"
NUM_TRACKS = 12
NUM_SCENES = 4

# Button ids sent by the firmware (core/interface/core/interfaceDefs.h).
BUTTON_IDS = {
    "Screen1": 0, "Screen2": 1, "Screen3": 2, "Screen4": 3,
    "Screen5": 4, "Screen6": 5, "Screen7": 6, "Screen8": 7,
    "Params": 8, "Performance": 9, "File": 10, "Config": 11, "Master": 12,
    "SampleLoad": 13, "SamplePlay": 14, "SampleEdit": 15, "SampleRec": 16,
    "Song": 17, "Note": 18, "Instr": 19, "Vol": 20, "Fx": 21, "Pattern": 22,
    "Play": 23, "Copy": 24, "Insert": 25, "Up": 26, "Delete": 27,
    "Rec": 28, "Shift": 29, "Left": 30, "Down": 31, "Right": 32,
}
SHIFT_ID = BUTTON_IDS["Shift"]
BUTTON_COUNT = 44  # interfaceButtonsCount (includes Tracker Mini buttons)

STATE_ORDER = ("play", "arrangement_record", "session_record", "overdub", "metronome")

DEFAULT_MAPPING = {
    "channel": 16,
    "buttonCcBase": 20,
    "jogCc": 70,
    "helloCc": 119,
    "padNoteBase": 36,
    "feedback": {
        "stateCcBase": 100,
        "tempoMsbCc": 110,
        "tempoLsbCc": 111,
        "ringTrackCc": 112,
        "ringSceneCc": 113,
        "selectedTrackCc": 114,
        "signatureNumeratorCc": 115,
        "signatureDenominatorCc": 116,
    },
    "ledValues": {
        "stopped": 6,
        "playing": 31,
        "triggered": 63,
        "recording": 52,
        "triggeredToRecord": 42,
        "armedEmpty": 2,
    },
    "buttons": {},
    "jog": {"default": "zoom", "shift": "tempo"},
    "ringFollowsSelection": True,
    "help": [],
}

# SysEx to the Tracker: F0 7D 50 54 <command> <index> <ASCII text> F7
# (7D = non-commercial manufacturer id; see core/interface/dawControl/dawControlConfig.h)
SYSEX_HEADER = (0xF0, 0x7D, 0x50, 0x54)
SYSEX_FILE_NAME = 1
SYSEX_PROJECT_NAME = 2
SYSEX_HELP_LINE = 3
MAX_NAME = 47        # characters the Tracker keeps for a name
MAX_HELP_LINES = 16
MAX_HELP_KEY = 11
MAX_HELP_VALUE = 39
INFO_POLL_TICKS = 10  # update_display runs about every 100 ms

# macOS virtual key codes and CGEventFlags for "key:" actions (keysender.js).
KEY_CODES = {
    "a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8, "v": 9,
    "b": 11, "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17,
    "1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23, "=": 24, "9": 25, "7": 26,
    "-": 27, "8": 28, "0": 29, "]": 30, "o": 31, "u": 32, "[": 33, "i": 34, "p": 35,
    "return": 36, "l": 37, "j": 38, "'": 39, "k": 40, ";": 41, "\\": 42, ",": 43,
    "/": 44, "n": 45, "m": 46, ".": 47, "tab": 48, "space": 49, "`": 50,
    "backspace": 51, "delete": 51, "escape": 53, "esc": 53,
    "f1": 122, "f2": 120, "f3": 99, "f4": 118, "f5": 96, "f6": 97, "f7": 98, "f8": 100,
    "f9": 101, "f10": 109, "f11": 103, "f12": 111,
    "home": 115, "pageup": 116, "forwarddelete": 117, "end": 119, "pagedown": 121,
    "left": 123, "right": 124, "down": 125, "up": 126,
}
KEY_FLAGS = {
    "shift": 0x20000, "ctrl": 0x40000, "control": 0x40000,
    "alt": 0x80000, "option": 0x80000, "opt": 0x80000,
    "cmd": 0x100000, "command": 0x100000,
}

# Built-in names for keyboard shortcuts (Live 12 manual, "Live Keyboard Shortcuts").
KEY_ACTIONS = {
    "automation_mode": "a",           # Toggle Automation Mode
    "toggle_mixer": "cmd+alt+m",      # Hide/Show Mixer
    "open_settings": "cmd+,",         # Open the Settings
    "copy": "cmd+c",
    "paste": "cmd+v",
    "delete": "backspace",
    "home": "home",                   # Move Insert Marker to Beginning / first track
    "cursor_up": "up",
    "cursor_down": "down",
    "cursor_left": "left",
    "cursor_right": "right",
    "select_up": "shift+up",          # extend the selection (Shift + arrow keys)
    "select_down": "shift+down",
    "select_left": "shift+left",
    "select_right": "shift+right",
}


def parse_key(text):
    """'cmd+shift+c' -> (key code, flags), or None if a part is unknown."""
    parts = [p.strip().lower() for p in text.split("+")]
    if not parts or not parts[-1]:
        return None
    code = KEY_CODES.get(parts[-1])
    if code is None:
        return None
    flags = 0
    for mod in parts[:-1]:
        flag = KEY_FLAGS.get(mod)
        if flag is None:
            return None
        flags |= flag
    return code, flags


class KeySender:
    """Starts keysender.js once and feeds it key presses (macOS only)."""

    def __init__(self, folder, log):
        self._script = os.path.join(folder, "keysender.js")
        self._log_path = os.path.join(folder, "keysender.log")
        self._log = log
        self._proc = None
        self._warned = False

    def send(self, code, flags):
        return self._write("%d %d\n" % (code, flags))

    def request_info(self, path):
        """Asks the helper to write Live's document URL and window title to path."""
        return self._write("info %s\n" % path)

    def _write(self, line):
        if sys.platform != "darwin":
            if not self._warned:
                self._log("PolyendTrackerDAW: keyboard shortcuts and file names only work on macOS")
                self._warned = True
            return False
        if self._proc is None or self._proc.poll() is not None:
            self._start()
        try:
            self._proc.stdin.write(line.encode("utf-8"))
            self._proc.stdin.flush()
            return True
        except (OSError, ValueError, AttributeError) as e:
            self._log("PolyendTrackerDAW: could not send key (%s)" % e)
            self._proc = None
            return False

    def _start(self):
        with open(self._log_path, "w") as log_file:
            self._proc = subprocess.Popen(
                ["/usr/bin/osascript", "-l", "JavaScript", self._script, str(os.getpid())],
                stdin=subprocess.PIPE,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                close_fds=True,
            )

    def close(self):
        proc, self._proc = self._proc, None
        if proc is None:
            return
        try:
            proc.stdin.close()
        except (OSError, ValueError):
            pass
        try:
            proc.wait(timeout=1)
        except Exception:
            proc.kill()


def load_mapping(path, log=None):
    """Returns DEFAULT_MAPPING overlaid with the JSON file (one level deep)."""
    mapping = json.loads(json.dumps(DEFAULT_MAPPING))
    try:
        with open(path, "r", encoding="utf-8") as f:
            user = json.load(f)
    except (OSError, ValueError) as e:
        if log:
            log("PolyendTrackerDAW: using defaults, could not read %s (%s)" % (path, e))
        return mapping
    for key, value in user.items():
        if isinstance(value, dict) and isinstance(mapping.get(key), dict):
            mapping[key].update(value)
        else:
            mapping[key] = value
    return mapping


def clamp(value, low, high):
    return max(low, min(high, value))


def to_ascii(text, limit):
    """What the Tracker can show: printable ASCII, anything else as '?'."""
    out = "".join(c if 32 <= ord(c) < 127 else "?" for c in str(text))
    return out[:limit]


def sysex_text(command, index, text):
    return SYSEX_HEADER + (command & 0x7F, index & 0x7F) + tuple(ord(c) for c in text) + (0xF7,)


def names_from_info(text):
    """(file name, project name) from the helper's "<document URL>\t<title>".
    Both are empty while the Live Set has not been saved."""
    url = text.split("\t", 1)[0].strip()
    if not url:
        return "", ""
    path = unquote(urlparse(url).path) if url.startswith("file:") else url
    path = path.rstrip("/")
    file_name = os.path.basename(path)
    project = os.path.basename(os.path.dirname(path))
    if project.endswith(" Project"):
        project = project[:-len(" Project")]
    return file_name, project


def help_lines(entries):
    """mapping.json "help": [["Params", "Mixer"], [], ...] -> "key\tvalue" lines."""
    lines = []
    for entry in entries[:MAX_HELP_LINES]:
        if isinstance(entry, (list, tuple)) and len(entry) >= 2:
            key = to_ascii(entry[0], MAX_HELP_KEY)
            value = to_ascii(entry[1], MAX_HELP_VALUE)
            lines.append("%s\t%s" % (key, value) if key or value else "")
        else:
            lines.append("")
    return lines


class PolyendTrackerDAW(ControlSurface):

    def __init__(self, c_instance):
        super().__init__(c_instance)
        here = os.path.dirname(os.path.abspath(__file__))
        self._mapping = load_mapping(os.path.join(here, "mapping.json"), self.log_message)
        m = self._mapping
        self._channel = clamp(int(m["channel"]), 1, 16) - 1  # 0-based for the framework
        self._feedback = m["feedback"]
        self._shift = False
        self._keys = KeySender(here, self.log_message)
        self._track_before_master = None
        self._info_path = os.path.join(here, "liveinfo.txt")
        self._info_ticks = 0
        self._names = None  # (file name, project name) last sent
        self._actions = self._build_actions()
        self._button_actions = self._resolve_buttons(m.get("buttons", {}))

        with self.component_guard():
            self._setup_controls()
            self._setup_session()
            self._setup_listeners()

        self.log_message("PolyendTrackerDAW %s loaded (channel %d)" % (SCRIPT_VERSION, self._channel + 1))
        self.show_message("Polyend Tracker DAW Control connected")
        self._send_cc(int(m["helloCc"]), 127)
        self._send_all_state(texts=True)

    # ------------------------------------------------------------------ setup
    def _setup_controls(self):
        m = self._mapping
        base = int(m["buttonCcBase"])
        self._buttons = []
        for button_id in range(BUTTON_COUNT):
            cc = base + button_id
            if cc > 127:
                break
            element = ButtonElement(True, MIDI_CC_TYPE, self._channel, cc)
            element.name = "Button_%d" % button_id
            element.add_value_listener(self._make_button_handler(button_id))
            self._buttons.append(element)

        self._jog = ButtonElement(False, MIDI_CC_TYPE, self._channel, int(m["jogCc"]))
        self._jog.name = "Jog"
        self._jog.add_value_listener(self._on_jog)

        self._hello = ButtonElement(False, MIDI_CC_TYPE, self._channel, int(m["helloCc"]))
        self._hello.name = "Hello"
        self._hello.add_value_listener(self._on_hello)

        note_base = int(m["padNoteBase"])
        rows = []
        for row in range(NUM_SCENES):
            rows.append([
                ButtonElement(True, MIDI_NOTE_TYPE, self._channel, note_base + row * NUM_TRACKS + col)
                for col in range(NUM_TRACKS)
            ])
        self._pads = ButtonMatrixElement(rows=rows)
        self._pad_elements = [pad for row in rows for pad in row]

    def _setup_session(self):
        leds = self._mapping["ledValues"]
        self._session = SessionComponent(NUM_TRACKS, NUM_SCENES)
        self._session.name = "Session"
        self._session.set_clip_launch_buttons(self._pads)
        for scene_index in range(NUM_SCENES):
            scene = self._session.scene(scene_index)
            for track_index in range(NUM_TRACKS):
                slot = scene.clip_slot(track_index)
                slot.set_stopped_value(int(leds["stopped"]))
                slot.set_started_value(int(leds["playing"]))
                slot.set_triggered_to_play_value(int(leds["triggered"]))
                slot.set_recording_value(int(leds["recording"]))
                slot.set_triggered_to_record_value(int(leds["triggeredToRecord"]))
                slot.set_record_button_value(int(leds["armedEmpty"]))
        self.set_highlighting_session_component(self._session)

    def _setup_listeners(self):
        song = self.song()
        self.register_slot(song, self._send_transport_state, "is_playing")
        self.register_slot(song, self._send_transport_state, "record_mode")
        self.register_slot(song, self._send_transport_state, "session_record")
        self.register_slot(song, self._send_transport_state, "arrangement_overdub")
        self.register_slot(song, self._send_transport_state, "metronome")
        self.register_slot(song, self._send_tempo, "tempo")
        self.register_slot(song, self._send_signature, "signature_numerator")
        self.register_slot(song, self._send_signature, "signature_denominator")
        self.register_slot(song.view, self._on_selected_track, "selected_track")
        self.register_slot(song.view, self._follow_selection, "selected_scene")

    # ----------------------------------------------------------------- input
    def _make_button_handler(self, button_id):
        def handler(value):
            self._on_button(button_id, value > 0)
        return handler

    def _on_button(self, button_id, pressed):
        if button_id == SHIFT_ID:
            self._shift = pressed
            return
        if not pressed:
            return
        entry = self._button_actions.get(button_id)
        if not entry:
            return
        action_name = entry.get("shift") if self._shift and entry.get("shift") else entry.get("press")
        self._run_action(action_name)

    def _on_jog(self, value):
        delta = value - 64
        if delta == 0:
            return
        jog = self._mapping.get("jog", {})
        action_name = jog.get("shift") if self._shift and jog.get("shift") else jog.get("default")
        if action_name == "tempo":
            song = self.song()
            song.tempo = clamp(song.tempo + delta, 20.0, 999.0)
        elif action_name == "zoom":
            self._zoom(delta)
        elif action_name == "scene_select":
            self._select_scene_relative(delta)
        elif action_name == "track_select":
            self._select_track_relative(delta)

    def _on_hello(self, value):
        if value > 0:
            # The Tracker opened its DAW Control screen: send everything again.
            self._send_all_state(force_leds=True, texts=True)
        else:
            self._shift = False

    def _run_action(self, name):
        if not name:
            return
        action = self._actions.get(name)
        if action is None and name.startswith("key:"):
            key = parse_key(name[4:])
            if key is not None:
                action = (lambda k=key: self._keys.send(*k))
        if action is None:
            self.log_message("PolyendTrackerDAW: unknown action '%s'" % name)
            return
        try:
            action()
        except Exception as e:  # keep the surface alive if one Live call fails
            self.log_message("PolyendTrackerDAW: action '%s' failed: %s" % (name, e))

    def _resolve_buttons(self, buttons):
        resolved = {}
        for key, entry in buttons.items():
            button_id = BUTTON_IDS.get(key)
            if button_id is None:
                try:
                    button_id = int(key)
                except ValueError:
                    self.log_message("PolyendTrackerDAW: unknown button '%s'" % key)
                    continue
            if isinstance(entry, str):
                entry = {"press": entry}
            resolved[button_id] = entry
        return resolved

    # --------------------------------------------------------------- actions
    def _build_actions(self):
        a = {
            "play_toggle": self._play_toggle,
            "continue": lambda: self.song().continue_playing(),
            "stop": lambda: self.song().stop_playing(),
            "arrangement_record": lambda: self._toggle("record_mode"),
            "session_record": lambda: self._toggle("session_record"),
            "overdub": lambda: self._toggle("arrangement_overdub"),
            "metronome": lambda: self._toggle("metronome"),
            "tap_tempo": lambda: self.song().tap_tempo(),
            "tempo_up": lambda: self._nudge_tempo(1),
            "tempo_down": lambda: self._nudge_tempo(-1),
            "undo": self._undo,
            "redo": self._redo,
            "capture_midi": self._capture_midi,
            "stop_all_clips": lambda: self.song().stop_all_clips(),
            "stop_selected_track": self._stop_selected_track,
            "arm_selected_track": self._arm_selected_track,
            "track_prev": lambda: self._select_track_relative(-1),
            "track_next": lambda: self._select_track_relative(1),
            "scene_prev": lambda: self._select_scene_relative(-1),
            "scene_next": lambda: self._select_scene_relative(1),
            "ring_left": lambda: self._move_ring(-1, 0),
            "ring_right": lambda: self._move_ring(1, 0),
            "ring_up": lambda: self._move_ring(0, -1),
            "ring_down": lambda: self._move_ring(0, 1),
            "ring_page_left": lambda: self._move_ring(-NUM_TRACKS, 0),
            "ring_page_right": lambda: self._move_ring(NUM_TRACKS, 0),
            "ring_page_up": lambda: self._move_ring(0, -NUM_SCENES),
            "ring_page_down": lambda: self._move_ring(0, NUM_SCENES),
            # views
            "toggle_session_arrangement": self._toggle_session_arrangement,
            "toggle_browser": self._toggle_browser,
            "toggle_clip_view": self._toggle_clip_view,
            "toggle_device_view": self._toggle_device_view,
            "toggle_main_devices": self._toggle_main_devices,
            "device_prev": lambda: self._select_device_relative(-1),
            "device_next": lambda: self._select_device_relative(1),
            # selected track
            "track_activator": self._toggle_track_activator,
            "solo_selected_track": self._toggle_solo,
            # automation
            "re_enable_automation": self._re_enable_automation,
        }
        for i in range(NUM_SCENES):
            a["scene_launch_%d" % (i + 1)] = (lambda index=i: self._launch_scene(index))
        for name, shortcut in KEY_ACTIONS.items():
            key = parse_key(shortcut)
            a[name] = (lambda k=key: self._keys.send(*k))
        return a

    # ------------------------------------------------------------ view actions
    def _app_view(self):
        return self.application().view

    def _toggle_session_arrangement(self):
        view = self._app_view()
        view.focus_view("Arranger" if view.is_view_visible("Session") else "Session")

    def _zoom(self, steps):
        """Horizontal zoom like Live's own ZoomComponent: right = in, left = out."""
        import Live  # only exists inside Live
        nav = Live.Application.Application.View.NavDirection
        direction = nav.right if steps > 0 else nav.left
        view = self._app_view()
        name = "Arranger" if view.is_view_visible("Arranger") else view.focused_document_view
        for _ in range(min(abs(steps), 8)):
            view.zoom_view(direction, name, False)

    def _toggle_browser(self):
        view = self._app_view()
        if view.is_view_visible("Browser"):
            view.hide_view("Browser")
        else:
            view.show_view("Browser")

    def _toggle_detail(self, sub_view):
        """Opens Detail/<sub_view>, or closes the Detail View if it already shows it."""
        view = self._app_view()
        name = "Detail/" + sub_view
        if view.is_view_visible(name):
            view.hide_view("Detail")
            return False
        view.show_view(name)
        return True

    def _toggle_clip_view(self):
        view = self._app_view()
        if not view.is_view_visible("Detail/Clip"):
            slot = self.song().view.highlighted_clip_slot
            if slot is not None and slot.has_clip:
                self.song().view.detail_clip = slot.clip
        self._toggle_detail("Clip")

    def _toggle_device_view(self):
        self._toggle_detail("DeviceChain")

    def _toggle_main_devices(self):
        song = self.song()
        view = self._app_view()
        on_main = song.view.selected_track == song.master_track
        if on_main and view.is_view_visible("Detail/DeviceChain"):
            view.hide_view("Detail")
            previous = self._track_before_master
            self._track_before_master = None
            if previous is not None and previous in self._all_tracks():
                song.view.selected_track = previous
            return
        if not on_main:
            self._track_before_master = song.view.selected_track
        song.view.selected_track = song.master_track
        view.show_view("Detail/DeviceChain")

    def _select_device_relative(self, delta):
        track = self.song().view.selected_track
        devices = list(track.devices)
        if not devices:
            return
        current = track.view.selected_device
        index = devices.index(current) + delta if current in devices else 0
        self.song().view.select_device(devices[clamp(index, 0, len(devices) - 1)])
        self._app_view().show_view("Detail/DeviceChain")

    def _all_tracks(self):
        song = self.song()
        return list(song.tracks) + list(song.return_tracks) + [song.master_track]

    # ---------------------------------------------------- selected-track actions
    def _toggle_track_activator(self):
        song = self.song()
        track = song.view.selected_track
        if track != song.master_track:  # the Main track has no Track Activator
            track.mute = not track.mute

    def _toggle_solo(self):
        song = self.song()
        track = song.view.selected_track
        if track != song.master_track:
            track.solo = not track.solo

    def _re_enable_automation(self):
        song = self.song()
        if song.re_enable_automation_enabled:
            song.re_enable_automation()

    def _toggle(self, prop):
        song = self.song()
        setattr(song, prop, not getattr(song, prop))

    def _play_toggle(self):
        song = self.song()
        if song.is_playing:
            song.stop_playing()
        else:
            song.start_playing()

    def _nudge_tempo(self, delta):
        song = self.song()
        song.tempo = clamp(song.tempo + delta, 20.0, 999.0)

    def _undo(self):
        if self.song().can_undo:
            self.song().undo()

    def _redo(self):
        if self.song().can_redo:
            self.song().redo()

    def _capture_midi(self):
        if self.song().can_capture_midi:
            self.song().capture_midi()

    def _stop_selected_track(self):
        track = self.song().view.selected_track
        if track in tuple(self.song().tracks):
            track.stop_all_clips()

    def _arm_selected_track(self):
        track = self.song().view.selected_track
        if getattr(track, "can_be_armed", False):
            track.arm = not track.arm

    def _select_track_relative(self, delta):
        song = self.song()
        tracks = list(song.visible_tracks)
        if not tracks:
            return
        current = song.view.selected_track
        index = tracks.index(current) if current in tracks else 0
        song.view.selected_track = tracks[clamp(index + delta, 0, len(tracks) - 1)]

    def _select_scene_relative(self, delta):
        song = self.song()
        scenes = list(song.scenes)
        if not scenes:
            return
        current = song.view.selected_scene
        index = scenes.index(current) if current in scenes else 0
        song.view.selected_scene = scenes[clamp(index + delta, 0, len(scenes) - 1)]

    def _launch_scene(self, index):
        song = self.song()
        scene_index = self._session.scene_offset() + index
        if 0 <= scene_index < len(song.scenes):
            song.scenes[scene_index].fire()

    def _move_ring(self, d_track, d_scene):
        song = self.song()
        max_track = max(0, len(song.visible_tracks) - 1)
        max_scene = max(0, len(song.scenes) - 1)
        track = clamp(self._session.track_offset() + d_track, 0, max_track)
        scene = clamp(self._session.scene_offset() + d_scene, 0, max_scene)
        self._session.set_offsets(track, scene)
        self._send_ring()

    # -------------------------------------------------------------- feedback
    def _send_cc(self, cc, value):
        if 0 <= cc <= 127:
            self._send_midi((0xB0 | self._channel, cc, clamp(int(value), 0, 127)))

    def _send_transport_state(self):
        song = self.song()
        values = {
            "play": song.is_playing,
            "arrangement_record": song.record_mode,
            "session_record": song.session_record,
            "overdub": song.arrangement_overdub,
            "metronome": song.metronome,
        }
        base = int(self._feedback["stateCcBase"])
        for offset, key in enumerate(STATE_ORDER):
            self._send_cc(base + offset, 127 if values[key] else 0)

    def _send_tempo(self):
        tenths = clamp(int(round(self.song().tempo * 10)), 0, 16383)
        self._send_cc(int(self._feedback["tempoMsbCc"]), tenths >> 7)
        self._send_cc(int(self._feedback["tempoLsbCc"]), tenths & 0x7F)

    def _send_signature(self):
        song = self.song()
        self._send_cc(int(self._feedback["signatureNumeratorCc"]), clamp(song.signature_numerator, 1, 127))
        self._send_cc(int(self._feedback["signatureDenominatorCc"]), clamp(song.signature_denominator, 1, 127))

    def _send_names(self, names):
        file_name, project = names
        self._send_midi(sysex_text(SYSEX_FILE_NAME, 0, to_ascii(file_name, MAX_NAME)))
        self._send_midi(sysex_text(SYSEX_PROJECT_NAME, 0, to_ascii(project, MAX_NAME)))

    def _send_help(self):
        for index, line in enumerate(help_lines(self._mapping.get("help") or [])):
            self._send_midi(sysex_text(SYSEX_HELP_LINE, index, line))

    def _read_names(self):
        try:
            with open(self._info_path, "r", encoding="utf-8") as f:
                return names_from_info(f.readline())
        except OSError:
            return None

    def update_display(self):
        super().update_display()
        self._info_ticks += 1
        if self._info_ticks < INFO_POLL_TICKS:
            return
        self._info_ticks = 0
        # The answer to the previous request; a new one is read next time.
        names = self._read_names()
        if names is not None and names != self._names:
            self._names = names
            self._send_names(names)
        self._keys.request_info(self._info_path)

    def _send_ring(self):
        self._send_cc(int(self._feedback["ringTrackCc"]), clamp(self._session.track_offset(), 0, 127))
        self._send_cc(int(self._feedback["ringSceneCc"]), clamp(self._session.scene_offset(), 0, 127))

    def _on_selected_track(self):
        self._send_selected_track()
        self._follow_selection()

    def _follow_selection(self):
        """Moves the session ring just enough to keep the selected slot inside it."""
        if not self._mapping.get("ringFollowsSelection", True):
            return
        song = self.song()
        tracks = list(song.visible_tracks)
        scenes = list(song.scenes)
        track_offset = self._session.track_offset()
        scene_offset = self._session.scene_offset()
        current_track = song.view.selected_track
        if current_track in tracks:
            index = tracks.index(current_track)
            if index < track_offset:
                track_offset = index
            elif index >= track_offset + NUM_TRACKS:
                track_offset = index - NUM_TRACKS + 1
        current_scene = song.view.selected_scene
        if current_scene in scenes:
            index = scenes.index(current_scene)
            if index < scene_offset:
                scene_offset = index
            elif index >= scene_offset + NUM_SCENES:
                scene_offset = index - NUM_SCENES + 1
        if (track_offset, scene_offset) != (self._session.track_offset(), self._session.scene_offset()):
            self._session.set_offsets(track_offset, scene_offset)
            self._send_ring()

    def _send_selected_track(self):
        song = self.song()
        tracks = list(song.visible_tracks)
        current = song.view.selected_track
        index = tracks.index(current) if current in tracks else 0
        self._send_cc(int(self._feedback["selectedTrackCc"]), clamp(index, 0, 127))

    def _send_all_state(self, force_leds=False, texts=False):
        self._send_transport_state()
        self._send_tempo()
        self._send_signature()
        self._send_ring()
        self._send_selected_track()
        if texts:
            self._send_help()
            if self._names is not None:
                self._send_names(self._names)
        if force_leds:
            for pad in self._pad_elements:
                pad.clear_send_cache()
            for scene_index in range(NUM_SCENES):
                scene = self._session.scene(scene_index)
                for track_index in range(NUM_TRACKS):
                    scene.clip_slot(track_index).update()

    # ------------------------------------------------------------- lifecycle
    def disconnect(self):
        self._send_cc(int(self._mapping["helloCc"]), 0)
        self._keys.close()
        super().disconnect()
