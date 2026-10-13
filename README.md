# Kiln

**A physically based path tracer written from scratch in modern C++.**

No game engine, no graphics API. Every pixel is computed by simulating how light bounces around a scene.

![Cornell box](renders/06-cornell-box.png)

*Cornell box, 600x600, 500 samples per pixel. Note the red/green colour bleeding on the white surfaces and the caustic under the glass sphere.*

---

## Features

- **Monte Carlo path tracing** with recursive bounces and a fixed bounce limit
- **Materials**
  - Lambertian (matte)
  - Metal with adjustable roughness (fuzz)
  - Dielectric (glass, water, diamond) with Snell's law, total internal reflection and Schlick's approximation
  - Emissive area lights
- **Geometry**
  - Spheres and quads (parallelograms)
  - Triangles with Möller-Trumbore intersection
  - Wavefront `.obj` mesh loading with scale, rotate and translate on load
  - Smooth shading from vertex normals, generated automatically for models that don't include them
- **BVH acceleration** with binned Surface Area Heuristic (SAH) splits: 2,168x faster on the Stanford bunny, pixel-identical output
- **Multithreaded tile rendering** with lock-free work distribution: 14.9x faster on 24 threads
- **Positionable camera** with configurable field of view, look-at and up vector
- **Antialiasing** via jittered supersampling
- **Gamma correction** and PNG output
- **Deterministic renders**: the same scene gives byte-identical output with or without the BVH, on any number of threads
- **Unit tests** with CTest, plus a clean ThreadSanitizer run

## Progress

Kiln is being built in public, one sprint at a time. Each render below was a milestone.

| | | |
|:---:|:---:|:---:|
| ![](renders/00-sky-gradient.png) | ![](renders/01-first-light.png) | ![](renders/02-diffuse.png) |
| Camera and rays | Ray-sphere intersection | Diffuse bounces and gamma |
| ![](renders/03-metal.png) | ![](renders/04-glass.png) | ![](renders/05-area-light.png) |
| Matte and metal | Glass with refraction | Area lights |
| ![](renders/07-triangles.png) | ![](renders/08-teapot.png) | ![](renders/09-bunny.png) |
| Triangles | Utah teapot | Sir Hops-a-Lot (Stanford bunny) |
| ![](renders/10-dragon.png) | | |
| Dragon, 250k triangles (BVH) | | |

## Performance

### BVH (single thread)

| Scene | Resolution | Samples | Acceleration | Time |
|---|---|---|---|---|
| Sir Hops-a-Lot, the Stanford bunny (69k tris) | 400x400 | 16 | None | 3702.9 s |
| Sir Hops-a-Lot, the Stanford bunny (69k tris) | 400x400 | 16 | BVH (midpoint) | 2.5 s |
| Sir Hops-a-Lot, the Stanford bunny (69k tris) | 400x400 | 16 | BVH (SAH) | **1.7 s** |
| XYZ RGB dragon (250k tris) | 400x400 | 16 | BVH (SAH) | 2.2 s |

Without acceleration, every ray is tested against every triangle, so render time grows linearly with mesh size. The BVH takes the bunny from an hour to under 2 seconds: a **2,168x speedup**. The dragon has 3.6x more triangles than the bunny but takes only 1.3x as long, which is logarithmic scaling in action. SAH splits beat a simple median split by 1.45x.

### Multithreading (BVH on)

| Scene | Resolution | Samples | Threads | Time | Speedup |
|---|---|---|---|---|---|
| Cornell box | 600x600 | 500 | 1 | 163.7 s | 1x |
| Cornell box | 600x600 | 500 | 24 | **11.0 s** | **14.9x** |
| XYZ RGB dragon (250k tris) | 400x400 | 16 | 1 | 1.97 s | 1x |
| XYZ RGB dragon (250k tris) | 400x400 | 16 | 24 | **0.14 s** | **14.1x** |

Measured on 12 cores / 24 threads. Hyperthreads share a core's execution units, so speedup beyond 12x comes from filling idle cycles rather than doubling throughput.

Output is unchanged: the 1-thread and 24-thread renders are byte-for-byte identical, and so are renders with and without the BVH. Full results are in [docs/benchmarks.md](docs/benchmarks.md).

## Build and run

Requires CMake 3.20+ and a C++17 compiler (MSVC, GCC or Clang).

**Linux / macOS**
```bash
cmake -B build
cmake --build build
./build/kiln
```

**Windows (PowerShell)**
```powershell
cmake -B build
cmake --build build --config Release
.\build\Release\kiln.exe
```

The render is written to `output.png`. Run `kiln` from the repo root so it can find `assets/`. To change scenes, edit the scene picker in `main()` in `src/main.cpp`.

Options:

| Flag | Effect |
|---|---|
| `--threads N` | Render with `N` threads (default: one per hardware thread) |
| `--no-bvh` | Test every object per ray (brute force, for benchmarking) |
| `--midpoint` | Build the BVH with median splits instead of SAH |
| `--out FILE` | Write the render to `FILE` instead of `output.png` |

Run the tests with:
```bash
ctest --test-dir build -C Release
```

**ThreadSanitizer** (GCC or Clang only, MSVC has no TSan; on Windows use WSL):
```bash
cmake -B build-tsan -DKILN_TSAN=ON
cmake --build build-tsan
./build-tsan/test_renderer
```

### Models

Mesh models are large, so they're not committed. Download them into `assets/models/`:

```bash
mkdir -p assets/models
curl -L -o assets/models/teapot.obj https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/teapot.obj
curl -L -o assets/models/stanford-bunny.obj https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/stanford-bunny.obj
curl -L -o assets/models/xyzrgb_dragon.obj https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/xyzrgb_dragon.obj
```

On Windows PowerShell, use `curl.exe` instead of `curl`.

| File | Triangles | Use |
|---|---|---|
| `teapot.obj` | ~6.3k | Correctness check |
| `stanford-bunny.obj` | ~69k | Benchmark scene (Sir Hops-a-Lot) |
| `xyzrgb_dragon.obj` | ~250k | Stress test |

The full 870k-triangle Stanford dragon is available as PLY from the [Stanford 3D Scanning Repository](https://graphics.stanford.edu/data/3Dscanrep/).

## How it works

1. The image is split into **32x32 tiles**, and a pool of worker threads (one per core) renders them in parallel.
2. For each pixel, the **camera** fires many rays with random jitter (antialiasing).
3. Each ray walks a **bounding volume hierarchy** (a tree of boxes) to find the **closest hit**. Any box the ray misses is skipped along with everything inside it, so only a handful of the scene's triangles are ever tested.
4. The hit surface's **material** decides what happens next: scatter randomly (matte), reflect (metal), refract or reflect (glass), or emit light (lights).
5. The ray **bounces** recursively, picking up colour from each surface, until it escapes the scene or hits the bounce limit.
6. All samples for a pixel are **averaged**, gamma corrected and written to PNG.

Because light paths are simulated physically, effects like soft shadows, colour bleeding and caustics appear naturally, without being programmed in.

Meshes are loaded as lists of triangles. Where a model has vertex normals, the normal at each hit point is blended from the triangle's three corners using barycentric coordinates. This makes a faceted mesh render as a smooth surface.

### The BVH

Every object knows its **axis-aligned bounding box**. The BVH groups objects into a binary tree of boxes, and a ray tests a box with the **slab method** before looking inside it.

- **Build:** at each node, Kiln tries 15 candidate split planes per axis (16 bins) and picks the one with the lowest **Surface Area Heuristic** cost: `area(left) x count(left) + area(right) x count(right)`. A ray is likelier to hit a bigger box, so SAH keeps big boxes nearly empty. Leaves hold up to 4 primitives.
- **Traversal:** the ray visits the nearer child first. Once it hits something there, the far child is only searched up to that distance, so most of the far side's boxes are rejected straight away.

### Multithreading

Every pixel is independent, so path tracing parallelises almost perfectly. The design choices are about keeping threads busy and never making them wait on each other.

- **Tiles, not rows:** a 32x32 tile is a compact block, so neighbouring rays hit the same BVH nodes and triangles and the cache stays warm. Small tiles also spread the work evenly when one part of the image (glass, a dense mesh) is much slower than the rest.
- **Lock-free work queue:** the queue is a single `std::atomic<int>`. Each thread calls `fetch_add` to claim the next tile, so no mutex is needed and fast threads simply take more tiles.
- **No framebuffer locks:** each pixel belongs to exactly one tile, so threads never write the same memory.
- **Per-thread RNG, per-tile seed:** each thread has its own `thread_local` random generator (sharing one would be a data race). It is reseeded from the tile number at the start of every tile, so a pixel's noise depends only on which tile it's in, not which thread rendered it. That's what makes multithreaded output byte-identical to single-threaded.

## Project structure

```
kiln/
├── include/kiln/    Headers: vec3, ray, camera, materials, shapes, obj loader, AABB, BVH, renderer
├── src/             Implementation, threaded renderer and main
├── tests/           Unit tests
│   └── data/        Small .obj fixtures for the loader tests
├── external/        Third-party single-header libraries (stb_image_write, tinyobjloader)
├── assets/models/   Downloaded .obj models (not committed, see Models)
├── renders/         Milestone renders
└── docs/            Documentation and benchmarks
```

## Roadmap

- [x] **Sprint 1:** Foundations (vectors, rays, camera, spheres, PNG output)
- [x] **Sprint 2:** Materials and lighting (matte, metal, glass, area lights, Cornell box)
- [ ] **Sprint 3:** Performance
  - [x] Triangle primitive
  - [x] `.obj` mesh loading
  - [x] BVH acceleration (with SAH)
  - [x] Multithreading
  - [ ] Benchmark harness
- [ ] **Sprint 4:** Polish (JSON scene files, CLI, live preview window, denoising, CI)

## References

- Peter Shirley, Trevor David Black, Steve Hollasch: [*Ray Tracing in One Weekend* series](https://raytracing.github.io/)
- Matt Pharr, Wenzel Jakob, Greg Humphreys: [*Physically Based Rendering*](https://pbr-book.org/)
- Tomas Möller, Ben Trumbore: [*Fast, Minimum Storage Ray/Triangle Intersection*](https://www.graphics.cornell.edu/pubs/1997/MT97.pdf) (1997)
- Ingo Wald: *On fast Construction of SAH-based Bounding Volume Hierarchies* (2007)
- [The Cornell Box](https://www.graphics.cornell.edu/online/box/), Cornell University Program of Computer Graphics
- [The Stanford 3D Scanning Repository](https://graphics.stanford.edu/data/3Dscanrep/) (bunny, dragon)

## Third-party code

- [stb_image_write](https://github.com/nothings/stb) by Sean Barrett (public domain / MIT)
- [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) by Syoyo Fujita and contributors (MIT)