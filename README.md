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

### wordlistsort

`wordlistsort` normalizes and sorts a word list. It decodes valid `$HEX[]` input before sorting by decoded byte value, then applies standard `$HEX[]` encoding to output words where required.

```bash
wordlistsort words.txt > sorted.txt
```

Use `--output <file>` (or `-o`) to write directly to an output file instead of stdout.

```bash
wordlistsort -o sorted.txt words.txt
```

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