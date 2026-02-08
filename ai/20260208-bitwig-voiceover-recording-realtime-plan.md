# Bitwig -> Fiction Fully Automated Voiceover Pipeline

Date: 2026-02-08
Project: /Users/pfeodrippe/dev/something

## Hard Requirement
You want zero-touch ingest:
1. Record in Bitwig.
2. Save/finish take.
3. Audio appears in `fiction` game resources automatically.
4. Next line playback in `fiction` uses the new file immediately.

This plan is now aligned to that requirement only.

## What We Implemented In Repo

### 1) Automation daemon script (new)
- File: `/Users/pfeodrippe/dev/something/bin/voice_auto_publish.py`
- Watches a Bitwig output folder continuously.
- Waits for file to finish writing.
- Resolves a `line_id` (from filename or queue file).
- Atomically writes canonical game file to:
  - `resources/fiction/voice/<locale>/<line_id>.wav`
- Optional ffmpeg transcode to canonical format.
- JSONL logging to `resources/fiction/voice/voice_pipeline.log`.

### 2) ID stability upgrade in parser (new)
- File: `/Users/pfeodrippe/dev/something/src/fiction/parser.jank`
- Added optional inline IDs in story lines: `[id:SC01_L0042]`.
- Replaced random `gensym` IDs with deterministic parse-order IDs when explicit ID is absent.

Why this matters: full automation needs deterministic mapping between script lines and VO files.

## The Fully Automated Flow

## A) Best "no-click export" mode (recommended)
Use Bitwig **Master Recording** output as the watcher source:
- Bitwig writes completed takes into project `master-recordings` folder.
- Our watcher ingests each new file and publishes directly into game resources.

Run watcher:

```bash
python3 /Users/pfeodrippe/dev/something/bin/voice_auto_publish.py \
  --source-dir "/ABS/PATH/TO/BITWIG_PROJECT/master-recordings" \
  --target-root "/Users/pfeodrippe/dev/something/resources/fiction/voice" \
  --locale fr \
  --story-file "/Users/pfeodrippe/dev/something/stories/la_voiture.md" \
  --ffmpeg \
  --sample-rate 48000 \
  --channels 1 \
  --codec pcm_s24le
```

Story-ID behavior:
- Resolved line IDs are validated against `[id:...]` entries in the story.
- This guarantees ingest only publishes IDs that actually exist in script content.

## B) Export-folder mode (if you want processed renders)
If you prefer edited/comped/exported files from Bitwig:
- Set Bitwig export destination to `_incoming` folder.
- Watch that folder instead.

```bash
python3 /Users/pfeodrippe/dev/something/bin/voice_auto_publish.py \
  --source-dir "/Users/pfeodrippe/dev/something/resources/fiction/voice/_incoming/fr" \
  --target-root "/Users/pfeodrippe/dev/something/resources/fiction/voice" \
  --locale fr \
  --line-id-regex "^([A-Za-z0-9_-]+)" \
  --ffmpeg
```

In this mode, filename stem should start with line ID (or just be exactly line ID).

## Why This Is Truly Automatic For `fiction`

- `fiction.voiceover` resolves file paths on playback attempts.
- After atomic file replacement, next time that line is spoken, the latest audio is used.
- No manual copy/rename step remains.

Current runtime path logic:
- Locale line file
- Locale speaker fallback
- Fallback-locale line file
- Fallback-locale speaker file
- Mock default

## Bitwig Side: Fast Control Surface Setup

Practical setup:
- Bind transport recording and any needed actions to keyboard/MIDI shortcuts.
- Use one shortcut/button flow for take capture.
- Let watcher handle publish step.

Bitwig documents shortcut customization and states shortcuts can control almost anything, which is enough to build a one-button operator workflow with external macro tools if needed. [B7]

## Story Authoring Convention (for robust automation)

Use explicit IDs in script content:

```text
#∆V [id:SC01_L0042] Une voiture avec ses feux rouges...
:: [id:SC01_C0003] Se retourner.
```

Then your voice files are deterministic:

```text
resources/fiction/voice/fr/SC01_L0042.wav
```

## Real-Time Notes

### Native (`make fiction`)
- Works for live iteration.
- Re-triggering a line plays latest file immediately.

### WASM (`make fiction-wasm`)
- Current build embeds `resources/fiction` at build time (`--embed-file`), so live external replacements do not hot-update running WASM build.
- Use native for recording sessions; rebuild WASM snapshots when needed.

## Minimal Operator Routine (Daily Use)

1. Start `make fiction`.
2. Start `voice_auto_publish.py` watcher.
3. Record takes in Bitwig.
4. Stop/save take.
5. Watcher logs `publish_ok`.
6. Re-trigger line in game and audition instantly.

## Optional Next Upgrade (if you want even tighter loop)

- Add a debug hotkey in `fiction` to replay last spoken line instantly after each publish.
- This removes navigation friction during recording sessions.

## References

### Bitwig
- [B1] Recording Clips: https://www.bitwig.com/userguide/latest/recording_clips/
- [B2] Recording Launcher Clips: https://www.bitwig.com/userguide/latest/recording_launcher_clips/
- [B3] Project panel and project organization: https://www.bitwig.com/userguide/latest/the_project_panel
- [B4] Exporting Audio: https://www.bitwig.com/userguide/latest/exporting_audio/
- [B5] Working with Projects and Exporting: https://www.bitwig.com/userguide/latest/working_with_projects_and_exporting
- [B6] Windows recording note (WASAPI): https://www.bitwig.com/support/technical_support/how-do-i-record-audio-using-wasapi-on-windows-36/
- [B7] Keyboard shortcuts customization: https://www.bitwig.com/support/technical_support/how-do-i-create-a-keyboard-shortcut-for-a-certain-function-or-command-16/

### Dialogue IDs / VO mapping patterns
- [D1] Yarn Spinner line tags/metadata: https://docs.yarnspinner.dev/2.5/getting-started/writing-in-yarn/tags-metadata
- [D2] Ren'Py voice by dialogue ID: https://www.renpy.org/doc/html/voice.html

### Miniaudio
- [M1] Miniaudio engine manual: https://miniaud.io/docs/manual/index.html

### Watcher reliability / atomic publish
- [R1] inotifywait events (`CLOSE_WRITE`, `MOVED_TO`): https://man7.org/linux/man-pages/man1/inotifywait.1.html
- [R2] Python `os.replace` atomic replacement: https://docs.python.org/3/library/os.html#os.replace
- [R3] Watchdog quickstart: https://python-watchdog.readthedocs.io/en/stable/quickstart.html

### WASM file packaging constraint
- [W1] Emscripten packaging (`--embed-file`/`--preload-file`): https://emscripten.org/docs/porting/files/packaging_files.html
