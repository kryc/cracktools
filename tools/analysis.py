#!/usr/bin/env python3
"""Analyse character frequencies and length distributions in wordlists."""

import argparse
import os
import re
import sys
import unicodedata
from collections import Counter

# Unicode bidi categories that indicate right-to-left characters
_RTL_BIDI = {'R', 'AL', 'AN'}


def _display_char(ch: str) -> str:
    """Return a safe display representation of a character.

    Non-printable characters are shown as repr(), RTL characters as U+XXXX
    to prevent bidirectional text from breaking column alignment.
    """
    if not ch.isprintable():
        return repr(ch)
    if unicodedata.bidirectional(ch) in _RTL_BIDI:
        return f'U+{ord(ch):04X}'
    return ch


def _ishex(word: str) -> bool:
    """Check if a word is in $HEX[...] format."""
    return (len(word) > 6 and len(word) % 2 == 0
            and word.startswith('$HEX[') and word.endswith(']')
            and all(c in '0123456789abcdefABCDEF' for c in word[5:-1]))


def _unhexlify(word: str, encoding: str = 'utf-8') -> str:
    """Decode a $HEX[...] encoded word to a string."""
    if _ishex(word):
        return bytes.fromhex(word[5:-1]).decode(encoding, errors='ignore')
    return word


def analyse(path):
    """Read *path* and return character/length frequency counters and word count."""
    char_freq = Counter()
    char_word_freq = Counter()  # how many words contain each character
    length_freq = Counter()

    size = os.path.getsize(path)
    processed_bytes = 0
    processed_lines = 0

    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw_line in fh:
            processed_bytes += len(raw_line.encode("utf-8", errors="replace"))
            processed_lines += 1

            if processed_lines % 10000 == 0:
                print(
                    f"\r{processed_lines} lines {processed_bytes/size:.2%}",
                    end="",
                    file=sys.stderr,
                )

            line = raw_line.strip()

            # Strip leading hash prefix (e.g. "hash:password")
            m = re.match(r"^[0-9a-fA-F]+:(.*?)$", line)
            if m is not None:
                line = m.group(1)

            # Decode $HEX[...] encoded words
            line = _unhexlify(line)

            length_freq[len(line)] += 1

            seen = set()
            for ch in line:
                char_freq[ch] += 1
                seen.add(ch)
            for ch in seen:
                char_word_freq[ch] += 1

    print(f"\r({100:.2%}), {processed_lines} lines", file=sys.stderr)
    return char_freq, char_word_freq, length_freq, processed_lines


def filter_counts(char_freq, char_word_freq, length_freq, total_words,
                  min_count=None, max_count=None, min_pct=0.09):
    """Filter counters by absolute count range and minimum word-percentage.

    Returns filtered copies of (char_freq, char_word_freq, length_freq).
    """
    pct_threshold = total_words * min_pct / 100 if total_words else 0

    def _keep_char(ch):
        wcount = char_word_freq[ch]
        if wcount < pct_threshold:
            return False
        count = char_freq[ch]
        if min_count is not None and count < min_count:
            return False
        if max_count is not None and count > max_count:
            return False
        return True

    kept = {ch for ch in char_freq if _keep_char(ch)}

    f_char_freq = Counter({ch: char_freq[ch] for ch in kept})
    f_char_word_freq = Counter({ch: char_word_freq[ch] for ch in kept})

    # Filter lengths by absolute count range
    f_length_freq = Counter()
    for length, count in length_freq.items():
        if min_count is not None and count < min_count:
            continue
        if max_count is not None and count > max_count:
            continue
        f_length_freq[length] = count

    return f_char_freq, f_char_word_freq, f_length_freq


def format_results(char_freq, char_word_freq, length_freq, total_words):
    """Return a formatted string of the analysis results.

    Characters appearing in fewer than *min_pct* % of words are omitted.
    """
    lines = []

    lines.append("=== Length Frequencies ===")
    for length, count in sorted(length_freq.items()):
        lines.append(f"  {length:>4d}: {count}")

    lines.append("")
    lines.append("=== Character Frequencies ===")
    for ch, count in char_freq.most_common():
        display = _display_char(ch)
        lines.append(f"  {display:>6s}: {count}")

    lines.append("")
    lines.append("=== Charset Coverage ===")
    lines.append(f"  Total words: {total_words}")
    lines.append("")
    lines.append(f"  {'Char':>6s}  {'Words':>10s}  {'% Words':>7s}")
    lines.append(f"  {'----':>6s}  {'-----':>10s}  {'-------':>7s}")

    # Characters sorted by how many words they appear in
    for ch, wcount in char_word_freq.most_common():
        pct = wcount / total_words * 100 if total_words else 0
        display = _display_char(ch)
        lines.append(f"  {display:>6s}  {wcount:>10d}  {pct:>6.2f}%")

    # Summary grouped by character class
    lines.append("")
    lines.append("=== Charset Summary ===")
    classes = {
        "lowercase": set(),
        "uppercase": set(),
        "digits": set(),
        "symbols": set(),
    }
    for ch in char_word_freq:
        if ch.isascii() and ch.islower():
            classes["lowercase"].add(ch)
        elif ch.isascii() and ch.isupper():
            classes["uppercase"].add(ch)
        elif ch.isascii() and ch.isdigit():
            classes["digits"].add(ch)
        else:
            classes["symbols"].add(ch)

    for cls, charset_group in classes.items():
        if not charset_group:
            continue
        counts = [char_word_freq[ch] for ch in charset_group]
        cmin_pct = min(counts) / total_words * 100 if total_words else 0
        cmax_pct = max(counts) / total_words * 100 if total_words else 0
        chars_sorted = sorted(charset_group)
        display_chars = " ".join(_display_char(c) for c in chars_sorted)
        lines.append(f"  {cls:<10s}: {display_chars}")
        lines.append(f"  {'':10s}  per-char coverage: {cmin_pct:.2f}% - {cmax_pct:.2f}%")

    return "\n".join(lines)


def compute_cumulative_coverage(path, char_word_freq, total_words):
    """Second pass over the file to compute cumulative charset coverage.

    For each character in greedy order (most-common-in-words first), determines
    at which step each word becomes fully representable. Uses O(unique_chars)
    memory instead of storing per-word character sets.
    """
    # Build greedy order and assign each char a step index
    greedy_order = [ch for ch, _ in char_word_freq.most_common()]
    char_step = {ch: i for i, ch in enumerate(greedy_order)}
    num_steps = len(greedy_order)

    # covered_at[i] = number of words that become fully covered at step i
    covered_at = [0] * num_steps
    uncovered = 0  # words containing chars not in char_word_freq at all

    print("\rComputing cumulative coverage...", end="", file=sys.stderr)

    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw_line in fh:
            line = raw_line.strip()
            m = re.match(r"^[0-9a-fA-F]+:(.*?)$", line)
            if m is not None:
                line = m.group(1)
            line = _unhexlify(line)

            if not line:
                # Empty word is covered at step 0
                if num_steps > 0:
                    covered_at[0] += 1
                continue

            # Find the latest step among this word's characters
            max_step = -1
            word_covered = True
            for ch in set(line):
                step = char_step.get(ch)
                if step is None:
                    word_covered = False
                    break
                if step > max_step:
                    max_step = step
            if word_covered and max_step >= 0:
                covered_at[max_step] += 1
            else:
                uncovered += 1

    print("\r" + " " * 40 + "\r", end="", file=sys.stderr)

    # Build cumulative results
    lines = []
    lines.append("=== Cumulative Charset Coverage ===")
    lines.append(f"  Total words: {total_words}")
    lines.append("")
    lines.append(f"  {'Char':>6s}  {'Charset Size':>12s}  {'Words Covered':>13s}  {'% Covered':>9s}  {'Delta':>7s}")
    lines.append(f"  {'----':>6s}  {'------------':>12s}  {'-------------':>13s}  {'---------':>9s}  {'-----':>7s}")

    cumulative = 0
    for i, ch in enumerate(greedy_order):
        delta = covered_at[i]
        cumulative += delta
        pct = cumulative / total_words * 100 if total_words else 0
        delta_pct = delta / total_words * 100 if total_words else 0
        display = _display_char(ch)
        lines.append(
            f"  {display:>6s}  {i + 1:>12d}  {cumulative:>13d}  {pct:>8.2f}%  +{delta_pct:.2f}%"
        )

    if uncovered:
        lines.append(f"  ... {uncovered} words contain characters not in the filtered charset")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Analyse character and length frequencies in a wordlist."
    )
    parser.add_argument("wordlist", help="Path to the wordlist file to analyse.")
    parser.add_argument(
        "-o", "--output", help="Write results to this file instead of stdout."
    )
    parser.add_argument(
        "-t", "--threshold", type=float, default=0.09, metavar="PCT",
        help="Hide characters appearing in fewer than this %% of words (default: 0.09)."
    )
    parser.add_argument(
        "-m", "--min", type=int, default=None, metavar="N",
        help="Hide entries with a count below N."
    )
    parser.add_argument(
        "-M", "--max", type=int, default=None, metavar="N",
        help="Hide entries with a count above N."
    )
    args = parser.parse_args()
    char_freq, char_word_freq, length_freq, total_words = analyse(args.wordlist)
    char_freq, char_word_freq, length_freq = filter_counts(
        char_freq, char_word_freq, length_freq, total_words,
        min_count=args.min, max_count=args.max, min_pct=args.threshold,
    )
    report = format_results(char_freq, char_word_freq, length_freq, total_words)
    report += "\n\n" + compute_cumulative_coverage(
        args.wordlist, char_word_freq, total_words
    )

    if args.output:
        with open(args.output, "w", encoding="utf-8") as out:
            out.write(report + "\n")
        print(f"Results written to {args.output}", file=sys.stderr)
    else:
        print(report)


if __name__ == "__main__":
    main()
