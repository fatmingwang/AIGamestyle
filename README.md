# AIGamestyle: reskin a game's art with AI

Gives a finished game a new art style with a local ComfyUI pipeline. Every new picture keeps
the original's file name, size and transparency, and Spine `.atlas` / `.json` files are never edited, so the output
drops into the game as is.

- Full method, lessons and worked examples: [Doc/Reskin_Game_Art.md](Doc/Reskin_Game_Art.md) (zh-TW: [Doc/Reskin_Game_Art.zh-TW.md](Doc/Reskin_Game_Art.zh-TW.md))
- Setting up a new PC (ComfyUI, nodes, models): [Doc/Reskin_Setup_New_PC.md](Doc/Reskin_Setup_New_PC.md)
- Visual guide with a decision panel (open in a browser): [Doc/Reskin_Guide.html](Doc/Reskin_Guide.html)
- Plans (one JSON per game + style): `_pipeline/style_profiles/` — `zeus_10085.json`, `christmas_10085.json`

## Reskin a game in 8 steps

1. **Start ComfyUI** (`_pipeline/tools/start_comfyui.bat`, it listens on `127.0.0.1:8188`). Check it with
   `python _pipeline/tools/check_reskin_setup.py`; it must say `ready`.
2. **Point a plan at the game.** Copy an existing plan to `_pipeline/style_profiles/<style>_<game>.json`. Set
   `source` (the game's art folder; absolute paths work, the source is only read), `output` (under `output/`),
   `style` (words added to every prompt) and `negative`.
3. **List the static pictures** under `static`, each with a `kind` and a one-line `subject` that describes the **same
   object** in the new style (a mahjong tile stays a mahjong tile). Kinds and their strength are in `kinds`.
4. **List the Spine pages** under `spine.pages`, each with a `subject` and, if it has one, its static symbol as
   `reference`, so the animated parts match the symbol.
5. **Dry run**: `python restyle_game.py <plan> --dry-run` prints how every picture and every Spine part is sorted
   (keep / text / object). Fix wrong sorts with `keep_words`, `sequence_pattern`, `text_names`, `keep_parts`, `parts`.
6. **Pilot** 3–4 key pictures: `python restyle_game.py <plan> --only <part of a file path>`. Look at them, adjust subjects and
   strengths, redo with `--force`.
7. **Full run**: `python restyle_game.py <plan>`. It resumes where it stopped (`<output>/_restyle_report.json`).
   Review `<output>/_previews/sheet_NN.png`; redo a bad file with `--only <name> --force` or another `--seed`.
8. **Check and apply**: `python check_restyle.py <source> <output>` must show the same sizes and alpha. Back up
   the game's originals, then copy the output's pictures over them and play the game.

All commands run from `_pipeline/tools/` with Python 3.11 (Pillow, numpy).

## What it can and cannot do

| Can                                                                        | Cannot                                                                                      |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| Change materials, colours, patterns, lighting inside each shape            | Change a shape: alpha is the original's (a rectangular tile cannot become an arched tablet) |
| Restyle Spine parts one by one, keeping atlas and animation                | Add parts, change a body, or fix animation; that needs a Spine artist                       |
| Keep letters and glyphs readable                                           | Strongly restyle text: it is done gently, so buttons and banner words change little         |
| Leave effects and flipbooks untouched (no flicker, additive blending kept) | Recolour effects with AI (a separate hue shift could)                                       |
| Work up to ~2048 px                                                        | Match a 3000 px original's sharpness exactly; big pictures come out a little softer         |

## Plan fields (quick reference)

Static item: `file` or `glob`, `kind`, `subject`, and optional `denoise`, `control`, `crop`, `long_side`,
`keep_glyph` (see the change log below for the last three).
Kind (`kinds.<name>`): `denoise` (strength), `control` (outline hold), `prompt_extra`, optional `keep_glyph`.
Spine page: `atlas`, `subject`, `reference`, `denoise`, `control`, `keep_glyph`, `keep_parts`, `parts.<name>`
(`subject`, `reference: false`, `denoise`, `keep_skin`, `keep_glyph`).
Spine sorting: `keep_words`, `sequence_pattern`, `min_size`, `text_names`, `text_prefixes`, `reference_weight`.

## Change log of the tools (why each change exists)

Read this before changing `restyle_game.py` / `restyle_pilot.py`: each entry says what problem the code solves, so
a later change does not undo it by accident. All of these options are **opt-in**; a plan that does not use them
gives exactly the same result as before.

The entries, newest first: [Doc/Tool_Changes.md](Doc/Tool_Changes.md) (`crop`, `long_side`, per-item strength,
`keep_glyph`, flipbook pattern; 2026-11-11).
