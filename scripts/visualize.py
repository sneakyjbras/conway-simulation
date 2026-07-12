#!/usr/bin/env python3
"""visualize.py — turn a life3d frame-log into a self-contained HTML5 scrubber.

The C++ simulation writes a binary frame-log when run with --export-frames PATH
(one central z-slice per generation).  This script reads that log and emits a
single standalone .html file: open it in any browser to scrub through the
generations with a slider, play/pause, step, and speed controls.  No web server
and no external files are needed — the frame data is embedded directly in the
page, so it works over SSH (copy the one .html down and open it locally).

Usage:
    python3 scripts/visualize.py --input frames.bin [--out view.html]
                                 [--cell PX] [--fps N]

Frame-log format (little-endian), as written by src/frame_exporter.cpp:
    char     magic[4] = "LIFE"
    uint32   version  = 1
    uint32   N
    uint32   num_frames
    then num_frames records: uint32 gen + N*N bytes (row-major, species 0..9).

Only the standard library is required to produce the HTML.  numpy is used when
available to decode frames a little faster, but the script falls back to pure
Python if it is missing.
"""

from __future__ import annotations

import argparse
import base64
import struct
import sys
from pathlib import Path

MAGIC = b"LIFE"
SUPPORTED_VERSION = 1

# Distinct, colour-blind-friendly-ish palette for species 1..9.  Index 0 is
# dead (rendered as the dark background).  Kept in sync with the JS below.
PALETTE = [
    "#10131a",  # 0 dead (background)
    "#4f9dff",  # 1 blue
    "#ff5d5d",  # 2 red
    "#37d67a",  # 3 green
    "#ffce4f",  # 4 amber
    "#b56dff",  # 5 purple
    "#ff9d3f",  # 6 orange
    "#3fd9d2",  # 7 teal
    "#ff6db5",  # 8 pink
    "#c0d050",  # 9 lime
]


def read_frame_log(path: Path) -> tuple[int, list[tuple[int, bytes]]]:
    """Parse the binary frame-log. Returns (N, [(gen, slice_bytes), ...])."""
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError("file too small to contain a header")

    magic = data[:4]
    if magic != MAGIC:
        raise ValueError(f"bad magic {magic!r} (expected {MAGIC!r}); not a life3d frame-log")

    version, n, num_frames = struct.unpack_from("<III", data, 4)
    if version != SUPPORTED_VERSION:
        raise ValueError(f"unsupported version {version} (this script handles {SUPPORTED_VERSION})")
    if n == 0:
        raise ValueError("header reports N=0")

    frame_bytes = n * n
    record = 4 + frame_bytes
    expected = 16 + num_frames * record
    if len(data) != expected:
        raise ValueError(
            f"size mismatch: header implies {expected} bytes "
            f"(N={n}, num_frames={num_frames}) but file is {len(data)}"
        )

    frames: list[tuple[int, bytes]] = []
    off = 16
    for _ in range(num_frames):
        (gen,) = struct.unpack_from("<I", data, off)
        off += 4
        frames.append((gen, data[off : off + frame_bytes]))
        off += frame_bytes
    return n, frames


def build_blob(n: int, frames: list[tuple[int, bytes]]) -> tuple[str, list[int]]:
    """Concatenate all slices into one base64 blob; return (b64, [gens])."""
    buf = bytearray()
    gens: list[int] = []
    for gen, slice_bytes in frames:
        gens.append(gen)
        buf.extend(slice_bytes)
    return base64.b64encode(bytes(buf)).decode("ascii"), gens


def render_html(n: int, frames: list[tuple[int, bytes]], cell_px: int, fps: int) -> str:
    blob_b64, gens = build_blob(n, frames)
    num_frames = len(frames)
    palette_js = ",".join(f'"{c}"' for c in PALETTE)
    gens_js = ",".join(str(g) for g in gens)

    # The HTML is intentionally dependency-free vanilla JS.  All frame data is
    # embedded as one base64 blob decoded once into a Uint8Array; each frame is
    # an N*N window into it.  Rendering is a simple fillRect per cell — fine for
    # the slice sizes this produces, and trivially portable.
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>life3d — generation scrubber (N={n})</title>
<style>
  :root {{ color-scheme: dark; }}
  body {{
    margin: 0; background: #0b0d12; color: #d7dce5;
    font: 14px/1.5 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    display: flex; flex-direction: column; align-items: center;
    gap: 14px; padding: 22px;
  }}
  h1 {{ font-size: 15px; font-weight: 600; margin: 0; color: #aeb6c2; letter-spacing: .02em; }}
  canvas {{
    background: {PALETTE[0]}; border: 1px solid #232838; border-radius: 6px;
    image-rendering: pixelated; box-shadow: 0 6px 24px rgba(0,0,0,.4);
  }}
  .controls {{ display: flex; align-items: center; gap: 10px; flex-wrap: wrap; justify-content: center; }}
  button {{
    background: #1a1f2b; color: #d7dce5; border: 1px solid #2c3344;
    border-radius: 5px; padding: 6px 12px; cursor: pointer; font: inherit;
  }}
  button:hover {{ background: #232a39; }}
  button:active {{ transform: translateY(1px); }}
  input[type=range] {{ width: min(60vw, 460px); accent-color: #4f9dff; }}
  .readout {{ min-width: 150px; text-align: center; color: #aeb6c2; }}
  .legend {{ display: flex; gap: 10px; flex-wrap: wrap; justify-content: center; font-size: 12px; color: #8b93a3; }}
  .chip {{ display: inline-flex; align-items: center; gap: 5px; }}
  .sw {{ width: 11px; height: 11px; border-radius: 2px; display: inline-block; }}
  label.speed {{ color: #8b93a3; display: inline-flex; align-items: center; gap: 6px; }}
</style>
</head>
<body>
  <h1>life3d &middot; central z-slice &middot; N={n} &middot; {num_frames} generations</h1>
  <canvas id="view" width="{n * cell_px}" height="{n * cell_px}"></canvas>

  <div class="controls">
    <button id="first" title="First (Home)">⏮</button>
    <button id="prev"  title="Step back (←)">◀</button>
    <button id="play"  title="Play / pause (Space)">▶ Play</button>
    <button id="next"  title="Step forward (→)">▶</button>
    <button id="last"  title="Last (End)">⏭</button>
    <input id="scrub" type="range" min="0" max="{num_frames - 1}" value="0" step="1">
    <span class="readout" id="readout">gen 0 / {gens[-1] if gens else 0}</span>
  </div>

  <div class="controls">
    <label class="speed">speed
      <input id="fps" type="range" min="1" max="60" value="{fps}" step="1">
    </label>
    <span class="readout" id="fpsout">{fps} fps</span>
  </div>

  <div class="legend" id="legend"></div>

<script>
(function () {{
  const N = {n};
  const CELL = {cell_px};
  const NUM = {num_frames};
  const GENS = [{gens_js}];
  const PALETTE = [{palette_js}];
  const FRAME = N * N;

  // Decode the embedded base64 blob once into a flat Uint8Array.
  const RAW = atob("{blob_b64}");
  const CELLS = new Uint8Array(RAW.length);
  for (let i = 0; i < RAW.length; i++) CELLS[i] = RAW.charCodeAt(i);

  const canvas = document.getElementById("view");
  const ctx = canvas.getContext("2d");
  const scrub = document.getElementById("scrub");
  const readout = document.getElementById("readout");
  const fps = document.getElementById("fps");
  const fpsout = document.getElementById("fpsout");
  const playBtn = document.getElementById("play");

  let frame = 0, playing = false, timer = null;

  function draw(f) {{
    const base = f * FRAME;
    for (let y = 0; y < N; y++) {{
      for (let x = 0; x < N; x++) {{
        const v = CELLS[base + y * N + x];
        ctx.fillStyle = PALETTE[v] || PALETTE[0];
        ctx.fillRect(x * CELL, y * CELL, CELL, CELL);
      }}
    }}
    readout.textContent = "gen " + GENS[f] + " / " + GENS[GENS.length - 1];
  }}

  function show(f) {{
    frame = (f + NUM) % NUM;
    scrub.value = frame;
    draw(frame);
  }}

  function stop() {{ playing = false; playBtn.textContent = "▶ Play"; if (timer) {{ clearInterval(timer); timer = null; }} }}
  function start() {{
    if (playing) return;
    playing = true; playBtn.textContent = "⏸ Pause";
    const period = 1000 / Number(fps.value);
    timer = setInterval(() => {{
      if (frame >= NUM - 1) {{ show(0); }} else {{ show(frame + 1); }}
    }}, period);
  }}
  function restartIfPlaying() {{ if (playing) {{ stop(); start(); }} }}

  document.getElementById("first").onclick = () => {{ stop(); show(0); }};
  document.getElementById("last").onclick  = () => {{ stop(); show(NUM - 1); }};
  document.getElementById("prev").onclick  = () => {{ stop(); show(frame - 1); }};
  document.getElementById("next").onclick  = () => {{ stop(); show(frame + 1); }};
  playBtn.onclick = () => {{ playing ? stop() : start(); }};
  scrub.oninput = (e) => {{ stop(); show(Number(e.target.value)); }};
  fps.oninput = () => {{ fpsout.textContent = fps.value + " fps"; restartIfPlaying(); }};

  document.addEventListener("keydown", (e) => {{
    if (e.key === " ") {{ e.preventDefault(); playing ? stop() : start(); }}
    else if (e.key === "ArrowLeft")  {{ stop(); show(frame - 1); }}
    else if (e.key === "ArrowRight") {{ stop(); show(frame + 1); }}
    else if (e.key === "Home") {{ stop(); show(0); }}
    else if (e.key === "End")  {{ stop(); show(NUM - 1); }}
  }});

  // Legend (only species that actually occur anywhere).
  const seen = new Set();
  for (let i = 0; i < CELLS.length; i++) if (CELLS[i]) seen.add(CELLS[i]);
  const legend = document.getElementById("legend");
  [...seen].sort((a, b) => a - b).forEach((s) => {{
    const chip = document.createElement("span");
    chip.className = "chip";
    chip.innerHTML = '<span class="sw" style="background:' + PALETTE[s] + '"></span>species ' + s;
    legend.appendChild(chip);
  }});

  show(0);
}})();
</script>
</body>
</html>
"""


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Render a life3d frame-log as a self-contained HTML scrubber.")
    ap.add_argument("--input", "-i", required=True, type=Path, help="frame-log written by --export-frames")
    ap.add_argument("--out", "-o", type=Path, default=None, help="output .html (default: <input>.html)")
    ap.add_argument("--cell", type=int, default=10, help="pixels per cell (default 10)")
    ap.add_argument("--fps", type=int, default=10, help="initial playback fps (default 10)")
    args = ap.parse_args(argv)

    if not args.input.exists():
        print(f"error: input not found: {args.input}", file=sys.stderr)
        return 1

    try:
        n, frames = read_frame_log(args.input)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not frames:
        print("error: frame-log contains no frames", file=sys.stderr)
        return 1

    out = args.out or args.input.with_suffix(".html")
    cell = max(1, args.cell)
    fps = min(60, max(1, args.fps))

    html = render_html(n, frames, cell, fps)
    out.write_text(html, encoding="utf-8")

    size_kb = out.stat().st_size / 1024
    print(f"wrote {out} ({len(frames)} frames, N={n}, {size_kb:.0f} KB) — open it in a browser")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
