# Tool changes and why they exist

Read this before changing `_pipeline/tools/restyle_game.py` or `restyle_pilot.py`. Each entry says which problem the
code solves, so a later change does not undo it by accident. Every option below is **opt-in**: a plan that does not
use it gives exactly the same result as before.

## 2026-11-11 — reskin of a mahjong-tile slot game

### `crop` (static item)
- **Problem:** the reel frames are 3000 × 1280 PNGs whose visible pixels are only about 900 px in the middle (the
  rest is transparent, for wide screens). The pipeline scales the whole picture to 1024 wide, so the frame was made
  at a third of its size and came back 3× blurry.
- **Fix:** `restyle_picture(..., crop=True)` restyles only the alpha bounding box (+16 px margin) and pastes it back.
  The transparent pixels outside are unchanged, so size and alpha stay identical.
- **Use for:** any big, mostly transparent picture. It does nothing on a fully opaque picture.

### `long_side` (static item)
- **Problem:** full 3000 × 1280 backgrounds made at 1024 × 440 and scaled up 3× are very soft in the 720 px a phone
  shows.
- **Fix:** the working size is passed to `restyle_pilot.prepare(long_side=...)`. The backgrounds use 2048 (about 24 s
  instead of 12 s on an RTX 4070).
- **Limit:** above about 2048, SDXL starts repeating objects. The default stays 1024.

### `denoise` / `control` on a static item
- **Problem:** strength was set only per kind, so one picture that needed a stronger change (the green reel panel
  to navy, the yellow ± buttons) forced a new kind.
- **Fix:** a static item may override its kind's `denoise` and `control`, as Spine `parts` already could.

### `keep_glyph` (kind, static item, Spine page or Spine part)
- **Problem:** turning white mahjong tiles into carved stone needs a strength of about 0.75. At that strength the
  model also recolours the glyph: a green character came out black, and red / green / blue dot patterns went dark. A glyph's
  colour is how the player tells the symbols apart. At a strength low enough to keep the colours (0.55–0.62), the
  tile body barely changed.
- **Fix:** `restyle_game.keep_glyph()` restyles at full strength, then puts the original's saturated pixels back
  inside the tile face. The tile's sides are left out, so a mahjong tile's green back is still restyled. Gold and
  yellow hues are skipped, so a golden tile's body is restyled. The mask is grown 1 px and feathered so the restyled
  glyph's edge is covered. Alpha is never touched.
- **Spine:** set on a page, it applies to that page's `object` parts. Turn it off for non-tile parts with
  `"keep_glyph": false` in `parts`; otherwise a red banner flag or an orange dragon gets its old colour back.
- **Limit:** tuned for tiles (`inset`, `min_sat=0.35`, `gold_hue=(18, 70)`). A symbol whose glyph is yellow or
  orange needs a different hue range.

### `sequence_pattern` with look-behinds (plan only, no code change)
- The game had flipbook frames numbered with 5 digits, which the old `_\d{2,3}$` missed, so they would have been
  restyled one by one and flickered. It also had distinct tile pictures whose names merely end in a number, which the
  old pattern wrongly kept unchanged.
- The fix is in the plan: allow 2–5 digits, and add a negative look-behind for each name prefix that ends in a number
  but is not a flipbook, in the form `(?<!<prefix>)_\d{2,5}$`. Check the dry run for both cases on every new game.
