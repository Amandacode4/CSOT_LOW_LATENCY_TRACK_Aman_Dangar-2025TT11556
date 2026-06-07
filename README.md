# CSoT Low Latency Track - Week 1 Submission

**Author:** Aman Dangar (2025TT11556)  
**Final Score:** 177.3  
**p50 Latency:** 18 ns  
**p99 Latency:** 38 ns  
**Throughput:** 20.06 M ticks/s  

## Overview
This repository contains a Week 1 submission for the CSoT Low Latency Track. The objective is to implement the moving-average strategy defined in the specification and execute it efficiently on the provided tick stream.

## Implementation Notes (`spec_strategy.cpp`)
The implementation focuses on reducing overhead in frequently executed code paths.

Key ideas used:

1. **Fixed-size state storage** – Symbol state is stored in preallocated arrays to avoid repeated dynamic allocation during execution.
2. **Compiler-assisted optimization** – The build configuration enables aggressive optimization settings and makes use of compiler intrinsics where appropriate.
3. **Custom memory pool** – A simple bump-style allocator is included so allocations can be served from a preallocated memory region.
4. **Contiguous data layout** – Frequently accessed data structures are stored in cache-friendly layouts to reduce access costs.

The implementation follows the competition strategy interface and is compiled as a shared library.

## Building

```bash
x86_64-unknown-linux-gnu-g++ -std=c++20 -O3 -march=x86-64-v3 -flto -ffast-math -DNDEBUG -fno-plt -shared -fPIC -Wall -Wextra -Wpedantic -I"include" -o "spec_strategy.so" "spec_strategy.cpp"
```

## Data Generation

A synthetic dataset can be generated with:

```bash
python3 data/gen.py --rows 1000000 --out synthetic_large.csv
```

The generator produces deterministic output when the same seed is used, making benchmark runs reproducible.
