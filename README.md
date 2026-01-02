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

The `--bitmask (-m)` flag can be used to configure the size of the hash lookup mask. A large size will consume more memory but increase the performance. A value in the range 1-32 (default: `16`).

For advanced usage and options see `cracklist --help`

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