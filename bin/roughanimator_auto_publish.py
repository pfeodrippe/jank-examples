#!/usr/bin/env python3
"""Sync RoughAnimator project files and publish fiction left-panel animation.

Pipeline:
1) Optional sync from iPad (USB or Wi-Fi) using pymobiledevice3 House Arrest.
2) Parse RoughAnimator project timeline from voiture.ra.
3) Composite layer frames.
4) Build left-panel RGBA frames.
5) Auto-crop to the union alpha bbox (small transparent sprites).
6) Write resources/fiction/anim/voiture/manifest.txt for fiction.anim.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class LayerEntry:
    start: int
    duration: int


@dataclass(frozen=True)
class Layer:
    directory: Path
    entries: list[LayerEntry]


def log(event: str, **payload: object) -> None:
    entry = {"event": event, **payload}
    print(json.dumps(entry, ensure_ascii=True))


def run(
    cmd: list[str],
    *,
    cwd: Path | None = None,
    stdin_text: str | None = None,
) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        input=stdin_text,
        text=True,
        capture_output=True,
        check=False,
    )
    return proc


def run_bytes(
    cmd: list[str],
    *,
    cwd: Path | None = None,
) -> subprocess.CompletedProcess[bytes]:
    proc = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        capture_output=True,
        check=False,
    )
    return proc


def require_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise RuntimeError(f"required tool not found in PATH: {name}")


def parse_project_meta(project_dir: Path) -> tuple[int, int, float]:
    data_file = project_dir / "data.txt"
    if not data_file.exists():
        raise RuntimeError(f"missing RoughAnimator data file: {data_file}")

    width = 1920
    height = 1080
    fps = 8.0

    for raw in data_file.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("canvasWidth:"):
            width = int(line.split(":", 1)[1])
        elif line.startswith("canvasHeight:"):
            height = int(line.split(":", 1)[1])
        elif line.startswith("framesPerSecond:"):
            fps = float(line.split(":", 1)[1])

    return width, height, fps


def discover_layers(project_dir: Path) -> list[Layer]:
    out: list[Layer] = []
    for child in sorted(project_dir.iterdir(), key=lambda p: p.name):
        if not child.is_dir():
            continue
        if not child.name.isdigit():
            continue
        layer_data = child / "layerData.txt"
        if not layer_data.exists():
            continue
        entries = parse_layer_entries(layer_data, child)
        out.append(Layer(directory=child, entries=entries))
    return out


def parse_layer_entries(layer_data_file: Path, layer_dir: Path) -> list[LayerEntry]:
    entries: list[LayerEntry] = []
    for raw in layer_data_file.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        parts = line.split(":")
        if len(parts) < 2:
            continue
        if not (parts[0].isdigit() and parts[1].isdigit()):
            continue
        start = int(parts[0])
        duration = max(1, int(parts[1]))
        entries.append(LayerEntry(start=start, duration=duration))

    if entries:
        entries.sort(key=lambda e: e.start)
        return entries

    # Fallback: if layerData has no timeline rows, derive from png names.
    fallback: list[LayerEntry] = []
    for png in sorted(layer_dir.glob("*.png")):
        stem = png.stem
        if stem.isdigit():
            fallback.append(LayerEntry(start=int(stem), duration=1))
    return fallback


def active_cel_start(entries: list[LayerEntry], frame_idx: int) -> int | None:
    active: int | None = None
    for entry in entries:
        if entry.start <= frame_idx < (entry.start + entry.duration):
            active = entry.start
    return active


def timeline_frame_count(layers: Iterable[Layer]) -> int:
    max_frame = -1
    for layer in layers:
        for entry in layer.entries:
            max_frame = max(max_frame, entry.start + entry.duration - 1)
    return max_frame + 1


def ffprobe_dimensions(image_path: Path) -> tuple[int, int]:
    proc = run(
        [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height",
            "-of",
            "csv=p=0:s=x",
            str(image_path),
        ]
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "ffprobe failed")
    text = (proc.stdout or "").strip()
    parts = text.split("x")
    if len(parts) != 2:
        raise RuntimeError(f"unexpected ffprobe output: {text}")
    return int(parts[0]), int(parts[1])


def compose_layer_stack(
    layer_pngs: list[Path],
    out_file: Path,
    canvas_width: int,
    canvas_height: int,
) -> None:
    if not layer_pngs:
        raise RuntimeError("compose_layer_stack called with no input layers")

    cmd: list[str] = [
        "ffmpeg",
        "-y",
        "-loglevel",
        "error",
        "-f",
        "lavfi",
        "-i",
        f"color=c=black@0.0:s={canvas_width}x{canvas_height},format=rgba",
    ]
    for path in layer_pngs:
        cmd.extend(["-i", str(path)])

    filter_parts = ["[0:v]format=rgba[b0]"]
    prev = "b0"
    for i in range(1, len(layer_pngs) + 1):
        cur = f"b{i}"
        filter_parts.append(f"[{prev}][{i}:v]overlay=0:0:format=auto[{cur}]")
        prev = cur
    filter_graph = ";".join(filter_parts)

    cmd.extend(
        [
            "-filter_complex",
            filter_graph,
            "-map",
            f"[{prev}]",
            "-frames:v",
            "1",
            str(out_file),
        ]
    )
    proc = run(cmd)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "ffmpeg layer compose failed")


def compose_left_panel_frame(
    left_frame: Path,
    out_file: Path,
    *,
    left_width: int,
    bg_height: int,
) -> None:
    proc = run(
        [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-i",
            str(left_frame),
            "-vf",
            (
                f"scale={left_width}:{bg_height}:force_original_aspect_ratio=increase,"
                f"crop={left_width}:{bg_height}:0:0"
            ),
            "-frames:v",
            "1",
            "-compression_level",
            "9",
            "-pred",
            "mixed",
            "-pix_fmt",
            "rgba",
            str(out_file),
        ]
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "ffmpeg panel compose failed")


def detect_alpha_bbox(image_path: Path, *, alpha_threshold: int = 8) -> tuple[int, int, int, int] | None:
    width, height = ffprobe_dimensions(image_path)
    proc = run_bytes(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(image_path),
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgba",
            "-frames:v",
            "1",
            "-",
        ]
    )
    if proc.returncode != 0:
        stderr_text = proc.stderr.decode("utf-8", errors="replace").strip()
        stdout_text = proc.stdout.decode("utf-8", errors="replace").strip()
        raise RuntimeError(stderr_text or stdout_text or "ffmpeg raw rgba extract failed")

    data = proc.stdout
    expected = width * height * 4
    if len(data) < expected:
        raise RuntimeError(f"raw rgba frame too short for {image_path}: got={len(data)} expected={expected}")

    min_x = width
    min_y = height
    max_x = -1
    max_y = -1

    for y in range(height):
        row_offset = y * width * 4
        for x in range(width):
            alpha = data[row_offset + x * 4 + 3]
            if alpha > alpha_threshold:
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y

    if max_x < min_x or max_y < min_y:
        return None

    return (min_x, min_y, max_x - min_x + 1, max_y - min_y + 1)


def crop_overlay_frame(
    src_file: Path,
    out_file: Path,
    *,
    crop_x: int,
    crop_y: int,
    crop_w: int,
    crop_h: int,
) -> None:
    proc = run(
        [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-i",
            str(src_file),
            "-vf",
            f"crop={crop_w}:{crop_h}:{crop_x}:{crop_y}",
            "-frames:v",
            "1",
            "-compression_level",
            "9",
            "-pred",
            "mixed",
            "-pix_fmt",
            "rgba",
            str(out_file),
        ]
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "ffmpeg overlay crop failed")


def publish_animation(
    *,
    project_dir: Path,
    output_dir: Path,
    base_bg: Path,
    left_ratio: float,
    repo_root: Path,
) -> tuple[int, float]:
    if not project_dir.exists():
        raise RuntimeError(f"project dir not found: {project_dir}")
    if not base_bg.exists():
        raise RuntimeError(f"base background not found: {base_bg}")

    canvas_w, canvas_h, fps = parse_project_meta(project_dir)
    layers = discover_layers(project_dir)
    if not layers:
        raise RuntimeError(f"no drawable layers found in: {project_dir}")

    frame_count = timeline_frame_count(layers)
    if frame_count <= 0:
        raise RuntimeError("timeline frame count is zero")

    bg_w, bg_h = ffprobe_dimensions(base_bg)
    left_width = max(1, int(bg_w * left_ratio))

    tmp_out = output_dir.parent / f"{output_dir.name}.tmp"
    if tmp_out.exists():
        shutil.rmtree(tmp_out)
    tmp_out.mkdir(parents=True, exist_ok=True)

    tmp_flat = tmp_out / "_flat"
    tmp_flat.mkdir(parents=True, exist_ok=True)
    tmp_panel = tmp_out / "_panel"
    tmp_panel.mkdir(parents=True, exist_ok=True)

    panel_frame_paths: list[Path] = []
    for frame_idx in range(frame_count):
        active_layers: list[Path] = []
        for layer in layers:
            cel_start = active_cel_start(layer.entries, frame_idx)
            if cel_start is None:
                continue
            candidate = layer.directory / f"{cel_start:04d}.png"
            if candidate.exists():
                active_layers.append(candidate)

        if not active_layers:
            continue

        flat_frame = tmp_flat / f"frame-{frame_idx:04d}.png"
        compose_layer_stack(active_layers, flat_frame, canvas_w, canvas_h)

        panel_frame = tmp_panel / f"frame-{frame_idx:04d}.png"
        compose_left_panel_frame(
            left_frame=flat_frame,
            out_file=panel_frame,
            left_width=left_width,
            bg_height=bg_h,
        )
        panel_frame_paths.append(panel_frame)

    if not panel_frame_paths:
        raise RuntimeError("no output frames produced")

    source_bbox_union: tuple[int, int, int, int] | None = None
    seen_cels: set[Path] = set()
    for layer in layers:
        for entry in layer.entries:
            candidate = layer.directory / f"{entry.start:04d}.png"
            if not candidate.exists() or candidate in seen_cels:
                continue
            seen_cels.add(candidate)
            bbox = detect_alpha_bbox(candidate)
            if bbox is None:
                continue
            if source_bbox_union is None:
                source_bbox_union = bbox
            else:
                x0 = min(source_bbox_union[0], bbox[0])
                y0 = min(source_bbox_union[1], bbox[1])
                x1 = max(source_bbox_union[0] + source_bbox_union[2], bbox[0] + bbox[2])
                y1 = max(source_bbox_union[1] + source_bbox_union[3], bbox[1] + bbox[3])
                source_bbox_union = (x0, y0, x1 - x0, y1 - y0)

    if source_bbox_union is None:
        source_bbox_union = (0, 0, canvas_w, canvas_h)

    src_x, src_y, src_w, src_h = source_bbox_union
    scale = max(left_width / canvas_w, bg_h / canvas_h)

    mapped_x0 = max(0, int(src_x * scale))
    mapped_y0 = max(0, int(src_y * scale))
    mapped_x1 = min(left_width, int((src_x + src_w) * scale + 0.9999))
    mapped_y1 = min(bg_h, int((src_y + src_h) * scale + 0.9999))

    if mapped_x1 <= mapped_x0 or mapped_y1 <= mapped_y0:
        crop_x, crop_y, crop_w, crop_h = (0, 0, left_width, bg_h)
    else:
        crop_x = mapped_x0
        crop_y = mapped_y0
        crop_w = mapped_x1 - mapped_x0
        crop_h = mapped_y1 - mapped_y0

    frame_paths: list[Path] = []
    for panel_frame in panel_frame_paths:
        out_frame = tmp_out / panel_frame.name
        crop_overlay_frame(
            panel_frame,
            out_frame,
            crop_x=crop_x,
            crop_y=crop_y,
            crop_w=crop_w,
            crop_h=crop_h,
        )
        frame_paths.append(out_frame)

    manifest_lines = [
        "# auto-generated by roughanimator_auto_publish.py",
        f"fps={fps}",
        f"overlay_x_ratio={crop_x / bg_w:.8f}",
        f"overlay_y_ratio={crop_y / bg_h:.8f}",
        f"overlay_w_ratio={crop_w / bg_w:.8f}",
        f"overlay_h_ratio={crop_h / bg_h:.8f}",
    ]
    for path in frame_paths:
        rel = path.relative_to(tmp_out)
        published_rel = output_dir / rel
        try:
            manifest_path = published_rel.relative_to(repo_root)
            manifest_lines.append(manifest_path.as_posix())
        except ValueError:
            manifest_lines.append(str(published_rel))

    (tmp_out / "manifest.txt").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")
    shutil.rmtree(tmp_flat, ignore_errors=True)
    shutil.rmtree(tmp_panel, ignore_errors=True)

    if output_dir.exists():
        shutil.rmtree(output_dir)
    shutil.move(str(tmp_out), str(output_dir))

    return len(frame_paths), fps


def resolve_project_dir(root: Path, project_name: str) -> Path:
    # Supports both:
    # - <root>/<project>/data.txt
    # - <root>/<project>/<project>/data.txt
    direct = root / project_name
    nested = root / project_name / project_name
    if (direct / "data.txt").exists():
        return direct
    if (nested / "data.txt").exists():
        return nested
    return nested


def sync_from_device(
    *,
    bundle_id: str,
    project_name: str,
    sync_root: Path,
    udid: str,
) -> Path:
    require_tool("pymobiledevice3")
    sync_root.mkdir(parents=True, exist_ok=True)

    tmp_pull = sync_root / ".pull_tmp"
    if tmp_pull.exists():
        shutil.rmtree(tmp_pull)
    tmp_pull.mkdir(parents=True, exist_ok=True)

    cmd = ["pymobiledevice3", "apps", "afc", "--documents"]
    if udid:
        cmd.extend(["--udid", udid])
    cmd.append(bundle_id)

    shell_input = f"pull '{project_name}' '{tmp_pull}'\nexit\n"
    proc = run(cmd, stdin_text=shell_input)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "pymobiledevice3 pull failed")

    candidate_a = tmp_pull / project_name
    candidate_b = tmp_pull / project_name / project_name
    pulled = None
    if (candidate_a / "data.txt").exists():
        pulled = candidate_a
    elif (candidate_b / "data.txt").exists():
        pulled = candidate_b
    else:
        project_base = project_name.removesuffix(".ra")
        matches = sorted(
            p.parent
            for p in tmp_pull.rglob("data.txt")
            if p.parent.is_dir()
        )
        preferred = [
            d
            for d in matches
            if d.name == project_name or d.name == project_base or project_base in d.name
        ]
        if preferred:
            pulled = preferred[0]
        elif matches:
            pulled = matches[0]
    if pulled is None:
        raise RuntimeError(f"could not find pulled project tree under {tmp_pull}")

    final_project_root = sync_root / project_name
    if final_project_root.exists():
        shutil.rmtree(final_project_root)
    shutil.copytree(pulled, final_project_root)
    shutil.rmtree(tmp_pull, ignore_errors=True)
    return final_project_root


def project_signature(project_dir: Path) -> tuple[int, int]:
    latest_mtime = 0
    file_count = 0
    if not project_dir.exists():
        return (0, 0)
    for path in project_dir.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() not in {".png", ".txt"}:
            continue
        file_count += 1
        try:
            latest_mtime = max(latest_mtime, path.stat().st_mtime_ns)
        except FileNotFoundError:
            pass
    return (latest_mtime, file_count)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Publish RoughAnimator voiture.ra frames for fiction.")
    parser.add_argument("--project-name", default="voiture.ra", help="RoughAnimator project folder name.")
    parser.add_argument(
        "--sync-root",
        default="roughanimator_from_ipad",
        help="Local root where pulled project files are stored.",
    )
    parser.add_argument(
        "--project-dir",
        default="",
        help="Project dir override. If empty, resolved from --sync-root/--project-name.",
    )
    parser.add_argument(
        "--sync-device",
        action="store_true",
        help="Pull latest project from connected/paired iPad via pymobiledevice3.",
    )
    parser.add_argument("--bundle-id", default="com.weirdhat.roughanimator", help="RoughAnimator bundle id.")
    parser.add_argument("--udid", default="", help="Optional target device UDID.")
    parser.add_argument(
        "--sync-interval",
        type=float,
        default=3.0,
        help="Seconds between device pulls when --sync-device is set.",
    )
    parser.add_argument(
        "--output-dir",
        default="resources/fiction/anim/voiture",
        help="Output dir for composed frames + manifest.",
    )
    parser.add_argument("--base-bg", default="resources/fiction/bg-1.png", help="Base fiction background image.")
    parser.add_argument(
        "--left-ratio",
        type=float,
        default=0.60,
        help="Left animation panel width ratio relative to background width.",
    )
    parser.add_argument("--scan-interval", type=float, default=1.0, help="Polling interval in seconds.")
    parser.add_argument("--once", action="store_true", help="Run one cycle and exit.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path.cwd().resolve()

    try:
        require_tool("ffmpeg")
        require_tool("ffprobe")
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    sync_root = Path(args.sync_root).expanduser().resolve()
    output_dir = Path(args.output_dir).expanduser().resolve()
    base_bg = Path(args.base_bg).expanduser().resolve()

    if args.project_dir:
        project_dir = Path(args.project_dir).expanduser().resolve()
    else:
        project_dir = resolve_project_dir(sync_root, args.project_name)

    last_sig = (-1, -1)
    next_sync_at = 0.0
    had_once_error = False

    while True:
        if args.sync_device and time.time() >= next_sync_at:
            try:
                pulled_root = sync_from_device(
                    bundle_id=args.bundle_id,
                    project_name=args.project_name,
                    sync_root=sync_root,
                    udid=args.udid,
                )
                log("sync_ok", project=str(pulled_root))
                project_dir = pulled_root
            except Exception as exc:  # noqa: BLE001
                log("sync_error", reason=str(exc))
                if args.once:
                    had_once_error = True
            next_sync_at = time.time() + max(1.0, args.sync_interval)

        sig = project_signature(project_dir)
        if sig != last_sig:
            try:
                frame_count, fps = publish_animation(
                    project_dir=project_dir,
                    output_dir=output_dir,
                    base_bg=base_bg,
                    left_ratio=args.left_ratio,
                    repo_root=repo_root,
                )
                last_sig = sig
                log(
                    "publish_ok",
                    project=str(project_dir),
                    output_dir=str(output_dir),
                    frame_count=frame_count,
                    fps=fps,
                )
            except Exception as exc:  # noqa: BLE001
                log("publish_error", project=str(project_dir), reason=str(exc))
                if args.once:
                    had_once_error = True

        if args.once:
            break
        time.sleep(max(0.2, args.scan_interval))

    if args.once and had_once_error:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
