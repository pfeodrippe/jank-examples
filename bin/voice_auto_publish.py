#!/usr/bin/env python3
"""Automated Bitwig -> fiction voice publish pipeline.

This watcher ingests newly written audio files and publishes canonical game VO
files into resources/fiction/voice/<locale>/<line_id>.wav.

Typical use:
  1) Bitwig writes files to an incoming folder (export folder or
     <project>/master-recordings).
  2) This script watches that folder.
  3) On new stable file, it resolves line_id, optional-transcodes with ffmpeg,
     and atomically replaces the destination file in the game resources.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Optional, Tuple


STOP = False


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Watch Bitwig outputs and publish fiction VO files."
    )
    parser.add_argument(
        "--source-dir",
        required=True,
        help="Folder to watch (e.g. Bitwig export folder or master-recordings).",
    )
    parser.add_argument(
        "--target-root",
        default="resources/fiction/voice",
        help="Voice root folder in the repo (default: resources/fiction/voice).",
    )
    parser.add_argument(
        "--locale",
        default="fr",
        help="Locale folder name under target root (default: fr).",
    )
    parser.add_argument(
        "--line-id-regex",
        default="",
        help=(
            "Optional regex to extract line id from source filename stem. "
            "If empty, full stem is used."
        ),
    )
    parser.add_argument(
        "--queue-file",
        default="",
        help=(
            "Optional queue file with one line_id per line; if set, each new file "
            "consumes the next id from queue."
        ),
    )
    parser.add_argument(
        "--story-file",
        default="",
        help=(
            "Optional story markdown file. When set, resolved line IDs must exist "
            "as [id:...] entries in that story."
        ),
    )
    parser.add_argument(
        "--extensions",
        default=".wav,.aif,.aiff,.flac,.ogg,.mp3,.m4a",
        help="Comma-separated file extensions to ingest (default includes common audio).",
    )
    parser.add_argument(
        "--scan-interval",
        type=float,
        default=0.5,
        help="Polling interval in seconds (default: 0.5).",
    )
    parser.add_argument(
        "--settle-polls",
        type=int,
        default=2,
        help=(
            "File must remain unchanged for this many scan cycles before ingest "
            "(default: 2)."
        ),
    )
    parser.add_argument(
        "--log-file",
        default="resources/fiction/voice/voice_pipeline.log",
        help="JSONL log file path.",
    )
    parser.add_argument(
        "--archive-dir",
        default="",
        help="Optional folder to move processed source files into.",
    )
    parser.add_argument(
        "--ffmpeg-bin",
        default="ffmpeg",
        help="ffmpeg executable name/path (default: ffmpeg).",
    )
    parser.add_argument(
        "--ffmpeg",
        action="store_true",
        help="Enable ffmpeg transcode to canonical WAV format before publish.",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=48000,
        help="Target sample rate when --ffmpeg is set (default: 48000).",
    )
    parser.add_argument(
        "--channels",
        type=int,
        default=1,
        help="Target channel count when --ffmpeg is set (default: 1 / mono).",
    )
    parser.add_argument(
        "--codec",
        default="pcm_s24le",
        help="Target ffmpeg codec when --ffmpeg is set (default: pcm_s24le).",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Scan/process once, then exit.",
    )
    return parser.parse_args()


def emit_log(log_path: Path, level: str, event: str, **payload: object) -> None:
    entry = {
        "ts": now_utc(),
        "level": level,
        "event": event,
        **payload,
    }
    line = json.dumps(entry, ensure_ascii=True)
    print(line)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def sanitize_line_id(raw: str) -> str:
    clean = re.sub(r"[^A-Za-z0-9_-]+", "_", raw).strip("_")
    return clean


def load_story_ids(story_file: Path) -> set[str]:
    content = story_file.read_text(encoding="utf-8")
    raw_ids = re.findall(r"\[id:([A-Za-z0-9_-]+)\]", content)
    return {sanitize_line_id(v) for v in raw_ids if sanitize_line_id(v)}


def consume_queue_id(queue_file: Path) -> Tuple[Optional[str], Optional[str]]:
    if not queue_file.exists():
        return None, f"queue file not found: {queue_file}"

    lines = queue_file.read_text(encoding="utf-8").splitlines()
    remaining = []
    taken = None
    consumed = False

    for line in lines:
        stripped = line.strip()
        if (not consumed) and stripped and (not stripped.startswith("#")):
            taken = stripped
            consumed = True
            continue
        remaining.append(line)

    if taken is None:
        return None, "queue file has no remaining line ids"

    tmp = queue_file.with_suffix(queue_file.suffix + ".tmp")
    content = "\n".join(remaining)
    if content:
        content += "\n"
    tmp.write_text(content, encoding="utf-8")
    os.replace(tmp, queue_file)
    return taken, None


def resolve_line_id(
    src: Path, queue_file: Optional[Path], line_id_re: Optional[re.Pattern[str]]
) -> Tuple[Optional[str], Optional[str]]:
    if queue_file is not None:
        value, err = consume_queue_id(queue_file)
        if err:
            return None, err
        clean = sanitize_line_id(value or "")
        if not clean:
            return None, "queue returned empty/invalid line id"
        return clean, None

    stem = src.stem
    if line_id_re is None:
        clean = sanitize_line_id(stem)
        if not clean:
            return None, f"could not derive line id from filename stem: {stem}"
        return clean, None

    match = line_id_re.search(stem)
    if not match:
        return None, f"filename did not match line-id regex: {stem}"

    raw = match.group(1) if match.groups() else match.group(0)
    clean = sanitize_line_id(raw)
    if not clean:
        return None, f"regex line id is empty/invalid: {raw}"
    return clean, None


def publish_with_ffmpeg(
    ffmpeg_bin: str,
    src: Path,
    dst_tmp: Path,
    sample_rate: int,
    channels: int,
    codec: str,
) -> Tuple[bool, str]:
    cmd = [
        ffmpeg_bin,
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(src),
        "-ar",
        str(sample_rate),
        "-ac",
        str(channels),
        "-c:a",
        codec,
        str(dst_tmp),
    ]
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
    except FileNotFoundError:
        return False, f"ffmpeg not found: {ffmpeg_bin}"

    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout or "").strip()
        return False, f"ffmpeg failed: {detail}"
    return True, "ok"


def publish_file(
    src: Path,
    dst: Path,
    use_ffmpeg: bool,
    ffmpeg_bin: str,
    sample_rate: int,
    channels: int,
    codec: str,
) -> Tuple[bool, str]:
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst_tmp = dst.with_suffix(dst.suffix + ".tmp")
    if dst_tmp.exists():
        dst_tmp.unlink()

    try:
        if use_ffmpeg:
            ok, message = publish_with_ffmpeg(
                ffmpeg_bin=ffmpeg_bin,
                src=src,
                dst_tmp=dst_tmp,
                sample_rate=sample_rate,
                channels=channels,
                codec=codec,
            )
            if not ok:
                return False, message
        else:
            shutil.copy2(src, dst_tmp)

        os.replace(dst_tmp, dst)
        return True, "ok"
    except Exception as exc:  # pylint: disable=broad-except
        if dst_tmp.exists():
            dst_tmp.unlink()
        return False, str(exc)


def archive_source(src: Path, archive_dir: Path) -> None:
    archive_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    dst = archive_dir / f"{stamp}__{src.name}"
    os.replace(src, dst)


def setup_signal_handlers() -> None:
    def _handle_signal(_sig: int, _frame: object) -> None:
        global STOP  # noqa: PLW0603
        STOP = True

    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)


def iter_audio_files(source_dir: Path, extensions: set[str]) -> list[Path]:
    files = []
    for path in source_dir.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() not in extensions:
            continue
        files.append(path)
    files.sort(key=lambda p: (str(p.parent), p.name))
    return files


def main() -> int:
    args = parse_args()
    setup_signal_handlers()

    source_dir = Path(args.source_dir).expanduser().resolve()
    if not source_dir.exists():
        print(f"ERROR: source dir not found: {source_dir}", file=sys.stderr)
        return 2

    target_root = Path(args.target_root).expanduser().resolve()
    log_file = Path(args.log_file).expanduser().resolve()
    archive_dir = Path(args.archive_dir).expanduser().resolve() if args.archive_dir else None
    queue_file = Path(args.queue_file).expanduser().resolve() if args.queue_file else None
    story_file = Path(args.story_file).expanduser().resolve() if args.story_file else None

    try:
        extensions = {
            ext.strip().lower()
            for ext in args.extensions.split(",")
            if ext.strip().startswith(".")
        }
    except Exception:  # pylint: disable=broad-except
        print("ERROR: invalid --extensions value", file=sys.stderr)
        return 2

    if not extensions:
        print("ERROR: no valid extensions configured", file=sys.stderr)
        return 2

    line_id_re = re.compile(args.line_id_regex) if args.line_id_regex else None
    story_ids: Optional[set[str]] = None
    if story_file is not None:
        if not story_file.exists():
            print(f"ERROR: story file not found: {story_file}", file=sys.stderr)
            return 2
        story_ids = load_story_ids(story_file)
        if not story_ids:
            print(
                f"ERROR: no [id:...] entries found in story file: {story_file}",
                file=sys.stderr,
            )
            return 2

    stable_counts: Dict[str, int] = {}
    last_stats: Dict[str, Tuple[int, int]] = {}
    processed_mtime: Dict[str, int] = {}

    emit_log(
        log_file,
        "info",
        "watch_start",
        source_dir=str(source_dir),
        target_root=str(target_root),
        locale=args.locale,
        queue_file=str(queue_file) if queue_file else None,
        story_file=str(story_file) if story_file else None,
        story_id_count=(len(story_ids) if story_ids is not None else None),
        ffmpeg=bool(args.ffmpeg),
    )

    while not STOP:
        files = iter_audio_files(source_dir, extensions)
        for src in files:
            key = str(src)
            try:
                stat = src.stat()
            except FileNotFoundError:
                continue

            snapshot = (stat.st_mtime_ns, stat.st_size)
            previous = last_stats.get(key)
            last_stats[key] = snapshot

            if previous == snapshot:
                stable_counts[key] = stable_counts.get(key, 0) + 1
            else:
                stable_counts[key] = 0

            if stable_counts[key] < args.settle_polls:
                continue

            if processed_mtime.get(key) == stat.st_mtime_ns:
                continue

            line_id, id_error = resolve_line_id(src, queue_file, line_id_re)
            if id_error:
                emit_log(
                    log_file,
                    "error",
                    "line_id_resolve_failed",
                    source=str(src),
                    reason=id_error,
                )
                processed_mtime[key] = stat.st_mtime_ns
                continue
            if story_ids is not None and line_id not in story_ids:
                emit_log(
                    log_file,
                    "error",
                    "line_id_not_in_story",
                    source=str(src),
                    line_id=line_id,
                    story_file=str(story_file),
                )
                processed_mtime[key] = stat.st_mtime_ns
                continue

            dst = target_root / args.locale / f"{line_id}.wav"
            ok, message = publish_file(
                src=src,
                dst=dst,
                use_ffmpeg=bool(args.ffmpeg),
                ffmpeg_bin=args.ffmpeg_bin,
                sample_rate=args.sample_rate,
                channels=args.channels,
                codec=args.codec,
            )
            if not ok:
                emit_log(
                    log_file,
                    "error",
                    "publish_failed",
                    source=str(src),
                    destination=str(dst),
                    reason=message,
                )
                processed_mtime[key] = stat.st_mtime_ns
                continue

            if archive_dir is not None:
                try:
                    archive_source(src, archive_dir)
                except Exception as exc:  # pylint: disable=broad-except
                    emit_log(
                        log_file,
                        "warn",
                        "archive_failed",
                        source=str(src),
                        archive_dir=str(archive_dir),
                        reason=str(exc),
                    )

            emit_log(
                log_file,
                "info",
                "publish_ok",
                source=str(src),
                destination=str(dst),
                line_id=line_id,
                locale=args.locale,
            )
            processed_mtime[key] = stat.st_mtime_ns

        if args.once:
            break
        time.sleep(max(0.1, args.scan_interval))

    emit_log(log_file, "info", "watch_stop")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
