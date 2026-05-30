# LSM-Tree Key-Value Store

A from-scratch implementation of a Log-Structured Merge-Tree (LSM-Tree) based key-value store in C++17, inspired by the storage engines powering LevelDB, RocksDB, and Apache Cassandra.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Features](#features)
- [Project Structure](#project-structure)
- [How It Works](#how-it-works)
  - [MemTable (Skip List)](#memtable-skip-list)
  - [Write-Ahead Log (WAL)](#write-ahead-log-wal)
  - [SSTable](#sstable)
  - [Bloom Filter](#bloom-filter)
  - [Sparse Index](#sparse-index)
  - [Levels & Compaction](#levels--compaction)
  - [Manifest](#manifest)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Build](#build)
  - [Run](#run)
- [Benchmarks](#benchmarks)
- [Design Decisions](#design-decisions)
- [Known Limitations & Future Work](#known-limitations--future-work)

---

## Overview

An LSM-Tree is a write-optimized data structure that powers many modern NoSQL databases. Rather than updating data in place (like a B-Tree), all writes go first to an in-memory buffer and are later flushed sequentially to disk — making writes extremely fast and disk-friendly.

This project implements the full LSM-Tree pipeline end-to-end:

```
Write → MemTable (Skip List + WAL) → SSTable (L0) → Compaction → L1 … L4
Read  → MemTable → L0 SSTables → L1 … (Bloom Filter + Sparse Index accelerated)
```

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    Write Path                        │
│                                                     │
│   put(key, value)                                   │
│        │                                            │
│        ▼                                            │
│   ┌──────────┐     ┌─────────────┐                 │
│   │MemTable  │────▶│  WAL (disk) │  (crash safety) │
│   │(SkipList)│     └─────────────┘                 │
│   └────┬─────┘                                      │
│        │ size >= threshold                          │
│        ▼                                            │
│   ┌──────────┐                                      │
│   │ SSTable  │──▶ Level 0 ──▶ Level 1 ──▶ ... L4  │
│   │  (disk)  │         (compaction on overflow)     │
│   └──────────┘                                      │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                    Read Path                         │
│                                                     │
│   get(key)                                          │
│        │                                            │
│        ▼                                            │
│   MemTable ──miss──▶ L0 SSTables                   │
│                           │                         │
│                    Bloom Filter (skip if absent)    │
│                           │                         │
│                    Sparse Index (seek to block)     │
│                           │                         │
│                    Binary scan within block         │
│                           │                         │
│                    L1 … L4 (if not found)           │
└─────────────────────────────────────────────────────┘
```

---

## Features

- **Skip List MemTable** — probabilistic, O(log n) average insert/search, with configurable max levels (default: 5) and promotion probability (p = 0.5)
- **Write-Ahead Log (WAL)** — binary-encoded log for crash recovery; replayed into MemTable on restart
- **SSTable Serialization** — sorted, binary-encoded on-disk files with key-value entries and tombstone markers for deletes
- **Bloom Filter** — dual-hash FNV-1a with 7 hash functions and 10 bits/key; drastically cuts unnecessary disk reads for missing keys
- **Sparse Index** — one index entry per 20-byte block boundary; enables fast seeking within large SSTable files
- **Multi-level Compaction** — 5 levels with 5× size amplification per level; overlapping SSTable ranges are merged using a min-heap
- **Tombstone-based Deletes** — logical deletes via tombstone markers, cleaned up during compaction
- **Manifest file** — tracks the LSM level structure across runs
- **C++17** — uses `std::filesystem`, smart pointers (`std::unique_ptr`, `std::shared_ptr`), and structured bindings throughout

---

## Project Structure

```
LSM-Tree-Key-Value-Store/
├── include/
│   ├── MemTable.h        # Skip list MemTable + WAL declarations
│   ├── SSTable.h         # SSTable, Bloom filter, sparse index declarations
│   ├── Levels.h          # Level management + compaction declarations
│   └── filecounter.h     # Global file counter / naming utility
├── srcs/
│   ├── main.cpp          # Entry point, benchmark driver
│   ├── MemTable.cpp      # Skip list insert, search, WAL read/write
│   ├── SSTable.cpp       # SSTable serialization, Bloom filter, key search
│   └── Levels.cpp        # Compaction logic, level management, manifest
├── obj/                  # Compiled object files (generated)
├── makefile
└── benchmark_results.csv # Recorded throughput measurements
```

---

## How It Works

### MemTable (Skip List)

Incoming writes land in an in-memory skip list. Each node (`Element`) holds an `Entry` (key, value, tombstone flag) and a vector of forward pointers — one per level. Node height is determined probabilistically at insertion (coin-flip with p = 0.5, up to `maxlevel = 5`).

The MemTable tracks its own byte size. Once `size >= maxsize` (default: 2000 bytes), it is flushed to an SSTable and a fresh MemTable is created.

### Write-Ahead Log (WAL)

Every insert is appended to a binary WAL file before it is considered committed. The format per entry is:

```
[key_length: 1 byte][key: N bytes][val_length: 1 byte][value: M bytes][tombstone: 1 byte]
```

On startup, if a WAL file exists and is non-empty, it is replayed into the MemTable to restore in-flight writes that survived a crash. The WAL is deleted once its MemTable is successfully flushed to an SSTable.

### SSTable

SSTables are immutable, sorted binary files written from a MemTable flush. Each file's layout is:

```
┌──────────────────────────────┐
│  Data records (sorted)       │  ← [key_len][key][val_len][value][tombstone]
├──────────────────────────────┤
│  Bloom filter buckets        │  ← raw bytes
├──────────────────────────────┤
│  Sparse index                │  ← [key_len][key][offset: 8 bytes] per entry
├──────────────────────────────┤
│  Footer (16 bytes)           │  ← bloom_offset | index_offset
└──────────────────────────────┘
```

Each SSTable also stores its `startkey` and `endkey` in memory so that levels can skip SSTables whose key ranges don't overlap a query.

### Bloom Filter

When writing an SSTable, a Bloom filter is constructed with `nkeys * 10` bits and 7 hash functions. Two independent FNV-1a hashes (with different seeds) are combined using double hashing:

```
bucket_index = (h1 + i * h2) % total_bits    for i in 0..6
```

At read time, if the Bloom filter says the key is absent, the SSTable is skipped entirely — no disk I/O needed. This is especially effective for workloads with many reads for non-existent keys (see benchmarks below).

### Sparse Index

An index entry is written for the first key in every 20-byte block of the data section. At read time, the index is scanned to find the largest indexed key ≤ the target key, and the file pointer jumps to that block offset. The linear scan then continues only within that small block.

### Levels & Compaction

The store maintains 5 levels. Level sizes follow a 5× geometric progression:

| Level | Max size       |
|-------|----------------|
| L0    | ~10 KB         |
| L1    | ~50 KB         |
| L2    | ~250 KB        |
| L3    | ~1.25 MB       |
| L4    | ~6.25 MB       |

When a level exceeds its size budget, compaction is triggered. The compactor:

1. Identifies the SSTable to compact and finds all SSTables in the next level whose key ranges overlap.
2. Streams all entries from those SSTables into a min-heap (`std::priority_queue`), ordered by key, with file ID as a tiebreaker (newer files win).
3. Writes the merged, deduplicated, sorted output as a new SSTable at `level + 1`.
4. Removes the old SSTable files from disk.
5. Recursively triggers compaction at `level + 1` if needed.

Tombstones are preserved through compaction until they are no longer needed for correctness (full deletion support is a planned improvement).

### Manifest

A manifest file is created on initialization to track the state of levels, enabling recovery and inspection of the tree structure across restarts.

---

## Getting Started

### Prerequisites

- **g++** with C++17 support (GCC 8+ recommended)
- **Linux / macOS** (uses POSIX filesystem APIs and `std::filesystem`)

### Build

```bash
git clone https://github.com/kushallt/LSM-Tree-Key-Value-Store.git
cd LSM-Tree-Key-Value-Store
make
```

This compiles all sources under `srcs/` into `obj/` and links to `main.exe`.

To clean build artifacts:

```bash
make clean
```

### Run

```bash
./main.exe
```

The default `run()` in `main.cpp` performs a benchmark: it inserts 10,000 key-value pairs and then reads 10,000 keys, printing the resulting level layout and logging throughput to `benchmark_results.csv`.

To experiment with interactive inserts, lookups, and deletes, uncomment the interactive loop in `srcs/main.cpp`:

```cpp
// Uncomment in main.cpp → run():
bool cont;
int add;
while (true) {
    // 1 = insert, 0 = delete, 2 = search
    ...
}
```

---

## Benchmarks

All benchmarks were run on the same machine. Results are logged in `benchmark_results.csv`.

### Write Throughput

| Number of Writes | Throughput (writes/sec) |
|-----------------|------------------------|
| 500             | 1,317                  |
| 5,000           | 384                    |
| 10,000          | 351                    |

Write throughput decreases as more compaction work kicks in at higher write volumes — a known characteristic of LSM-Tree workloads.

### Read Throughput

| Workload                               | Mode                  | Throughput (reads/sec) |
|----------------------------------------|-----------------------|------------------------|
| 10,000 existing keys                   | Without Bloom filter  | 20,373                 |
| 10,000 existing keys                   | With Bloom filter     | 20,209                 |
| 5,000 existing + 5,000 non-existing    | Without Bloom filter  | 22,596                 |
| 5,000 existing + 5,000 non-existing    | With Bloom filter     | **32,977**             |

The Bloom filter provides the largest benefit for mixed workloads with non-existent keys — delivering a **~46% read throughput improvement** by avoiding unnecessary SSTable disk scans.

---

## Design Decisions

**Skip list over red-black tree for MemTable** — Skip lists offer comparable O(log n) average complexity with simpler, cache-friendlier implementation and no rebalancing logic.

**FNV-1a double hashing for Bloom filter** — FNV-1a is fast and produces good distribution. Double hashing (two seeds → 7 virtual hash functions) avoids the overhead of 7 independent hash computations.

**Sparse index over full index** — Storing one index entry per 20-byte block trades a small linear scan within a block for a dramatically smaller index that fits in memory and reduces I/O.

**`std::shared_ptr` for SSTables** — SSTables are referenced from both the level vectors and the compaction routines; shared ownership simplifies lifetime management during concurrent compaction (a future extension).

**Footer-based file layout** — Storing the bloom offset and index offset at a fixed footer position allows the reader to seek directly to either structure without scanning the data section.

---

## Known Limitations & Future Work

- **No concurrency** — reads and writes are single-threaded; adding a read-write lock per level would enable concurrent access
- **Tombstone accumulation** — deleted keys leave tombstone markers through all levels until they reach the deepest level; a purge pass at `maxlevel` is not yet implemented
- **Key/value size** — currently encoded with `uint8_t` length prefixes, limiting keys and values to 255 bytes each
- **No iterator / range scan** — a range query API (`scan(start, end)`) would be a natural next step given the sorted SSTable structure
- **No LRU block cache** — frequently accessed SSTable blocks are re-read from disk on every lookup; an in-memory block cache would significantly improve read latency for hot keys
- **Compaction strategy** — currently implements size-tiered compaction; leveled compaction (as in LevelDB) could be explored for better read amplification

---

## References

- [The Log-Structured Merge-Tree (LSM-Tree)](https://www.cs.umb.edu/~poneil/lsmtree.pdf) — O'Neil et al., 1996
- [LevelDB Design](https://github.com/google/leveldb/blob/main/doc/impl.md)
- [Designing Data-Intensive Applications](https://dataintensive.net/) — Chapter 3: Storage and Retrieval
