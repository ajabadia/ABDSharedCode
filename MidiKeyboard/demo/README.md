# ABDKeyboard Demo

## Quick Start

### macOS
```bash
open demo/index.html
```

### Windows
```bash
start demo/index.html
```

### Linux
```bash
xdg-open demo/index.html
```

---

## Recording a GIF

### Option A: Automated capture (requires Node.js + Chrome)

```bash
# 1. Capture frames (headless Chrome)
node demo/capture.mjs

# 2. Assemble into GIF with ffmpeg
ffmpeg -framerate 6 -i demo/frames/frame-%03d.png \
  -vf "scale=960:-1:flags=lanczos,split[s0][s1];[s0]palettegen=max_colors=128:stats_mode=diff[p];[s1][p]paletteuse=dither=sierra2_4a" \
  -loop 0 demo/keyboard-demo.gif
```

Output: `demo/keyboard-demo.gif` (~224KB, 16 frames, 6fps)

### Option B: Screen recording tools

| Tool | OS | Steps |
|---|---|---|
| **LICEcap** | Win/Mac | Open → select area → Record → name file `.gif` |
| **ScreenToGif** | Win | Record → edit frames → File → Save As → GIF |
| **Gifox** | Mac | Select region → Record → Export |
| **Kap** | Mac | Select area → Record → Export as GIF |

### Option C: ffmpeg from live recording

```bash
# Record 10 seconds of screen region (x,y,w,h)
ffmpeg -f gdigrab -framerate 30 -t 10 -i desktop -vf "crop=960:520:100:200" output.mkv

# Convert to optimized GIF
ffmpeg -i output.mkv -vf "fps=8,scale=960:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" demo/keyboard-demo.gif
```

---

## Suggested GIF Sequences

### Quick Tour (5 seconds)
1. Idle keyboard (1s)
2. Play C major chord (1s)
3. Aftertouch drag (1s)
4. Panic flash (1s)
5. Return to idle (1s)

### Feature Showcase (8 seconds)
1. Velocity demo: low → high → medium (2s)
2. Aftertouch: click + drag down (1.5s)
3. Sustain: toggle ON → play notes → release keys (2s)
4. QWERTY scale: Z-M chromatic run (1s)
5. Panic: Ctrl+Q red flash (1.5s)

### Developer Docs GIF
Capture just the keyboard area at 2x scale for README embedding.
