# Client render preparation

This change addresses the gaps exposed by rapid movement/zoom. It does not
replace the game renderer with WebGL/WASM or alter the wire protocol.

## Readiness

`visual_assets.js` owns shared SVG source promises and original/derived
images. Readiness includes source fetch, SVG surgery, `load`, and `decode`.
Failures/timeouts settle explicitly; map loads reject failed terrain assets
and allow a retry. Successful Blob URLs are retained for DOM card reuse.

Startup prepares animated mob bodies/parts, recoloured summons, cropped live
petals, rarity variants, flower attachments, card art and fonts. Small sprite
rasters are primed before the loading curtain is dismissed. Canvas and DOM
cards use the same image source/variant, rather than fetching it separately.

## Terrain continuity

`map_chunk_cache.js` publishes **complete, opaque layer stacks** for aligned
world regions. Changing zoom/DPR changes pixel density but retains old
coverage until replacement chunks exist. Hidden layers stay hidden. Real
neighbour-pixel gutters and shared device-pixel edges replace stretched
overlapping chunk rectangles. Terrain edits invalidate all touching levels
and update the overview fallback immediately.

Visible jobs take priority over a velocity-based lookahead (250ms, capped).
The queue is refreshed for the current camera, so old movement/zoom work is
cancelled. Startup warms the visible region before publishing the new map.
Close-up cache misses can draw decoded SVG tiles directly; far-away misses
still retain the overview when no previous coverage exists. This is not a
promise of unlimited, instantaneous high-resolution coverage after arbitrary
teleports.

Preparation is **cooperatively sliced on the main thread**, targeting 1.5ms
between yields; it is not a Worker implementation. One browser raster call
cannot be pre-empted, so real-device frame timing still needs measurement.
New sprite bitmap sizes are prepared outside the entity draw call, with the
decoded SVG displayed until ready. Giant sprites beyond 2048 device pixels
use the original image path, avoiding enlargement of the capped bitmap.

## Budget and timing

Map composites, sprite rasters and hit variants share a 192MiB byte LRU.
Current-frame coverage is pinned. Allocation falls back instead of growing
without bound; release callbacks clear canvas storage. Overview canvases,
source images, text caches and the display framebuffer are separate costs,
so 192MiB is **not** a total browser memory cap.

Map crossfades composite once, not per layer. A transition buffer larger
than 32MiB is skipped; the prepared new map is then published without a fade.
Observer close and map replacement release the old terrain rasters.

Client zoom interpolates separately from the saved target/server radius.
Short hitches catch up with at most eight movement steps over 250ms. Adaptive
resolution accounts for the whole frame and preparation cost, pauses while
zoom/terrain work settles, and never automatically drops below 1x (explicit
0.75x remains available). Subpixel bodies are not replaced with dots or
discarded; text readability thresholds remain independent.

F1 debug information includes `chunks … q… profile` and shared `raster
used/limit MiB`. All tile/asset paths and protocol fields stay compatible.
The optional bridge ETag change requires a web-bridge restart; JS changes
only need a hard reload. No game-server rebuild is required.

## Offline verification

- `node tools/test_render_pipeline.mjs`: decode/failure/retry, source reuse,
  byte budget/pinning, complete layers, zoom fallback, gutters, cancellation,
  vector fallback, subpixel sprites and 30/60/120fps zoom/catch-up.
- `node tools/test_map_loading.mjs`: paths, retries, readiness and observer
  isolation with fake resources/canvases.
- Existing UI, motion, input and bridge regression suites.

No production game process, database or account is used by these checks.
