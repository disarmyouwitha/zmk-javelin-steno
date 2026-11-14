#!/usr/bin/env python3
"""Render the default split keymap as SVGs using only the Python standard library.

Adapted from disarmyouwitha/zmk-config-zen-plover-hid/scripts/render_keymap.py.
The split profile overrides the shared keymap's CORNE_* binding groups.
"""

from pathlib import Path
import html
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "config/corne.keymap"
PROFILE = ROOT / "config/steno-squid-split/corne.keymap"
OUTPUT = ROOT / "docs/keymap"
LAYERS = [
    (
        "main_layer",
        "qwerty",
        "Qwerty",
        "0",
        "Hold middle left thumb for Numpad · Right thumbs: Space, Javelin, PloverHID",
    ),
    (
        "sym_num_layer",
        "numpad",
        "Numpad",
        "1",
        "Release middle left thumb to return · Outer right thumb starts Practice",
    ),
    (
        "steno_layer",
        "javelin",
        "Javelin",
        "2",
        "Practice toggles at the key left of # · Outer left thumb: Qwerty · Outer right: PloverHID",
    ),
    (
        "plover_hid_layer",
        "plover-hid",
        "PloverHID",
        "3",
        "Raw strokes to host Plover · Outer left thumb: Qwerty · Outer right thumb: Javelin",
    ),
]
NAMES = {
    "TAB": "Tab",
    "ESC": "Esc",
    "LCTRL": "Ctrl",
    "LSHIFT": "Shift",
    "LCMD": "⌘",
    "SPACE": "Space",
    "BSPC": "Bksp",
    "RET": "Enter",
    "DEL": "Del →",
    "LBKT": "[",
    "RBKT": "]",
    "LBRC": "{",
    "RBRC": "}",
    "SQT": "'",
    "DQT": '"',
    "LPAR": "(",
    "RPAR": ")",
    "MINUS": "−",
    "UNDER": "_",
    "AMPS": "&",
    "STAR": "*",
    "EXCL": "!",
    "HASH": "#",
    "TILDE": "~",
    "GRAVE": "`",
    "EQUAL": "=",
    "AT": "@",
    "DLLR": "$",
    "CARET": "^",
    "BSLH": "\\",
    "FSLH": "/",
    "COMMA": ",",
    "DOT": ".",
    "SEMI": ";",
    "UP": "↑",
    "DOWN": "↓",
    "LEFT": "←",
    "RIGHT": "→",
    "KP_PLUS": "KP +",
    "MAIN": "Qwerty",
    "STENO": "Javelin",
    "PLOVER_HID": "PloverHID",
}
PLOVER = {
    "SL": "S1−",
    "SL2": "S2−",
    "TL": "T−",
    "KL": "K−",
    "PL": "P−",
    "WL": "W−",
    "HL": "H−",
    "RL": "R−",
    "A": "A",
    "O": "O",
    "E": "E",
    "U": "U",
    "ST": "*1",
    "ST2": "*2",
    "ST3": "*3",
    "ST4": "*4",
    "FR": "−F",
    "RR": "−R",
    "PR": "−P",
    "BR": "−B",
    "LR": "−L",
    "GR": "−G",
    "TR": "−T",
    "SR": "−S",
    "DR": "−D",
    "ZR": "−Z",
    "NM": "#1",
    "NM2": "#2",
    "NM3": "#3",
    "NM4": "#4",
    "NM5": "#5",
    "NM6": "#6",
    "NM7": "#7",
    "NM8": "#8",
    "NM9": "#9",
    "NMA": "#A",
    "NMB": "#B",
    "NMC": "#C",
}

JAVELIN = {
    "S1": "S1−",
    "S2": "S2−",
    "TL": "T−",
    "KL": "K−",
    "PL": "P−",
    "WL": "W−",
    "HL": "H−",
    "RL": "R−",
    "A": "A",
    "O": "O",
    "E": "E",
    "U": "U",
    "STAR1": "*1",
    "STAR2": "*2",
    "STAR3": "*3",
    "STAR4": "*4",
    "FR": "−F",
    "RR": "−R",
    "PR": "−P",
    "BR": "−B",
    "LR": "−L",
    "GR": "−G",
    "TR": "−T",
    "SR": "−S",
    "DR": "−D",
    "ZR": "−Z",
}
JAVELIN.update({f"NUM{number}": f"#{number}" for number in range(1, 13)})


def key_name(key):
    if key == "LG(LS(N4))":
        return "⌘⇧4"
    if key == "LG(LA(ESC))":
        return "⌘⌥Esc"
    if key == "LS(LEFT)":
        return "⇧←"
    if key.startswith("LG("):
        return "⌘" + key_name(key[3:-1])
    if re.fullmatch(r"N[0-9]", key):
        return key[1:]
    return NAMES.get(key, key)


def parse_layers():
    source = re.sub(r"//[^\n]*", "", SOURCE.read_text())
    profile = re.sub(r"//[^\n]*", "", PROFILE.read_text())
    macros = dict(re.findall(r"^#define (CORNE_\w+) (.+)$", profile, re.M))
    for name, value in re.findall(r"^#define (CORNE_\w+) (.+)$", source, re.M):
        macros.setdefault(name, value)
    source = re.sub(r"^\s*#.*$", "", source, flags=re.M)
    for name, value in macros.items():
        source = re.sub(r"\b" + re.escape(name) + r"\b", lambda match: value, source)
    groups = dict(
        re.findall(r"(\w+_layer)\s*\{.*?bindings\s*=\s*<(.*?)>;", source, re.S)
    )
    layers = {}
    for name, _, _, _, _ in LAYERS:
        bindings = [
            re.sub(r"\s+", " ", binding).strip()
            for binding in re.findall(r"&[^&]+", groups[name])
        ]
        if len(bindings) != 42:
            raise ValueError(f"{name}: expected 42 bindings, got {len(bindings)}")
        layers[name] = bindings
    return layers


def label(binding, index, layers):
    parts = binding.split()
    kind = parts[0]
    if kind == "&trans":
        return label(layers["main_layer"][index], index, layers)
    if kind == "&ht":
        return key_name(parts[1]), key_name(parts[2]), "dual"
    if kind == "&kp":
        return key_name(parts[1]), "", "plain"
    if kind == "&plv":
        return PLOVER[parts[1][4:]], "", "steno"
    if kind == "&javsteno":
        return JAVELIN[parts[1][6:]], "", "steno"
    if kind == "&javpractice":
        return "Practice", "toggle", "layer"
    if kind == "&to":
        return key_name(parts[1]), "tap to switch", "layer"
    if kind == "&mo" and parts[1] == "SYM_NUM":
        return "Numpad", "hold", "layer"
    if kind == "&sk":
        return "Shift", "sticky", "plain"
    if kind == "&bt":
        return (
            ("Clear", "BT bond", "plain")
            if parts[1] == "BT_CLR"
            else ("BT " + str(int(parts[2]) + 1), "profile", "plain")
        )
    if kind == "&none":
        return "—", "", "disabled"
    raise ValueError(f"Unknown binding: {binding}")


def text(x, y, content, css=""):
    return f'<text x="{x}" y="{y}" class="{css}">{html.escape(content)}</text>'


def render(layer_key, filename, title, number, caption, layers):
    svg = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="1120" height="540" viewBox="0 0 1120 540" role="img" aria-labelledby="title desc">',
        f'<title id="title">ZMK + Javelin — {html.escape(title)} layer {number}</title>',
        '<desc id="desc">42-key split layout. For dual-action keys, tap is above the divider and hold is below. Thumb keys appear at the bottom. Disabled keys have a dash.</desc>',
        """<style>
.bg{fill:#f6f7fb}.key{fill:#fff;stroke:#d5d9e4;stroke-width:1.2}.line{stroke:#e5e7ef;stroke-width:1}
text{font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;fill:#202637;font-size:18px;text-anchor:middle;dominant-baseline:middle}
.title{font-size:27px;font-weight:600;text-anchor:start}.subtitle{font-size:14px;fill:#596477;text-anchor:start}.side{font-size:12px;letter-spacing:2px;fill:#596477;text-anchor:start}
.compact{font-size:14px}.hold{fill:#5f4aad;font-size:16px}.meta{fill:#596477;font-size:11px}.disabled .key{fill:#eceef4;stroke:#e0e3eb}.disabled text{fill:#8e98aa}
.layer .key{fill:#ebe5fb;stroke:#c5b4eb}.steno .key{fill:#edf5fb;stroke:#c4d9ea}.footer{font-size:14px;fill:#596477;text-anchor:start}
@media(prefers-color-scheme:dark){.bg{fill:#151922}.key{fill:#222936;stroke:#414b5e}.line{stroke:#414b5e}text{fill:#ecf0f7}.subtitle,.side,.meta,.footer{fill:#b3bdcd}.compact{font-size:14px}.hold{fill:#c2aaff}.disabled .key{fill:#1b202b;stroke:#303746}.disabled text{fill:#77849a}.layer .key{fill:#35294e;stroke:#71578e}.steno .key{fill:#203448;stroke:#3f617c}}
</style>""",
        '<rect class="bg" width="1120" height="540" rx="16"/>',
        text(32, 37, title + "  /  " + number, "title"),
        text(32, 69, "STENO SQUID / CORNE SPLIT     ·     DEFAULT KEYMAP", "subtitle"),
        text(735, 37, "Dual-action keys: tap above · hold below", "subtitle"),
        text(735, 64, "⌘ Command     ⌥ Option     ⇧ Shift", "subtitle"),
    ]
    for side in range(2):
        origin = 32 if side == 0 else 611
        svg.append(
            text(origin, 105, "LEFT HALF" if side == 0 else "RIGHT HALF", "side")
        )
        stagger = [18, 18, 8, 0, 8, 18] if side == 0 else [18, 8, 0, 8, 18, 18]
        for local in range(21):
            if local < 18:
                row, col = divmod(local, 6)
                idx = row * 12 + side * 6 + col
                x = origin + col * 81
                y = 128 + row * 88 + stagger[col]
            else:
                thumb = local - 18
                idx = 36 + side * 3 + thumb
                x = origin + ((3 + thumb) if side == 0 else thumb) * 81
                y = 412
            tap, hold, kind = label(layers[layer_key][idx], idx, layers)
            key_css = "compact" if len(tap) >= 7 else ""
            svg.append(
                f'<g class="{kind}" data-key-index="{idx}"><title>{html.escape(tap + (" / " + hold if hold else ""))}</title><rect class="key" x="{x}" y="{y}" width="72" height="76" rx="8"/>'
            )
            if hold:
                svg.append(text(x + 36, y + 25, tap, key_css))
                if kind == "dual":
                    svg.append(
                        f'<line class="line" x1="{x+12}" x2="{x+60}" y1="{y+40}" y2="{y+40}"/>'
                    )
                svg.append(
                    text(x + 36, y + 57, hold, "hold" if kind == "dual" else "meta")
                )
            else:
                svg.append(text(x + 36, y + 38, tap, key_css))
            svg.append("</g>")
    svg += [text(32, 516, caption, "footer"), "</svg>"]
    content = "\n".join(svg) + "\n"
    digest = hashlib.sha256(content.encode()).hexdigest()[:12]
    target = OUTPUT / f"{filename}-{digest}.svg"
    target.write_text(content)
    readme = ROOT / "README.md"
    pattern = r"docs/keymap/" + re.escape(filename) + r"(?:-[0-9a-f]{12})?\.svg"
    readme.write_text(
        re.sub(pattern, target.relative_to(ROOT).as_posix(), readme.read_text())
    )
    for previous in OUTPUT.glob(f"{filename}-*.svg"):
        if previous != target:
            previous.unlink()
    print(target.relative_to(ROOT))


if __name__ == "__main__":
    OUTPUT.mkdir(parents=True, exist_ok=True)
    layers = parse_layers()
    for args in LAYERS:
        render(*args, layers)
