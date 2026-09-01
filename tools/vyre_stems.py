#!/usr/bin/env python3
"""Prepare cached four-part stem files outside VYRE DJ's audio process.

This helper intentionally has no Python package dependencies of its own. It
coordinates FFmpeg and Stemgen, records a machine-readable manifest, and keeps
all temporary/output files below the VYRE DJ local application-data folder.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Dict, Optional, Sequence


APP_FOLDER = "VYRE DJ"
STEM_CACHE_VERSION = 1
SUPPORTED_STEMGEN_INPUTS = {".wav", ".wave", ".aif", ".aiff", ".flac"}


class StemPreparationError(RuntimeError):
    """An actionable problem that can be shown directly to the user."""


def default_stem_root() -> Path:
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        return Path(local_app_data) / APP_FOLDER / "stems"
    return Path.home() / ".local" / "share" / "vyre-dj" / "stems"


def executable_status(name: str) -> Dict[str, object]:
    path = shutil.which(name)
    return {"available": path is not None, "path": path}


def python_module_available(module: str) -> bool:
    check = subprocess.run(
        [sys.executable, "-c", f"import {module}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return check.returncode == 0


def find_stemgen(stemgen_root: Optional[Path]) -> Optional[Path]:
    candidates = []
    if stemgen_root:
        candidates.extend(
            [stemgen_root / "stemgen.py", stemgen_root / "stemgen" / "stemgen.py"]
        )
    env_root = os.environ.get("VYRE_STEMGEN_ROOT")
    if env_root:
        root = Path(env_root)
        candidates.extend([root / "stemgen.py", root / "stemgen" / "stemgen.py"])
    command = shutil.which("stemgen.py")
    if command:
        candidates.append(Path(command))
    return next((path.resolve() for path in candidates if path.is_file()), None)


def dependency_report(stemgen_root: Optional[Path]) -> Dict[str, object]:
    stemgen = find_stemgen(stemgen_root)
    return {
        "ready": bool(
            stemgen
            and shutil.which("ffmpeg")
            and shutil.which("sox")
            and python_module_available("demucs")
            and python_module_available("mutagen")
        ),
        "python": {"available": True, "path": sys.executable, "version": sys.version.split()[0]},
        "ffmpeg": executable_status("ffmpeg"),
        "sox": executable_status("sox"),
        "demucs": {"available": python_module_available("demucs")},
        "mutagen": {"available": python_module_available("mutagen")},
        "stemgen": {"available": stemgen is not None, "path": str(stemgen) if stemgen else None},
        "cache_root": str(default_stem_root()),
    }


def track_fingerprint(path: Path) -> str:
    stat = path.stat()
    digest = hashlib.sha256()
    digest.update(str(path.resolve()).casefold().encode("utf-8"))
    digest.update(str(stat.st_size).encode("ascii"))
    digest.update(str(stat.st_mtime_ns).encode("ascii"))
    sample_size = 1024 * 1024
    with path.open("rb") as source:
        digest.update(source.read(sample_size))
        if stat.st_size > sample_size:
            source.seek(max(0, stat.st_size - sample_size))
            digest.update(source.read(sample_size))
    return digest.hexdigest()[:24]


def run_checked(command: Sequence[str], cwd: Path) -> None:
    result = subprocess.run(command, cwd=str(cwd), check=False)
    if result.returncode != 0:
        raise StemPreparationError(
            f"Stem preparation command failed with exit code {result.returncode}: "
            + " ".join(command)
        )


def write_manifest(path: Path, data: Dict[str, object]) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")
    temporary.replace(path)


def prepare_track(input_path: Path, cache_root: Path, stemgen_root: Optional[Path]) -> Path:
    input_path = input_path.expanduser().resolve()
    if not input_path.is_file():
        raise StemPreparationError(f"Track does not exist: {input_path}")

    report = dependency_report(stemgen_root)
    if not report["ready"]:
        missing = [
            name
            for name in ("ffmpeg", "sox", "demucs", "mutagen", "stemgen")
            if not report[name]["available"]
        ]
        raise StemPreparationError(
            "Stem tools are not ready. Missing: "
            + ", ".join(missing)
            + ". Run `vyre_stems.py check` for details."
        )

    stemgen = Path(str(report["stemgen"]["path"]))
    fingerprint = track_fingerprint(input_path)
    cache_dir = cache_root.expanduser().resolve() / "cache" / fingerprint
    cache_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = cache_dir / "manifest.json"

    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        cached_output = Path(str(manifest.get("output", "")))
        if manifest.get("status") == "ready" and cached_output.is_file():
            return cached_output

    work_root = cache_root.expanduser().resolve() / "work"
    work_root.mkdir(parents=True, exist_ok=True)
    manifest: Dict[str, object] = {
        "cache_version": STEM_CACHE_VERSION,
        "fingerprint": fingerprint,
        "input": str(input_path),
        "output": None,
        "started_at": int(time.time()),
        "status": "preparing",
    }
    write_manifest(manifest_path, manifest)

    try:
        with tempfile.TemporaryDirectory(prefix=f"{fingerprint}-", dir=str(work_root)) as temp_name:
            work_dir = Path(temp_name)
            stemgen_input = input_path
            if input_path.suffix.casefold() not in SUPPORTED_STEMGEN_INPUTS:
                stemgen_input = work_dir / "source.wav"
                run_checked(
                    [
                        "ffmpeg",
                        "-hide_banner",
                        "-loglevel",
                        "warning",
                        "-y",
                        "-i",
                        str(input_path),
                        "-ar",
                        "44100",
                        "-ac",
                        "2",
                        str(stemgen_input),
                    ],
                    work_dir,
                )

            run_checked([sys.executable, str(stemgen), str(stemgen_input)], work_dir)
            generated = sorted(work_dir.rglob("*.stem.m4a"), key=lambda path: path.stat().st_mtime_ns)
            if not generated:
                raise StemPreparationError("Stemgen completed but did not create a .stem.m4a file.")

            output = cache_dir / f"{input_path.stem}.stem.m4a"
            shutil.copy2(generated[-1], output)
            manifest.update(
                {"completed_at": int(time.time()), "output": str(output), "status": "ready"}
            )
            write_manifest(manifest_path, manifest)
            return output
    except Exception as error:
        manifest.update({"completed_at": int(time.time()), "error": str(error), "status": "failed"})
        write_manifest(manifest_path, manifest)
        raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="VYRE DJ on-demand stem preparation worker")
    parser.add_argument(
        "--stemgen-root",
        type=Path,
        help="Folder containing the Stemgen checkout (or set VYRE_STEMGEN_ROOT)",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check", help="Print dependency readiness as JSON")
    prepare = subparsers.add_parser("prepare", help="Generate or reuse a cached .stem.m4a file")
    prepare.add_argument("track", type=Path)
    prepare.add_argument("--cache-root", type=Path, default=default_stem_root())
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "check":
        report = dependency_report(args.stemgen_root)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0 if report["ready"] else 2

    try:
        output = prepare_track(args.track, args.cache_root, args.stemgen_root)
    except StemPreparationError as error:
        print(str(error), file=sys.stderr)
        return 2
    print(json.dumps({"output": str(output), "status": "ready"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
