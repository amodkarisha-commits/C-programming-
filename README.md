# Multithreaded File Compression Tool

A high-performance file compressor/decompressor written in C++17 that uses a
custom thread pool to compress independent data blocks in parallel, delivering
significant throughput gains on multi-core machines.

---

## Architecture

```
Input File
    │
    ▼
┌───────────────────────────────────────────┐
│  Split into fixed-size blocks (1 MB each) │
└───────────────────────────────────────────┘
    │           │           │           │
    ▼           ▼           ▼           ▼
 Thread 0    Thread 1    Thread 2    Thread 3
 zlib        zlib        zlib        zlib
 deflate     deflate     deflate     deflate
    │           │           │           │
    └───────────┴───────────┴───────────┘
                    │
                    ▼
        ┌───────────────────────┐
        │  .mtfl container file │
        │  ─────────────────    │
        │  FileHeader (24 B)    │
        │  BlockDesc[] (16 B×N) │
        │  compressed data …    │
        └───────────────────────┘
```

### Key Design Decisions

| Concern | Decision | Rationale |
|---|---|---|
| Parallelism unit | 1 MB block | Large enough to amortize thread-spawn; small enough to keep all cores busy |
| Compression codec | zlib (deflate) | Ubiquitous, battle-tested, C-linkage, no extra deps |
| Thread model | Custom thread pool | Avoids repeated `std::thread` create/destroy cost |
| Decompression | Also parallelised | Block offsets in header → workers seek independently |
| Integrity | MD5 in demo | zlib CRC32 per block is implicit |

---

## File Format (.mtfl)

```
Offset   Size  Field
──────────────────────────────────────────────────
0        4     Magic  = 0x4D54464C  ("MTFL")
4        4     Version = 1
8        8     original_size  (uint64)
16       4     num_blocks     (uint32)
20       4     nominal block size (uint32)
24       N×16  BlockDesc array
                  compressed_offset (uint64)
                  compressed_size   (uint32)
                  original_size     (uint32)
24+N×16  …     raw zlib-compressed block data
```

---

## Building

```bash
# Requirements: g++ ≥ 7, zlib-dev
sudo apt-get install -y zlib1g-dev   # Debian / Ubuntu

g++ -O2 -std=c++17 -pthread compressor.cpp -lz -o compressor
```

---

## Usage

### Compress
```bash
./compressor compress <input> <output.mtfl> [threads]

# Examples
./compressor compress video.mp4 video.mtfl        # auto-detect cores
./compressor compress biglog.txt biglog.mtfl 8    # force 8 threads
```

### Decompress
```bash
./compressor decompress <input.mtfl> <output> [threads]

./compressor decompress video.mtfl video_out.mp4
```

### Benchmark
Runs compress + decompress across thread counts 1→2→4→8→…→N and prints a
speedup table:

```bash
./compressor benchmark <input> [max_threads]

./compressor benchmark testfile.dat 8
```

Sample output (8-core machine):

```
╔══════════════════════════════════════════════════════════╗
║        Multithreaded Compression Benchmark               ║
╚══════════════════════════════════════════════════════════╝
  Input file     : testfile.dat
  Hardware cores : 8
  Testing up to  : 8 threads

Threads         Compress   C-Spd(MB/s)    Decompress   D-Spd(MB/s)      Ratio%
------------------------------------------------------------------------------
1               920.0 ms          21.6      210.1 ms          94.6        42.8
2               476.3 ms          41.8      108.9 ms         182.6        42.8
3               328.4 ms          60.6       74.2 ms         268.0        42.8
4               253.1 ms          78.6       57.0 ms         348.7        42.8
8               157.8 ms         126.1       33.4 ms         595.0        42.8

  Speedup vs 1-thread baseline:

Threads       Compress Speedup    Decompress Speedup
----------------------------------------------------
1                        1.00x                 1.00x
2                        1.93x                 1.93x
3                        2.80x                 2.83x
4                        3.64x                 3.69x
8                        5.83x                 6.29x
```

---

## Performance Notes

* **Why decompression is faster than compression**: inflate is ~4–6× cheaper
  per byte than deflate. Both still scale linearly with thread count because
  blocks are fully independent.

* **Single-core machine**: speedup will be ≈1×. The thread pool still
  demonstrates the architecture correctly; gains appear immediately on ≥2 cores.

* **Tuning block size**: decrease `BLOCK_SIZE` (e.g. 256 KB) for more granular
  work units on many-core machines; increase for better per-block compression
  ratio (less header overhead).

* **Compression level**: `ZLIB_LEVEL = Z_BEST_SPEED` (level 1) maximises
  throughput. Switch to `Z_DEFAULT_COMPRESSION` (level 6) or `Z_BEST_COMPRESSION`
  (level 9) for smaller files at the cost of CPU time.

---

## Thread Pool Implementation

The embedded `ThreadPool` class:
1. Spawns N worker threads at construction.
2. Workers sleep on a condition variable until work arrives.
3. `enqueue()` pushes a `std::function<void()>` onto the shared queue and
   notifies one worker.
4. Destruction sets `stop_=true`, broadcasts, and joins all workers.

Compression and decompression tasks are identical in shape:
```cpp
pool.enqueue([b, &raw, &comp_blocks, …]() {
    compress_block(raw.data() + offset, bsz,
                   comp_blocks[b].data(), bound);
});
```
A `std::atomic<size_t> done` counter is polled (with microsecond sleeps) until
all N blocks complete before the pool is destroyed.
---------------------------------------------------------------------------------------------------------------
___________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________________
🔗DISCRIPTION OF ENTIRE INTERSHIP TASKS

This repository contains the complete solutions for the CODTECH C++ Internship Tasks. The project includes implementations of all four assigned tasks with proper code structure, comments, and functionality demonstration.

Implemented Tasks:

1. File Management Tool – Developed a C++ application capable of reading, writing, and appending data to text files using file handling concepts.
2. Multithreaded File Compression Tool – Created a compression and decompression utility using multithreading techniques to improve execution performance.
3. Snake Game – Designed and developed a graphical Snake game using C++ libraries with interactive gameplay, increasing difficulty levels, and sound effects.
4. Compiler Design Basics – Implemented a basic compiler that parses and evaluates arithmetic expressions efficiently.

All tasks are organized systematically in this repository along with source code, output screenshots, and required documentation. Proper commenting standards and coding practices have been followed throughout the project.
