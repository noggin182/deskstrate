#!/usr/bin/env python3
"""Writes the VIA definitions for the Deskstrate Q3 Max firmware.

Keychron doesn't publish its Q3 Max (wireless) definitions where we can fetch them,
so the layout is generated from QMK's keyboard.json and the custom keys are listed
here: Keychron's own first (QK_KB_0..15, in keychron_common.h order, matching the
Launcher's own Q3 Max definition), then ours in keymap/deskstrate_keys.h order. The
lighting menu is copied from the Launcher's definition
(launcher.keychron.com/static/device/875825201/json/v3.json). Run after build.sh has fetched Keychron's QMK:

    python3 firmware/keyboard/via/make_via_json.py

Load the result in usevia.app: Settings > Show Design tab, then Design > Load.
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
QMK_BOARD = HERE.parent / ".qmk" / "keyboards" / "keychron" / "q3_max"

VARIANTS = {"iso_encoder": "ISO", "ansi_encoder": "ANSI"}
ENCODER_KEY = [0, 13]  # the knob's press switch

KEYCHRON_KEYS = [
    ("Left Option", "LOpt"), ("Right Option", "ROpt"), ("Left Cmd", "LCmd"),
    ("Right Cmd", "RCmd"), ("Mission Control", "MCtrl"), ("Launchpad", "LPad"),
    ("Task View", "Task"), ("File Explorer", "File"), ("Screenshot", "SShot"),
    ("Cortana", "Cortana"), ("Siri", "Siri"), ("Bluetooth host 1", "BT1"),
    ("Bluetooth host 2", "BT2"), ("Bluetooth host 3", "BT3"), ("2.4 GHz", "2.4G"),
    ("Battery level", "Bat"),
]

# Keychron's Launcher definition draws ISO/ANSI right shift 2.75u; QMK's keyboard.json
# says 2.25u. Keychron's matches the board.
RIGHT_SHIFT = [4, 13]

LIGHTING_MENU = [
    {
        "label": "Lighting",
        "content": [
            {
                "label": "Backlight",
                "content": [
                    {
                        "label": "Brightness",
                        "type": "range",
                        "options": [
                            0,
                            255
                        ],
                        "content": [
                            "id_qmk_rgb_matrix_brightness",
                            3,
                            1
                        ]
                    },
                    {
                        "label": "Effect",
                        "type": "dropdown",
                        "content": [
                            "id_qmk_rgb_matrix_effect",
                            3,
                            2
                        ],
                        "options": [
                            [
                                "None",
                                0
                            ],
                            [
                                "Solid Color",
                                1
                            ],
                            [
                                "Breathing",
                                2
                            ],
                            [
                                "Band Spiral Val",
                                3
                            ],
                            [
                                "Cycle All",
                                4
                            ],
                            [
                                "Cycle Left Right",
                                5
                            ],
                            [
                                "Cycle Up Down",
                                6
                            ],
                            [
                                "Rainbow Moving Chevron",
                                7
                            ],
                            [
                                "Cycle Out In",
                                8
                            ],
                            [
                                "Cycle Out In Dual",
                                9
                            ],
                            [
                                "Cycle Pinwheel",
                                10
                            ],
                            [
                                "Cycle Spiral",
                                11
                            ],
                            [
                                "Dual Beacon",
                                12
                            ],
                            [
                                "Rainbow Beacon",
                                13
                            ],
                            [
                                "Jellybean Raindrops",
                                14
                            ],
                            [
                                "Pixel Rain",
                                15
                            ],
                            [
                                "Typing Heatmap",
                                16
                            ],
                            [
                                "Digital Rain",
                                17
                            ],
                            [
                                "Reactive Simple",
                                18
                            ],
                            [
                                "Reactive Multiwide",
                                19
                            ],
                            [
                                "Reactive Multinexus",
                                20
                            ],
                            [
                                "Splash",
                                21
                            ],
                            [
                                "Solid Splash",
                                22
                            ]
                        ]
                    },
                    {
                        "showIf": "{id_qmk_rgb_matrix_effect} > 1",
                        "label": "Effect Speed",
                        "type": "range",
                        "options": [
                            0,
                            255
                        ],
                        "content": [
                            "id_qmk_rgb_matrix_effect_speed",
                            3,
                            3
                        ]
                    },
                    {
                        "showIf": "{id_qmk_rgb_matrix_effect} != 0 && ( {id_qmk_rgb_matrix_effect} < 4 || {id_qmk_rgb_matrix_effect} == 18 || ({id_qmk_rgb_matrix_effect} > 17 && {id_qmk_rgb_matrix_effect} != 21) ) ",
                        "label": "Color",
                        "type": "color",
                        "content": [
                            "id_qmk_rgb_matrix_color",
                            3,
                            4
                        ]
                    }
                ]
            }
        ]
    }
]

DESKSTRATE_KEYS = [
    ("KVM: everything to PC", "KVM PC"),
    ("KVM: everything to Mac", "KVM Mac"),
    ("KVM: left monitor to PC", "L PC"),
    ("KVM: left monitor to Mac", "L Mac"),
    ("KVM: left monitor to guest", "L Gst"),
    ("KVM: right monitor to PC", "R PC"),
    ("KVM: right monitor to Mac", "R Mac"),
    ("KVM: right monitor to guest", "R Gst"),
    ("KVM: keyboard/mouse to PC", "HID PC"),
    ("KVM: keyboard/mouse to Mac", "HID Mac"),
    ("KVM: media hub to PC", "Med PC"),
    ("KVM: media hub to Mac", "Med Mac"),
]


def kle_rows(layout):
    """QMK layout entries -> VIA's KLE keymap rows."""
    rows = {}
    for key in layout:
        rows.setdefault(key["y"], []).append(key)
    out = []
    prev_y = -1.0
    for y in sorted(rows):
        row = []
        x = 0.0
        props = {}
        if y - prev_y - 1 > 1e-6:
            props["y"] = round(y - prev_y - 1, 4)
        for key in sorted(rows[y], key=lambda k: k["x"]):
            w, h = key.get("w", 1), key.get("h", 1)
            if key["matrix"] == RIGHT_SHIFT:
                w = 2.75
            if key["x"] - x > 1e-6:
                props["x"] = round(key["x"] - x, 4)
            if w != 1:
                props["w"] = w
            if h != 1:
                props["h"] = h
            if w == 1.25 and h == 2:  # ISO Enter
                props.update({"w2": 1.5, "h2": 1, "x2": -0.25})
            if props:
                row.append(props)
            props = {}
            label = "{},{}".format(*key["matrix"])
            if key["matrix"] == ENCODER_KEY:
                label += "\n" * 9 + "e0"
            row.append(label)
            x = key["x"] + w
        out.append(row)
        prev_y = y
    return out


def main():
    for variant, name in VARIANTS.items():
        board = json.loads((QMK_BOARD / variant / "keyboard.json").read_text())
        info = json.loads((QMK_BOARD / "info.json").read_text())
        (layout_name, layout), = board["layouts"].items()
        rows = 1 + max(k["matrix"][0] for k in layout["layout"])
        cols = len(info["matrix_pins"]["cols"])
        definition = {
            "name": f"Keychron Q3 Max {name} Knob (Deskstrate)",
            "vendorId": info["usb"]["vid"],
            "productId": board["usb"]["pid"],
            "matrix": {"rows": rows, "cols": cols},
            "keycodes": ["qmk_lighting"],
            "menus": LIGHTING_MENU,
            "customKeycodes": [
                {"name": n, "title": n, "shortName": s}
                for n, s in KEYCHRON_KEYS + DESKSTRATE_KEYS
            ],
            "layouts": {"keymap": kle_rows(layout["layout"])},
        }
        path = HERE / f"q3_max_{variant}_deskstrate.json"
        path.write_text(json.dumps(definition, indent=2) + "\n")
        print(f"{path.name}: {layout_name}, {len(layout['layout'])} keys")


if __name__ == "__main__":
    main()
