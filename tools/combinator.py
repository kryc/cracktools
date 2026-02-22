#!/usr/bin/env python3
"""Generate hashcat .restore files for combinator / hybrid / mask attacks
using length-split wordlists.

The input directory should contain wordlists named "word_N.txt" where N
is the length of every word in that file.  The script enumerates all
ordered pairs (a, b) — including (a, a) — whose sum falls within
[min, max] and writes a binary .restore file for each one so that
    hashcat --session <name> --restore
picks up the job without any extra arguments.

When a word length is at or below --hybrid-threshold, the wordlist is
replaced with a brute-force mask ("?a" repeated N times):
  - Both sides short  → skipped (no wordlist involved)
  - Only right short  → hybrid     -a 6  wordlist + ?a × right
  - Only left short   → hybrid     -a 7  ?a × left + wordlist
  - Neither short     → combinator -a 1  wordlist + wordlist

Restore format reference: https://hashcat.net/wiki/doku.php?id=restore
"""

import argparse
import itertools
import logging
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Restore-file writer
# ---------------------------------------------------------------------------

def build_argv_blob(argv: list[str]) -> bytes:
    """Encode argv as the newline-separated blob hashcat expects."""
    return "\n".join(argv).encode() + b"\n"


def write_restore_file(
    path: Path,
    *,
    version: int,
    cwd: str,
    dicts_pos: int = 0,
    masks_pos: int = 0,
    words_cur: int = 0,
    argv: list[str],
) -> None:
    """Write a hashcat .restore file at *path*."""
    argc = len(argv)
    cwd_bytes = cwd.encode()[:256]
    cwd_padded = cwd_bytes + b"\x00" * (256 - len(cwd_bytes))
    argv_blob = build_argv_blob(argv)

    with open(path, "wb") as fp:
        # version          @ 0x000  (4 bytes)
        fp.write(struct.pack("<I", version))
        # cwd              @ 0x004  (256 bytes)
        fp.write(cwd_padded)
        # dicts_pos        @ 0x104  (4 bytes)
        fp.write(struct.pack("<I", dicts_pos))
        # masks_pos        @ 0x108  (4 bytes)
        fp.write(struct.pack("<I", masks_pos))
        # padding          @ 0x10C  (4 bytes)
        fp.write(b"\x00" * 4)
        # words_cur        @ 0x110  (8 bytes)
        fp.write(struct.pack("<Q", words_cur))
        # argc             @ 0x118  (4 bytes)
        fp.write(struct.pack("<I", argc))
        # padding          @ 0x11C  (4 bytes)
        fp.write(b"\x00" * 4)
        # argv pointer     @ 0x120  (8 bytes) — unused, kept for alignment
        fp.write(b"\x00" * 8)
        # argv data        @ 0x128+
        fp.write(argv_blob)


def read_restore_version(path: Path) -> int | None:
    """Read just the 4-byte version field from an existing .restore file."""
    try:
        with open(path, "rb") as fp:
            data = fp.read(4)
            if len(data) == 4:
                return struct.unpack("<I", data)[0]
    except OSError:
        pass
    return None


DEFAULT_HASHCAT_VERSION = 600


def detect_hashcat_version(sessions_dir: Path) -> int:
    """Return the version from the newest .restore file in *sessions_dir*.

    Falls back to DEFAULT_HASHCAT_VERSION if no restore files exist or
    none can be read.
    """
    try:
        restore_files = sorted(
            sessions_dir.glob("*.restore"),
            key=lambda p: p.stat().st_mtime,
            reverse=True,
        )
    except OSError:
        return DEFAULT_HASHCAT_VERSION

    for rf in restore_files:
        ver = read_restore_version(rf)
        if ver is not None:
            return ver

    return DEFAULT_HASHCAT_VERSION


# ---------------------------------------------------------------------------
# Wordlist discovery
# ---------------------------------------------------------------------------

WORDLIST_RE = re.compile(r"^word_(\d+)\.txt$")


def discover_lengths(wordlist_dir: Path) -> tuple[list[int], dict[int, str]]:
    """Return sorted lengths and a map from length to zero-padded filename.

    The map values are like ``"word_04.txt"`` — preserving whatever
    zero-padding the files on disk actually use.
    """
    lengths: list[int] = []
    name_map: dict[int, str] = {}  # length -> filename (e.g. 4 -> "word_04.txt")
    for entry in wordlist_dir.iterdir():
        m = WORDLIST_RE.match(entry.name)
        if m:
            n = int(m.group(1))
            lengths.append(n)
            name_map[n] = entry.name
    lengths.sort()
    return lengths, name_map


# ---------------------------------------------------------------------------
# Combination generation
# ---------------------------------------------------------------------------

def valid_combos(
    lengths: list[int], min_len: int, max_len: int
) -> list[tuple[int, int]]:
    """Return every ordered (left, right) pair with min <= sum <= max.

    Uses itertools.product so both (a,b) and (b,a) are produced
    naturally, and (a,a) pairs are included.
    """
    return [
        (a, b)
        for a, b in itertools.product(lengths, repeat=2)
        if min_len <= a + b <= max_len
    ]


# ---------------------------------------------------------------------------
# Session-name helpers
# ---------------------------------------------------------------------------

SESSION_RE = re.compile(r"^(.+)_(\d+)_(\d+)\.restore$")


def parse_session_name(filename: str) -> tuple[str, int, int] | None:
    """Extract (prefix, left, right) from a restore filename.

    Returns None if the name doesn't match the expected pattern.
    """
    m = SESSION_RE.match(filename)
    if m:
        return m.group(1), int(m.group(2)), int(m.group(3))
    return None


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Generate and run hashcat combinator / hybrid restore sessions.",
    )
    sub = p.add_subparsers(dest="command", required=True)

    # -- shared flags -------------------------------------------------------
    shared = argparse.ArgumentParser(add_help=False)
    shared.add_argument(
        "-O", "--output-dir",
        type=Path,
        default=None,
        help="Directory for .restore files "
             "(default: $HOME/.local/share/hashcat/sessions/).",
    )
    shared.add_argument(
        "--session-prefix",
        default="combo",
        help='Prefix for session names (default: "combo").',
    )
    shared.add_argument(
        "--hashcat-bin",
        default="hashcat",
        help="Path / name of the hashcat binary (default: hashcat).",
    )

    # -- build --------------------------------------------------------------
    bp = sub.add_parser(
        "build",
        parents=[shared],
        help="Generate .restore files for combinator / hybrid attacks.",
    )
    bp.add_argument(
        "wordlist_dir",
        type=Path,
        help='Directory with "word_N.txt" wordlists split by length.',
    )
    bp.add_argument(
        "-o", "--output",
        type=str,
        default="recrackedhc.txt",
        help="Hashcat output file for the restored sessions (default: recrackedhc.txt).",
    )
    bp.add_argument(
        "--hash-file",
        type=str,
        default="uncracked.txt",
        help="Path to the file containing hashes.",
    )
    bp.add_argument(
        "-m", "--min",
        type=int,
        default=9,
        dest="min_len",
        help="Minimum combined word length (default: 8).",
    )
    bp.add_argument(
        "-M", "--max",
        type=int,
        default=31,
        dest="max_len",
        help="Maximum combined word length (default: 31).",
    )
    bp.add_argument(
        "--cwd",
        default=os.getcwd(),
        help="Working directory stored in each .restore file "
             "(default: current directory).",
    )
    bp.add_argument(
        "--hybrid-threshold",
        type=int,
        default=3,
        help="Word lengths <= this value use a ?a mask instead of a "
             "wordlist, turning the attack into a hybrid (-a 6/-a 7). "
             "Pairs where both sides are below the threshold are "
             "skipped.  0 disables (default: 3).",
    )
    bp.add_argument(
        "--min-input-length",
        type=int,
        default=1,
        dest="min_input_len",
        help="Ignore wordlists whose word length is below this value "
             "(default: 1, i.e. keep everything).",
    )
    bp.add_argument(
        "--hashcat-version",
        type=int,
        default=None,
        help="Hashcat version number for the restore header. "
             "Auto-detected from the newest existing .restore file, "
             f"falls back to {DEFAULT_HASHCAT_VERSION}.",
    )
    bp.add_argument(
        "extra_flags",
        nargs="*",
        metavar="HASHCAT_FLAG",
        help="Extra hashcat flags forwarded into every restore file "
             '(e.g. -w 3 -O). Use -- before them if they start with "-".',
    )

    # -- run ----------------------------------------------------------------
    rp = sub.add_parser(
        "run",
        parents=[shared],
        help="Execute remaining restore sessions, smallest total length first.",
    )
    rp.add_argument(
        "-n", "--dry-run",
        action="store_true",
        help="Print the execution order without launching hashcat.",
    )

    return p.parse_args(argv)


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_build(args: argparse.Namespace) -> None:
    """Generate .restore files (the 'build' subcommand)."""
    # Resolve output directory
    output_dir: Path = args.output_dir or Path.home() / ".local/share/hashcat/sessions"
    output_dir.mkdir(parents=True, exist_ok=True)

    # Determine hashcat version
    if args.hashcat_version is not None:
        hc_version = args.hashcat_version
    else:
        hc_version = detect_hashcat_version(output_dir)
    log.info("Using hashcat version: %d", hc_version)

    # Resolve wordlist directory
    wl_dir: Path = args.wordlist_dir.resolve()
    if not wl_dir.is_dir():
        log.error("%s is not a directory.", wl_dir)
        sys.exit(1)

    # Discover available lengths
    lengths, wl_name_map = discover_lengths(wl_dir)
    if not lengths:
        log.error("No word_N.txt files found in %s.", wl_dir)
        sys.exit(1)
    log.info("Found wordlist lengths: %s", lengths)

    # Detect zero-pad width from the existing filenames (e.g. word_04.txt → width 2).
    pad_width = max(
        (len(WORDLIST_RE.match(v).group(1)) for v in wl_name_map.values()),
        default=1,
    )

    def wl_name(n: int) -> str:
        """Format a wordlist filename with the correct zero-padding."""
        return f"word_{n:0{pad_width}d}.txt"

    # Drop wordlist lengths below the minimum input length.
    if args.min_input_len > 1:
        lengths = [n for n in lengths if n >= args.min_input_len]
        log.info("After --min-input-length %d: %s", args.min_input_len, lengths)

    # Ensure all mask-only lengths min_input_len..threshold are present
    # even without a corresponding wordlist file (they're only used as ?a masks).
    threshold = args.hybrid_threshold
    if threshold > 0:
        mask_lengths = set(range(args.min_input_len, threshold + 1))
        merged = sorted(set(lengths) | mask_lengths)
        added = mask_lengths - set(lengths)
        if added:
            log.info("Added mask-only lengths: %s", sorted(added))
        lengths = merged
    log.info("All lengths in play:    %s", lengths)

    # Generate valid combos
    combos = valid_combos(lengths, args.min_len, args.max_len)
    if not combos:
        log.error("No combinations satisfy the length constraints.")
        sys.exit(1)
    log.info("Generating %d restore file(s) ...", len(combos))

    cwd = args.cwd

    seen_sessions: set[str] = set()

    for left, right in combos:
        left_is_mask = threshold > 0 and left <= threshold
        right_is_mask = threshold > 0 and right <= threshold

        left_path = str(wl_dir / wl_name(left))
        right_path = str(wl_dir / wl_name(right))
        left_mask = "?a" * left
        right_mask = "?a" * right

        if left_is_mask and right_is_mask:
            # Both sides below threshold — no wordlist involved, skip
            continue
        elif right_is_mask:
            # Hybrid: wordlist + mask  -a 6
            attack_mode = "6"
            session = f"{args.session_prefix}_{left}_{right}"
            tail_args = [left_path, right_mask]
            label = f"hybrid6 {wl_name(left)} + {right_mask}"
        elif left_is_mask:
            # Hybrid: mask + wordlist  -a 7
            attack_mode = "7"
            session = f"{args.session_prefix}_{left}_{right}"
            tail_args = [left_mask, right_path]
            label = f"hybrid7 {left_mask} + {wl_name(right)}"
        else:
            # Combinator  -a 1
            attack_mode = "1"
            session = f"{args.session_prefix}_{left}_{right}"
            tail_args = [left_path, right_path]
            label = f"combo {wl_name(left)} + {wl_name(right)}"

        if session in seen_sessions:
            continue
        seen_sessions.add(session)

        # Build the full hashcat argv
        argv = [
            args.hashcat_bin,
            "-a", attack_mode,
            "-m", "100",
            "-O",
            "--bitmap-max", "23",
            "-w", "3",
            "--potfile-disable",
            "--markov-disable",
            "--session", session,
            "-o", args.output,
            args.hash_file,
            *tail_args,
        ]
        if args.extra_flags:
            argv.extend(args.extra_flags)

        restore_path = output_dir / f"{session}.restore"
        write_restore_file(
            restore_path,
            version=hc_version,
            cwd=cwd,
            argv=argv,
        )
        log.info("  %-40s  %s", restore_path.name, label)

    log.info("Done — wrote %d file(s) to %s", len(seen_sessions), output_dir)


def cmd_run(args: argparse.Namespace) -> None:
    """Execute remaining restore sessions (the 'run' subcommand)."""
    # Resolve sessions directory
    sessions_dir: Path = args.output_dir or Path.home() / ".local/share/hashcat/sessions"
    if not sessions_dir.is_dir():
        log.error("Sessions directory does not exist: %s", sessions_dir)
        sys.exit(1)

    # Enumerate matching restore files
    prefix = args.session_prefix
    pattern = f"{prefix}_*.restore"
    restore_files = list(sessions_dir.glob(pattern))
    if not restore_files:
        log.info("No remaining restore files matching '%s' in %s", pattern, sessions_dir)
        return

    # Parse (left, right) from each filename and sort by total length
    jobs: list[tuple[int, int, int, Path]] = []  # (total, left, right, path)
    for rf in restore_files:
        parsed = parse_session_name(rf.name)
        if parsed is None:
            log.warning("Skipping unrecognised file: %s", rf.name)
            continue
        _, left, right = parsed
        jobs.append((left + right, left, right, rf))

    jobs.sort()  # smallest total first

    log.info("Found %d session(s) to run (sorted by total word length):", len(jobs))
    for total, left, right, rf in jobs:
        session = rf.stem
        log.info("  %-40s  length %d+%d=%d", session, left, right, total)

    if args.dry_run:
        log.info("Dry run — not launching hashcat.")
        return

    # Execute each session sequentially
    for i, (total, left, right, rf) in enumerate(jobs, 1):
        session = rf.stem
        cmd = [args.hashcat_bin, "--session", session, "--restore"]
        log.info(
            "\n[%d/%d] Running session %s  (length %d+%d=%d)",
            i, len(jobs), session, left, right, total,
        )
        log.info("  %s", " ".join(cmd))

        try:
            result = subprocess.run(cmd)
        except KeyboardInterrupt:
            log.warning("\n  Interrupted by user (Ctrl+C). Stopping.")
            sys.exit(130)

        if result.returncode == 0:
            log.info("  Session %s finished successfully.", session)
        elif result.returncode == 1:
            # hashcat exit code 1 = exhausted (all keyspace tried)
            log.info("  Session %s exhausted.", session)
        elif result.returncode in (2, 3, 4, 5):
            # 2 = user abort, 3 = checkpoint abort, 4 = runtime abort, 5 = finish abort
            reason = {
                2: "user abort",
                3: "checkpoint abort",
                4: "runtime limit",
                5: "finish flag",
            }[result.returncode]
            log.info("  Session %s stopped (%s). Halting run.", session, reason)
            remaining = len(jobs) - i
            if remaining > 0:
                log.info("  %d session(s) remaining — re-run to continue.", remaining)
            return
        elif result.returncode < 0:
            # Killed by signal (e.g. -2 = SIGINT)
            import signal
            sig = -result.returncode
            sig_name = signal.Signals(sig).name if sig in signal.Signals._value2member_map_ else str(sig)
            log.warning("  Session %s killed by signal %s. Halting run.", session, sig_name)
            remaining = len(jobs) - i
            if remaining > 0:
                log.info("  %d session(s) remaining — re-run to continue.", remaining)
            sys.exit(128 + sig)
        else:
            log.warning(
                "  Session %s exited with code %d — continuing.",
                session, result.returncode,
            )

    log.info("\nAll sessions complete.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )
    args = parse_args()

    if args.command == "build":
        cmd_build(args)
    elif args.command == "run":
        cmd_run(args)


if __name__ == "__main__":
    main()
