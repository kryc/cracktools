#!/usr/bin/env python3
"""Generate hashcat .restore files for combinator / hybrid / mask attacks
using length-split wordlists.

The input directory should contain wordlists named "words_N.txt" where N
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

WORDLIST_RE = re.compile(r"^words_(\d+)\.txt$")


def discover_lengths(wordlist_dir: Path) -> list[int]:
    """Return sorted list of word lengths available in *wordlist_dir*."""
    lengths: list[int] = []
    for entry in wordlist_dir.iterdir():
        m = WORDLIST_RE.match(entry.name)
        if m:
            lengths.append(int(m.group(1)))
    lengths.sort()
    return lengths


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
# CLI
# ---------------------------------------------------------------------------

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Generate hashcat .restore files for combinator attacks.",
    )
    p.add_argument(
        "wordlist_dir",
        type=Path,
        help='Directory with "words_N.txt" wordlists split by length.',
    )
    p.add_argument(
        "-o", "--output",
        type=str,
        default="recrackedhc.txt",
        help="Hashcat output file for the restored sessions (default: recrackedhc.txt).",
    )
    p.add_argument(
        "--hash-file",
        type=str,
        default="uncracked.txt",
        help="Path to the file containing hashes.",
    )
    p.add_argument(
        "-m", "--min",
        type=int,
        default=8,
        dest="min_len",
        help="Minimum combined word length (default: 8).",
    )
    p.add_argument(
        "-M", "--max",
        type=int,
        default=31,
        dest="max_len",
        help="Maximum combined word length (default: 31).",
    )
    p.add_argument(
        "-O", "--output-dir",
        type=Path,
        default=None,
        help="Output directory for .restore files "
             "(default: $HOME/.local/share/hashcat/sessions/).",
    )
    p.add_argument(
        "--cwd",
        default=os.getcwd(),
        help="Working directory stored in each .restore file "
             "(default: current directory).",
    )
    p.add_argument(
        "--session-prefix",
        default="combo",
        help='Prefix for session names (default: "combo").',
    )
    p.add_argument(
        "--hybrid-threshold",
        type=int,
        default=3,
        help="Word lengths <= this value use a ?a mask instead of a "
             "wordlist, turning the attack into a hybrid (-a 6/-a 7). "
             "Pairs where both sides are below the threshold are "
             "skipped.  0 disables (default: 0).",
    )
    p.add_argument(
        "--hashcat-version",
        type=int,
        default=None,
        help="Hashcat version number for the restore header. "
             "Auto-detected from the newest existing .restore file, "
             f"falls back to {DEFAULT_HASHCAT_VERSION}.",
    )
    p.add_argument(
        "--hashcat-bin",
        default="hashcat",
        help="Path / name of the hashcat binary (default: hashcat).",
    )
    p.add_argument(
        "extra_flags",
        nargs="*",
        metavar="HASHCAT_FLAG",
        help="Extra hashcat flags forwarded into every restore file "
             '(e.g. -w 3 -O). Use -- before them if they start with "-".',
    )
    return p.parse_args(argv)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
    )
    args = parse_args()

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
    lengths = discover_lengths(wl_dir)
    if not lengths:
        log.error("No words_N.txt files found in %s.", wl_dir)
        sys.exit(1)
    log.info("Found wordlist lengths: %s", lengths)

    # Ensure all mask-only lengths 1..threshold are present even without
    # a corresponding wordlist file (they'll only ever be used as ?a masks).
    threshold = args.hybrid_threshold
    if threshold > 0:
        mask_lengths = set(range(1, threshold + 1))
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

        left_path = str(wl_dir / f"words_{left}.txt")
        right_path = str(wl_dir / f"words_{right}.txt")
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
            label = f"hybrid6 words_{left}.txt + {right_mask}"
        elif left_is_mask:
            # Hybrid: mask + wordlist  -a 7
            attack_mode = "7"
            session = f"{args.session_prefix}_{left}_{right}"
            tail_args = [left_mask, right_path]
            label = f"hybrid7 {left_mask} + words_{right}.txt"
        else:
            # Combinator  -a 1
            attack_mode = "1"
            session = f"{args.session_prefix}_{left}_{right}"
            tail_args = [left_path, right_path]
            label = f"combo words_{left}.txt + words_{right}.txt"

        if session in seen_sessions:
            continue
        seen_sessions.add(session)

        # Build the full hashcat argv
        argv = [
            args.hashcat_bin,
            "-a", attack_mode,
            "-m", "100",
            "-O",
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


if __name__ == "__main__":
    main()
