# Custom Memory Allocator (`mymalloc` / `myfree`)

A from-scratch implementation of a `malloc`/`free`-style general-purpose
allocator in C++, built to understand how real allocators manage memory
internally — free lists, block metadata, splitting, and coalescing.

Unlike a simple bump/arena allocator, this one supports **freeing individual
allocations in any order**, by maintaining an explicit free list of blocks
and merging adjacent free blocks back together.

## How it works

- **Backing memory**: a single 1 MB memory pool is requested once from the OS
  via `mmap` (`PROT_READ|PROT_WRITE`, `MAP_PRIVATE|MAP_ANONYMOUS`) in
  `init_pool()`. All blocks live contiguously inside this pool, which is what
  makes coalescing possible.
- **Block metadata**: every allocation is preceded by a `block` header
  storing its `size`, a `next` pointer (intrusive linked list), and an
  `is_free` flag. The pointer returned to the caller points *after* this
  header (`meta + 1`), and `myfree` walks backward (`(block*)ptr - 1`) to
  recover it.
- **Allocation (`mymalloc`)**:
  1. Size is rounded up to pointer-width alignment.
  2. The free list is searched for the first free block large enough
     (first-fit).
  3. If found and it's significantly larger than needed, it's **split** —
     the leftover remainder becomes a new free block.
  4. If no suitable free block exists, a new block is carved from the pool
     (bump-allocated) and inserted into the free list **in address order**
     (this ordering is what makes coalescing possible later).
- **Freeing (`myfree`)**: marks the block `is_free = true`, then calls
  `coalesce()` to merge it with an adjacent free block (checking both the
  next block and, by walking the list, the preceding block) when they're
  physically contiguous in memory.

## Files

| File | Purpose |
|---|---|
| `allocator.h` | Core allocator: `init_pool`, `mymalloc`, `myfree`, `block_split`, `coalesce` |
| `main.cpp` | Manual walkthrough demonstrating allocation, freeing, address reuse, splitting, and coalescing behavior with printed output |
| `benchmark.cpp` | Compares `mymalloc`/`myfree` against the system `malloc`/`free` across single-size, mixed-size, bulk-allocate, and reverse-order-free workloads |

## Build & run

```bash
# Demo
g++ main.cpp -o main
./main

# Benchmark
g++ -O2 benchmark.cpp -o benchmark
./benchmark
```

> Uses `sys/mman.h` (POSIX `mmap`), so this currently targets Linux/macOS/WSL,
> not native Windows.
