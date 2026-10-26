# Kiln

[![CI](https://github.com/Mohamed-Elkiky/Lumen/actions/workflows/ci.yml/badge.svg)](https://github.com/Mohamed-Elkiky/Lumen/actions/workflows/ci.yml)

**A physically based path tracer written from scratch in modern C++17.**

No game engine, no graphics API. Every pixel is computed by simulating how light bounces around a scene.

![Cornell box](renders/06-cornell-box.png)

*Cornell box, 600x600, 500 samples per pixel. Note the red/green colour bleeding and the caustic under the glass sphere.*

## Highlights

- **Monte Carlo path tracing** with matte, metal (with roughness), glass (Snell's law, total internal reflection, Schlick) and emissive area lights
- **Next event estimation**: every matte bounce fires a shadow ray straight at a light. About 3x less noise at the same sample count, with the same average image (checked by a unit test)
- **Triangle meshes** from `.obj` files, with Möller-Trumbore intersection and smooth shading from vertex normals (generated when a model has none)
- **Flat BVH with binned SAH splits**: one array of 64-byte nodes, an iterative traversal and triangles packed by value. The Stanford bunny goes from ~59 minutes to 1.29 s on one thread (~2,700x)
- **Lock-free multithreaded tile renderer**: ~15x faster on 24 threads
- **Deterministic**: byte-identical output on 1 or 24 threads, with or without the BVH
- **JSON scene files and a real CLI**: change any render without recompiling, with errors that point at the exact field
- **Neural denoising** with Intel Open Image Denoise: 16 samples per pixel looks close to 500 (`--denoise`)
- **Live preview window** (SDL2): watch the image refine from noise to clean, and drag to orbit the camera (`--preview`)
- **Tested and benchmarked**: unit tests with CTest, a benchmark harness, and CI on Linux (GCC, Clang), macOS and Windows with ThreadSanitizer and clang-format checks

## Progress

Built in public, one sprint at a time. Each render was a milestone.

| | | |
|:---:|:---:|:---:|
| ![](renders/00-sky-gradient.png) | ![](renders/01-first-light.png) | ![](renders/02-diffuse.png) |
| Camera and rays | Ray-sphere intersection | Diffuse bounces and gamma |
| ![](renders/03-metal.png) | ![](renders/04-glass.png) | ![](renders/05-area-light.png) |
| Matte and metal | Glass with refraction | Area lights |
| ![](renders/07-triangles.png) | ![](renders/08-teapot.png) | ![](renders/09-bunny.png) |
| Triangles | Utah teapot | Sir Hops-a-Lot (Stanford bunny) |
| ![](renders/10-dragon.png) | | |
| Dragon, 250k triangles | | |

## Performance

`kiln_bench` on a Ryzen 9 7900X (12 cores / 24 threads), MSVC 19.44 Release, median of 3 runs. Full report in [docs/benchmarks.md](docs/benchmarks.md).

| Scene | Size | Naive, 1T | BVH SAH, 1T | BVH SAH, 24T | Total speedup |
|---|---|---|---|---|---|
| Stanford bunny | 69k tris | ~58m 59s | 1.29 s | **0.09 s** | **~37,000x** |
| XYZ RGB dragon | 250k tris | ~9h 18m | 1.63 s | **0.11 s** | **~294,000x** |
| Cornell box | 8 objects | 10.5 s | 12.0 s | **0.88 s** | 11.9x |

`~` is an estimate: brute force on a mesh is rendered smaller and scaled up. A full brute-force bunny render, timed directly, took 3,703 s, within 5% of the estimate. The Cornell box fires a shadow ray at every matte bounce (next event estimation), so each sample costs more but carries far less noise.

- **Logarithmic scaling.** The dragon has 3.6x more triangles than the bunny but takes only 1.3x as long.
- **SAH beats a median split** by 1.5x on the bunny and 1.7x on the dragon.
- **Threads scale 12.9x to 14.3x** on 24 threads. There are 12 physical cores; hyperthreads fill the idle cycles.
- **Small scenes don't need a BVH.** On the 8-object Cornell box, brute force is 1.1x faster.
- **Brute force hits a memory wall.** Per ray, naive is 9.3x slower on the dragon than the bunny despite only 3.6x more triangles. The bunny likely fits in the 32 MB L3 cache; the dragon doesn't.

## Build and run

Requires CMake 3.20+ and a C++17 compiler (MSVC, GCC or Clang).

```bash
cmake -B build
cmake --build build --config Release
./build/kiln scenes/cornell.json -o cornell.png      # Windows: .\build\Release\kiln.exe
```

Flags override the scene file:

```bash
kiln scenes/bunny.json --samples 64 --width 800 -o bunny.png
kiln scenes/dragon.json --benchmark --runs 5
kiln --help
```

| Flag | Effect |
|---|---|
| `-o, --out FILE` | Output PNG (default `output.png`) |
| `-s, --samples N` / `-d, --depth N` / `-w, --width W` | Samples per pixel, max bounces, image width |
| `-t, --threads N` | Worker threads (default: all) |
| `--seed N` | Random seed; same seed, same image |
| `--no-bvh` / `--midpoint` | Brute force, or median-split BVH instead of SAH |
| `--no-nee` | Turn off next event estimation (pure path tracing, for comparison) |
| `--benchmark` | Time the render instead of saving it: median of `--runs` and Mrays/s |
| `--denoise` | Remove noise with Intel Open Image Denoise (see [Denoising](#denoising)) |
| `--preview` | Live window that refines as you watch (see [Live preview](#live-preview)) |

**Models** are too large to commit. Download them into `assets/models/` (use `curl.exe` on Windows):

```bash
mkdir -p assets/models
curl -L -o assets/models/teapot.obj https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/teapot.obj
curl -L -o assets/models/stanford-bunny.obj https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/stanford-bunny.obj
curl -L -o assets/models/xyzrgb_dragon.obj https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/xyzrgb_dragon.obj
```

## Tests, benchmarks and CI

```bash
ctest --test-dir build -C Release                     # unit tests
./build/kiln_bench --out docs/benchmarks.md           # benchmarks, from the repo root
cmake -B build-tsan -DKILN_TSAN=ON && cmake --build build-tsan && ./build-tsan/test_renderer && ./build-tsan/test_progressive   # TSan (GCC/Clang)
```

Every push and pull request runs [GitHub Actions](.github/workflows/ci.yml):

| Job | What it checks |
|---|---|
| Linux GCC + OIDN | Build with warnings as errors, all tests, real denoising, a smoke render |
| Linux Clang | Build with warnings as errors, all tests, a smoke render |
| macOS Clang | Build, all tests, a smoke render |
| Windows MSVC + preview | Build including the SDL2 preview window, all tests, a smoke render |
| ThreadSanitizer | The threaded renderer and progressive preview, with zero data races |
| clang-format | Every source file matches `.clang-format` (pinned to 18.1.8) |

Every build job also renders a small Cornell box and uploads the PNG, so each run's Artifacts section has a real image from every OS.

The tests use [doctest](https://github.com/doctest/doctest): 41 test cases across 8 suites, one executable each. They check, among other things, that:

- every primitive (sphere, triangle, quad, box) intersects rays correctly, from outside, from inside and edge-on
- the BVH returns the same closest hit as brute force on 20k random rays
- output is pixel-identical on 1, 4 and 16 threads, with and without NEE
- NEE converges to the same brightness as plain path tracing, with less noise
- denoising 8 spp cuts the error against a 256 spp reference by over 4x
- every scene file error names the exact field

## Scene files

Every scene is a JSON file in `scenes/`:

```json
{
  "camera":   { "look_from": [278, 278, -800], "look_at": [278, 278, 0], "fov": 40 },
  "settings": { "width": 600, "aspect_ratio": 1, "samples": 500, "depth": 50 },
  "materials": {
    "glass": { "type": "dielectric", "ior": 1.5 },
    "white": { "type": "lambertian", "albedo": [0.73, 0.73, 0.73] }
  },
  "objects": [
    { "type": "sphere", "center": [190, 90, 190], "radius": 90, "material": "glass" },
    { "type": "box", "min": [265, 0, 295], "max": [430, 330, 460], "material": "white" }
  ]
}
```

- **Materials:** `lambertian`, `metal` (`fuzz`), `dielectric` (`ior`), `light` (`emit`)
- **Textures:** any `lambertian` or `metal` `albedo` can be a procedural texture instead of a colour. They're solid 3D textures, looked up at the hit point, so they need no UVs and work on any shape:

| `texture` | Fields |
|---|---|
| `checker` | `colors` (2), `scale` |
| `noise` | `colors` (2), `scale`, `seed` (Perlin turbulence) |
| `wood` | `colors` (light, dark), `axis` (grain direction), `center`, `ring_scale`, `noise`, `seed` |
| `normal` | none: colour from the direction the surface faces |
| `gradient` | `colors` (2 or more), `axis`, `from`, `to` (heights along the axis) |

```json
"table": { "type": "lambertian", "albedo": { "texture": "wood", "colors": [[0.72, 0.5, 0.3], [0.38, 0.22, 0.1]], "axis": "x", "ring_scale": 6, "noise": 1.2 } }
```
- **Objects:** `sphere`, `quad`, `triangle`, `box`, `mesh` (`file`, plus `transform`: `scale`, `rotate_y`, `translate`)
- Quads and boxes with a `light` material are sampled directly by next event estimation.
- Mesh paths are relative to the scene file, and `//` comments are allowed.

Mistakes name the exact field:

```
error: scenes/cornell.json: objects[6].material: unknown material 'glas' (defined: glass, green, light, mirror, red, white)
```

## Live preview

```bash
cmake -B build -DKILN_PREVIEW=ON          # downloads and builds SDL2 the first time
cmake --build build --config Release
./build/kiln scenes/cornell.json --preview
```

| Input | Action |
|---|---|
| Drag | Orbit the camera around the scene |
| Scroll | Zoom in and out |
| `R` | Reset the view |
| `S` | Save the current image to `--out` |
| `C` | Print the camera as a JSON block to paste into a scene file |
| `Esc` | Quit |

The window title shows the pass count, elapsed time and Mrays/s. Each pass adds one sample per pixel, so the image goes from noise to clean in seconds.

- **Rendering never blocks the window.** Worker threads trace each pass; the main thread only handles input and uploads the latest frame about 30 times a second.
- **No tearing.** A pass is added to the running average only once it's complete, under a lock, so the window never shows a half-finished frame.
- **Instant restarts.** Moving the camera cancels the pass in flight within one tile and starts accumulating again.

The preview is off by default, so headless builds never need SDL or a display.

## Denoising

`--denoise` runs [Intel Open Image Denoise](https://www.openimagedenoise.org/), a neural denoiser, on the finished render. During the same render, Kiln fills two guide images: the **albedo** (surface colour) and **normal** of the first rough surface each ray reaches, looking through glass and mirrors. They show the denoiser where real edges are, so it removes noise without blurring detail.

| 16 spp | 16 spp + `--denoise` | 500 spp reference |
|:---:|:---:|:---:|
| ![](renders/11-denoise-noisy.png) | ![](renders/11-denoise-oidn.png) | ![](renders/06-cornell-box.png) |

Same scene, 31x fewer samples.

OIDN is optional. Without it, Kiln builds normally and `--denoise` says it's unavailable. To enable it, unzip a [2.x release](https://github.com/RenderKit/oidn/releases) anywhere and point CMake at it:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/oidn-2.3.3.x86_64.linux
cmake --build build --config Release
./build/kiln scenes/cornell.json --samples 16 --denoise -o denoised.png
```

On Windows, CMake copies the OIDN DLLs next to `kiln.exe`. `-DKILN_DENOISE=OFF` builds without it even when it's installed.

## How it works

1. The image is split into **32x32 tiles**, rendered in parallel by a pool of worker threads.
2. For each pixel, the **camera** fires many jittered rays (antialiasing).
3. Each ray walks a **BVH** to find the closest hit, skipping any box it misses along with everything inside.
4. The surface's **material** decides what happens next: scatter (matte), reflect (metal), refract or reflect (glass), or emit (lights).
5. At a matte surface, a **shadow ray** goes straight to a random point on a random light (next event estimation).
6. The ray **bounces** recursively, picking up colour, until it escapes or hits the bounce limit.
7. Samples are **averaged**, gamma corrected and written to PNG.

Soft shadows, colour bleeding and caustics all fall out of the physics without being programmed in.

**Next event estimation.** Pure path tracing only finds a light when a random bounce happens to hit it, which is rare for a small ceiling panel. So at every matte surface Kiln also picks a random point on a random light quad and fires a shadow ray at it. If nothing blocks it, that light's contribution is added directly. If the random bounce later lands on that same light, its glow is skipped, because the shadow ray already counted it. Mirrors and glass reflect in one exact direction, so they can't use shadow rays and still pick up light by hitting it, which is how caustics survive. On the Cornell box at 16 spp this cuts the error against a converged reference by 3.2x.

**BVH.** At each node, Kiln bins primitives into 16 buckets per axis and picks the split with the lowest Surface Area Heuristic cost, `area(L) x count(L) + area(R) x count(R)`. Big boxes are likelier to be hit, so SAH keeps them nearly empty. Traversal visits the nearer child first and clips the far child to the closest hit so far, so most far-side boxes are rejected immediately.

The tree is stored **flat**:

- **One array** of 64-byte nodes (a cache line each) in depth-first order. A left child is always the next node, so only the right child's index is stored.
- **No recursion:** traversal is a loop with a fixed-size stack, and `1 / direction` is worked out once per ray, not once per box.
- **Packed triangles:** triangles are copied into the BVH by value in leaf order and tested with a direct call instead of a virtual one.

Against the pointer-based tree it replaced, that's 1.12x faster on the bunny and 1.17x on the dragon on one thread with MSVC (1.35x and 1.42x with GCC), with byte-identical output.

**Multithreading.**

- **Tiles, not rows:** compact tiles keep neighbouring rays on the same BVH nodes, so the cache stays warm, and they balance uneven work.
- **Lock-free queue:** a single `std::atomic<int>`; each thread `fetch_add`s the next tile. No mutex.
- **No framebuffer locks:** every pixel belongs to exactly one tile.
- **Per-tile seeding:** each thread has a `thread_local` RNG, reseeded from the tile index. A pixel's noise depends on its tile, not its thread, which is what makes output byte-identical at any thread count.

## Project structure

```
kiln/
├── .github/        CI workflow
├── include/kiln/   Headers: maths, camera, materials, shapes, lights, BVH, renderer, scene loader
├── src/            Implementation, main and benchmark
├── tests/          Unit tests (doctest) and .obj fixtures
├── scenes/         JSON scene files
├── external/       Single-header libraries
├── assets/models/  Downloaded models (not committed)
├── renders/        Milestone renders
└── docs/           Benchmarks
```

## Roadmap

- [x] **Sprint 1:** Foundations (vectors, rays, camera, spheres, PNG output)
- [x] **Sprint 2:** Materials and lighting (matte, metal, glass, area lights, Cornell box)
- [x] **Sprint 3:** Performance (triangles, `.obj` meshes, SAH BVH, multithreading, benchmarks)
- [x] **Sprint 4:** Polish
  - [x] JSON scene files and CLI
  - [x] Live preview window
  - [x] Denoising
  - [x] Next event estimation
  - [x] Flat BVH
  - [x] CI

## References

**Books**
- Shirley, Black, Hollasch: *Ray Tracing in One Weekend* series
- Pharr, Jakob, Humphreys: *Physically Based Rendering: From Theory to Implementation*

**Light transport**
- Kajiya: *The Rendering Equation*, SIGGRAPH (1986). The equation every path tracer solves.
- Veach: *Robust Monte Carlo Methods for Light Transport Simulation*, PhD thesis, Stanford (1997). Light sampling and next event estimation.
- Schlick: *An Inexpensive BRDF Model for Physically-based Rendering*, Computer Graphics Forum (1994). The Fresnel approximation used for glass.

**Intersection**
- Möller, Trumbore: *Fast, Minimum Storage Ray/Triangle Intersection*, Journal of Graphics Tools (1997)
- Kay, Kajiya: *Ray Tracing Complex Scenes*, SIGGRAPH (1986). The slab test for bounding boxes.
- Williams, Barrus, Morley, Shirley: *An Efficient and Robust Ray-Box Intersection Algorithm*, Journal of Graphics Tools (2005)

**Acceleration**
- Goldsmith, Salmon: *Automatic Creation of Object Hierarchies for Ray Tracing*, IEEE Computer Graphics and Applications (1987). The surface-area idea behind SAH.
- MacDonald, Booth: *Heuristics for Ray Tracing Using Space Subdivision*, The Visual Computer (1990). The Surface Area Heuristic.
- Wald: *On fast Construction of SAH-based Bounding Volume Hierarchies*, IEEE Symposium on Interactive Ray Tracing (2007). Binned SAH, as used here.

**Denoising**
- Intel Open Image Denoise documentation: albedo and normal guides, prefiltering, `cleanAux`

**Scenes and models**
- The Cornell Box, Cornell University Program of Computer Graphics
- Turk, Levoy: *Zippered Polygon Meshes from Range Images*, SIGGRAPH (1994). The Stanford bunny.
- The Stanford 3D Scanning Repository (bunny, XYZ RGB dragon) and common-3d-test-models (the `.obj` downloads, Utah teapot)

**Third-party code:** stb_image_write (public domain / MIT), tinyobjloader (MIT), nlohmann/json (MIT), CLI11 (BSD-3-Clause), doctest (MIT), Intel Open Image Denoise (Apache-2.0, optional), SDL2 (zlib, optional)

## Contact

Want to collaborate on Kiln?
If there is any other open source of private projects that you might need me in please dm me on linkedin or email me at moh.elkiky@gmail.com