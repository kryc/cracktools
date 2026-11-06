# Crack Tools
A selection of password cracking tools

## Building

### Requirements
```bash
sudo apt install build-essential clang-21 cmake libssl-dev libicu-dev libgmp-dev
```

### Building
```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```

To build with AVX512 use
```bash
cmake -DAVX=512 -DCMAKE_BUILD_TYPE=Release ..
```

## Tools

### cracklist

Cracklist is a tool for finding passwords given a list of unsalted hashes. It takes a file containing a list of hashes and a file containing a list of possible words as input and outputs all hash:word combinations that match. If no wordlist is provided it reads passwords from stdin.

```bash
cracklist -o <output> <hashfile> [wordlist]
```

An optional `--threads (-t)` parameter can be used to specify the number of threads to use for lookup. This will reduce lookup time but result order will not match the order of input words.

Hashcat-compatible rules can be applied with `--rules <file>` or `-r <file>`. Every rule in the file is independently applied to every input word before hashing; rejected or invalid candidates are skipped.

The `--bitmask (-m)` flag can be used to configure the size of the hash lookup mask. A large size will consume more memory but increase the performance. A value in the range 1-32 (default: `16`).

For advanced usage and options see `cracklist --help`

### ruleanalyze

#### Optional CUDA backend

On the first configuration, CMake automatically detects a usable NVIDIA CUDA
compiler and enables the GPU backend when available. Without CUDA, the project
builds CPU-only. Override detection with `-DCRACKTOOLS_ENABLE_CUDA=ON` to require
CUDA or `-DCRACKTOOLS_ENABLE_CUDA=OFF` to disable it. The choice is cached; an
existing CPU-only build must explicitly enable the option to switch to CUDA.
CUDA 12.4 builds use C++20 with a compatible host compiler (GCC 13 is selected
when available). Default architectures include SM 89 and compute 90 PTX;
the driver can JIT that PTX on newer GPUs such as the RTX 5080. Override
`CMAKE_CUDA_ARCHITECTURES` and `CMAKE_CUDA_HOST_COMPILER` when needed.

Use `--backend cuda` to require GPU execution, `--backend auto` to use CUDA
when available, or `--backend cpu` (the default) for the reference evaluator.
Inputs, a compact Bloom filter, and GPU-supported rule instructions stay
resident on the device. The sorted length-bucketed 128-bit target index stays
in system RAM, not VRAM. The filter uses 12 bits per target and 8 probes (about
0.3% expected false positives for well-distributed hashes). Word length and
both fingerprint halves participate in filter lookup.

Definite misses are rejected on the GPU. Every potential hit is verified by
exact fingerprint and length lookup on the CPU before incrementing match
counts, recording target IDs, or qualifying a rule. Bloom false positives
therefore do not affect results. Exactness here is relative to the existing
128-bit fingerprint representation, not a plaintext collision check.

For 2.034 billion targets the filter occupies about 2.84 GiB, rather than
30.31 GiB for the fingerprints. Filter construction streams bounded chunks of
target fingerprints through a temporary upload buffer; it does not allocate
another full target copy on the host or GPU. Insufficient VRAM for the filter,
inputs, instructions, and execution buffers is still an error. Managed-memory
oversubscription is not used.

The initial GPU interpreter supports common literal transformations and reject
operations. Memory operations, character classes, saved-position arguments,
and malformed/unsupported rules run through the CPU evaluator; the backend
prints the GPU/fallback rule counts. CPU fallback uses `--threads`.

Both exhaustive ranking and `--qualify` are supported. Qualification preserves
the exact logical input prefix and unique-target threshold. Both modes run up
to 32 rules per batch over chunks of up to 32,768 inputs. Qualified rules are
retired independently, while unqualified rules continue through the remaining
inputs. Qualification starts with 4,096-input chunks and grows toward 32,768
to reduce speculative work for quickly qualifying rules.
Only Bloom-positive candidate fingerprints and small aggregate
counters are transferred; misses do not produce per-input transfer records.

Two independent CUDA streams and pinned host buffers allow the next chunk's
GPU execution to overlap transfers and CPU sorting/verification of the current
chunk. Qualification can speculatively evaluate the rest of the final chunk
plus one lookahead chunk, but these extra results do not affect logical
statistics. Compact hits are sorted by rule and input position; the GPU
recounts the final qualifying prefix to preserve exact applied, changed,
rejected, and error totals. Buffers can hold one hit per evaluated pair, with
explicit overflow checks rather than silently dropping hits.

Match deduplication and greedy selection remain on the CPU. Verification
summaries include downloaded result bytes so transfer reductions can be
observed. GPU per-rule timing is amortized batch wall time, not an isolated
kernel benchmark. CUDA differential tests are included in `rules_unittest`
when enabled and skip explicitly on machines without a usable GPU.

Use a Release build for performance runs. In AddressSanitizer-enabled Debug
builds, CUDA initialization may report out-of-memory despite free VRAM because
of conflicting virtual-address reservations; `ASAN_OPTIONS=protect_shadow_gap=0`
allows CUDA initialization on the tested system. CUDA-enabled CTest runs set
that option automatically.

```bash
ruleanalyze --backend cuda -i base.txt --qualify 10 \
	--valuable-rules qualified.rule rules.rule targets.txt
```

#### Ranking and qualification

In normal ranking mode, `ruleanalyze` exhaustively applies every Hashcat-compatible rule to the analysis inputs, records the distinct target words reached by each rule, and greedily ranks rules by marginal target coverage. A rule is selected only when it adds targets not already covered by higher-ranked rules.

```bash
ruleanalyze --input-wordlist base.txt rules.rule known-passwords.txt
```

Use `--min-gain <count>` (or `--bar`) to require each selected rule to add at least that many previously uncovered targets. `--top <count>` limits the ruleset size, and `--coverage <percent>` stops selection after the requested target coverage is reached.

```bash
ruleanalyze --input-wordlist base.txt --bar 10 --top 1000 \
	--coverage 90 --valuable-rules best.rule --output ranking.csv \
	rules.rule known-passwords.txt
```

The CSV report contains each selected rule's individual unique targets, marginal gain, and cumulative coverage. `--valuable-rules` writes the selected rules in ranking order, suitable for direct reuse.

For a fast first pass, `--qualify <count>` keeps any rule that reaches at least that many unique targets. Processing stops immediately for a rule when it qualifies; rules that never reach the threshold still scan the complete input list. Duplicate input hits for the same target count only once toward qualification.

```bash
ruleanalyze --input-wordlist base.txt --qualify 10 \
	--valuable-rules qualified.rule --output qualified.csv \
	gamut.rule known-passwords.txt
```

Qualification mode answers only whether each rule reaches the threshold. Its match counts, evaluated count, rate, and timing may be truncated, and it does not calculate marginal overlap, ranking, or cumulative coverage. `--qualify` cannot be combined with `--top`, `--bar`, `--min-gain`, `--min-matches`, or `--coverage`.

A recommended two-stage workflow is to qualify the full candidate ruleset first, then run normal marginal ranking against the smaller result:

```bash
ruleanalyze --input-wordlist base.txt --qualify 10 \
	--valuable-rules qualified.rule gamut.rule known-passwords.txt
ruleanalyze --input-wordlist base.txt --bar 10 --top 1000 \
	--valuable-rules best.rule --output ranking.csv \
	qualified.rule known-passwords.txt
```

The base input list is required and remains plaintext because rules transform those words. Targets are streamed into sorted FNV-1a 128 fingerprint vectors bucketed by decoded byte length; complete target plaintexts are not retained in memory.

Target fingerprints already present in the base list are excluded before scoring, so the ranking measures additional rule coverage. The 128-bit fingerprints make accidental collisions negligible at HIBP scale.

Use `--input-sample` with `--seed` for a reproducible faster approximation in either mode. Target fingerprints remain complete and are sorted and deduplicated within each length bucket.

### wordlist

`wordlist` is a multithreaded, ASCII-only C++ counterpart to the Python wordlist
tool. It supports `union`, `difference`, `intersection`, `symmetric-difference`,
`lower`, `upper`, `title`, and `clean`. Pass an operation and one or more regular
input files; output goes to stdout. Use `--threads` (or `-t`) to set a positive
worker count; the default uses hardware concurrency.

Set membership is based on decoded words, with duplicates within a file ignored.
Difference subtracts every later file from the first. Intersection requires a
word in every file. Multi-file symmetric difference retains words appearing in
an odd number of files. Case transformations combine and deduplicate all files.
`clean` lowercases words, converts trailing `'s` to `s`, replaces hyphenated words
with joined and split variants, then does the same for spaces. It drops empty
words and single characters outside `a`–`z`, preserving the Python script's
single-character filter rather than filtering every non-letter word.

Lines beginning with `#` are skipped unless `--include-comments` is supplied;
this option applies consistently to every operation. LF, CRLF, CR, and an
unterminated final line are supported. Valid nonempty `$HEX[]` payloads are
decoded before processing. Malformed wrappers and literal `$HEX[]` stay literal.
Hash prefixes are not stripped. Output uses the project's standard `Util::Hexlify`
policy: printable UTF-8 is retained unless the word contains a colon, ends in
whitespace, or begins with `$HEX[`. Those words, control characters, and invalid
or non-printable UTF-8 are hex-encoded; empty words become blank lines. Literal
malformed wrappers are therefore escaped on output rather than confused with
encoded data. Results are sorted **after** output escaping. This uses the
project's escaping policy rather than the Python script's output policy.

No ICU dependency is added. **TODO: ICU-backed Unicode decoding, casing,
titlecasing, and transliteration.** For now, non-ASCII bytes are preserved,
ASCII letters alone change case, and `clean` does not add transliterated variants.
`--encoding` accepts `ascii`, `utf8`, and `utf-8` as byte-preserving modes, not
Unicode decoding implementations; other encodings are rejected explicitly.
Use `--verbose` for summaries on stderr and `--` for dash-prefixed input paths.

Input is memory-mapped and split into newline-aligned tasks, so large individual
files can use several workers. Worker-local batches feed hash partitions;
partitions independently reduce distinct-file membership and sort their escaped
results. One `libdispatchqueue` worker pool is reused across both phases, with
atomic-index work distribution and explicit idle barriers before mappings are
released or reduction begins. Workers are drained and joined before output.
A buffered k-way merge produces deterministic output. Processing is
in-memory: records, sorted results, and mapped pages consume RAM; no external
sort or hard memory limit is provided. Do not modify or truncate inputs while
the command runs, and do not redirect output onto an input file.

### wordlistsort

`wordlistsort` normalizes and sorts a word list. It decodes valid `$HEX[]` input before sorting by decoded byte value, then applies standard `$HEX[]` encoding to output words where required.

```bash
wordlistsort words.txt > sorted.txt
```

Use `--output <file>` (or `-o`) to write directly to an output file instead of stdout. This also displays an updating status line on stderr with counting, reading, sorting, and writing stages, including processed/written word counts. Status output is disabled when writing to stdout.

```bash
wordlistsort -o sorted.txt words.txt
```

Use `--length` (or `-l`) to sort by decoded byte length first (shortest first), then by decoded byte content for words of equal length.

Use `--unique` (or `-u`) to suppress duplicate decoded words during the final output step. For example, `A` and `$HEX[41]` produce just one `A`. This works with either sort order and with memory-mapped input; duplicates are preserved by default.

When estimated input storage (file size plus one `std::string` per newline) reaches 256 MiB, `wordlistsort` automatically uses memory-mapped input and sorts `std::string_view` entries instead of copying every word. Use `--mmap` to force this mode for smaller files. Plain words reference the mapped file; valid `$HEX[]` words are decoded into separate stable storage. Both sorting orders and output encoding remain unchanged.

Mapped sorting still needs memory for the view index (typically 16 bytes per word on 64-bit systems), decoded words, and resident file pages; it is not an external sort or a hard memory limit. Do not modify or truncate the input while sorting. Output paths referring to the input file, including hard links and symlinks, are rejected.

For `hash:word` input, a likely hexadecimal or crypt hash prefix is detected automatically and discarded before decoding and sorting. Other colon-containing words are preserved.

```bash
wordlistsort cracked.txt > sorted.txt
```

### crackdb++

`CrackDB++` is an unsalted password hash lookup application and an example of a time-memory tradeoff tool. It works by storing the hash of every word in the input wordlist in a set of files on disk, then uses efficient lookup algorithms to recover provided hashes later. It is extremely disk- and memory-efficient storing only a small portion of each word's hash and typically recovers many thousands of hashes per second.

First you need to build the database with a seed wordlist. By default it will use all available CPUs for hashing the input words, but this can be toggled using `-t`:
```bash
crackdb++ <database folder> build <wordlist>
```

You can then query some stats about the database if you wish:
```bash
crackdb++ <database folder> info
```

Finally you can easily perform a lookup of a hash thus:
```bash
crackdb++ <database folder> crack <hash>
```

Example:
```bash
crackdb++ ~/crackdb/ crack aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d
CrackDB++ by Kryc
aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d:hello
```