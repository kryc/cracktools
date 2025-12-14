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