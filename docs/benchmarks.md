# Benchmarks

Measured on 12 cores / 24 threads, Release build. Times cover rendering only (not model loading or BVH build).

## BVH (single thread)

| Scene | Resolution | SPP | Acceleration | Time |
|---|---|---|---|---|
| Cornell box | 600x600 | 500 | None | 135.4 s |
| Sir Hops-a-Lot, the Stanford bunny (69k tris) | 400x400 | 16 | None | 3702.9 s |
| Sir Hops-a-Lot, the Stanford bunny (69k tris) | 400x400 | 16 | BVH (midpoint) | 2.5 s |
| Sir Hops-a-Lot, the Stanford bunny (69k tris) | 400x400 | 16 | BVH (SAH) | 1.7 s |
| XYZ RGB dragon (250k tris) | 400x400 | 16 | BVH (SAH) | 2.2 s |

## Multithreading (BVH on, SAH)

| Scene | Resolution | SPP | Threads | Time | Speedup |
|---|---|---|---|---|---|
| Cornell box | 600x600 | 500 | 1 | 163.7 s | 1x |
| Cornell box | 600x600 | 500 | 24 | 11.0 s | 14.9x |
| XYZ RGB dragon (250k tris) | 400x400 | 16 | 1 | 1.97 s | 1x |
| XYZ RGB dragon (250k tris) | 400x400 | 16 | 24 | 0.14 s | 14.1x |

## Correctness

- 1-thread and 24-thread renders are byte-for-byte identical.
- Renders with and without the BVH are byte-for-byte identical.

## Reproduce

```bash
./build/kiln --no-bvh --threads 1 --out nobvh.png
./build/kiln --midpoint --threads 1 --out midpoint.png
./build/kiln --threads 1 --out t1.png
./build/kiln --threads 24 --out t24.png
cmp t1.png t24.png
```

Switch scenes in `main()` in `src/main.cpp`.
