"""Ableton Live control surface script for the Polyend Tracker DAW Control screen.

The Tracker firmware only sends raw button / pad / jog messages and shows what
it receives. Everything else (what each button does, Shift combinations, LED
values) is decided here and configured in mapping.json next to this file.
"""

import json
import os

from _Framework.ButtonElement import ButtonElement
from _Framework.ButtonMatrixElement import ButtonMatrixElement
from _Framework.ControlSurface import ControlSurface
from _Framework.InputControlElement import MIDI_CC_TYPE, MIDI_NOTE_TYPE
from _Framework.SessionComponent import SessionComponent

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
    "jog": {"default": "scene_select", "shift": "tempo"},
}


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


class PolyendTrackerDAW(ControlSurface):

    def __init__(self, c_instance):
        super().__init__(c_instance)
        here = os.path.dirname(os.path.abspath(__file__))
        self._mapping = load_mapping(os.path.join(here, "mapping.json"), self.log_message)
        m = self._mapping
        self._channel = clamp(int(m["channel"]), 1, 16) - 1  # 0-based for the framework
        self._feedback = m["feedback"]
        self._shift = False
        self._actions = self._build_actions()
        self._button_actions = self._resolve_buttons(m.get("buttons", {}))

        with self.component_guard():
            self._setup_controls()
            self._setup_session()
            self._setup_listeners()

        self.log_message("PolyendTrackerDAW loaded (channel %d)" % (self._channel + 1))
        self.show_message("Polyend Tracker DAW Control connected")
        self._send_cc(int(m["helloCc"]), 127)
        self._send_all_state()

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
        self.register_slot(song.view, self._send_selected_track, "selected_track")

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
        elif action_name == "scene_select":
            self._select_scene_relative(delta)
        elif action_name == "track_select":
            self._select_track_relative(delta)

    def _on_hello(self, value):
        if value > 0:
            # The Tracker opened its DAW Control screen: send everything again.
            self._send_all_state(force_leds=True)
        else:
            self._shift = False

    def _run_action(self, name):
        if not name:
            return
        action = self._actions.get(name)
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
        }
        for i in range(NUM_SCENES):
            a["scene_launch_%d" % (i + 1)] = (lambda index=i: self._launch_scene(index))
        return a

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

    def _send_ring(self):
        self._send_cc(int(self._feedback["ringTrackCc"]), clamp(self._session.track_offset(), 0, 127))
        self._send_cc(int(self._feedback["ringSceneCc"]), clamp(self._session.scene_offset(), 0, 127))

    def _send_selected_track(self):
        song = self.song()
        tracks = list(song.visible_tracks)
        current = song.view.selected_track
        index = tracks.index(current) if current in tracks else 0
        self._send_cc(int(self._feedback["selectedTrackCc"]), clamp(index, 0, 127))

    def _send_all_state(self, force_leds=False):
        self._send_transport_state()
        self._send_tempo()
        self._send_ring()
        self._send_selected_track()
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
        super().disconnect()
