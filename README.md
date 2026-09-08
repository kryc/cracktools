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

`ruleanalyze` measures how often each Hashcat-compatible rule transforms one word into a different word that also appears in the supplied word list. The word list is loaded into memory and sorted before analysis.

```bash
ruleanalyze [--ascending|--descending] [--sort matches|rule] rules.rule words.txt
```

The default CSV report is sorted by descending match count. Use `--threads` to control parallel rule analysis and `--output` to write the report to a file.

Use `--generate <length>` to analyze generated inputs through a separate maximum length while retaining `words.txt` as the lookup set. The `--min` and `--max` options only filter the lookup word list.

```bash
ruleanalyze --generate 4 --charset lower --max 16 rules.rule words.txt
```

Use `--input-wordlist <file>` (or `-i`) to apply rules to a separate input word list while retaining `words.txt` as the lookup set. `--input-wordlist` and `--generate` cannot be combined.

```bash
ruleanalyze --input-wordlist base.txt rules.rule known-passwords.txt
```

Use `--match-limit <count>` to stop evaluating an individual rule once it reaches the requested number of matches. This can substantially reduce analysis time when the goal is to retain rules that meet a minimum usefulness threshold.
Statistics for a stopped rule reflect only the inputs evaluated before it reached the limit.

Use `--input-sample` (or `--sample`) to reduce rule evaluations, and `--wordlist-sample` to independently reduce the lookup set before sorting and indexing.

Reports distinguish evaluated, applied, changed, matched, unique matched, rejected, and invalid applications, with match-rate, coverage, and timing columns. Use `--seed` for reproducible samples and report filters such as `--only-zero`, `--min-matches`, `--min-rate`, `--errors-only`, and `--changed-only`. `--valuable-rules <file>` writes rules that produced at least one known word as a reusable rule file.

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