# Reskinning a game's art with AI (restyle_game.py + ComfyUI)

How to give a finished game a new art style with the local AI pipeline in `AIGamestyle/`, so every new picture
replaces the original as is: same file, same size, same transparency, and Spine animations still working. Read this to
reskin **another game** with the same method; section 7 is the step-by-step for a new game.

- Setting up a PC (ComfyUI, custom nodes, models, where they go): [Reskin_Setup_New_PC.md](Reskin_Setup_New_PC.md)
- Traditional Chinese: [Reskin_Game_Art.zh-TW.md](Reskin_Game_Art.zh-TW.md)
- Worked examples: game **10085** (an Oktoberfest slot, Cocos Creator + Spine 3.8) in a **Christmas** and a **Zeus /
  ancient Greek** style (section 10).

## 1. What "placeable" means

A reskinned picture must drop into the game without touching anything else:

- **Same file name, folder and pixel size.** The game's layouts, prefabs and atlases refer to them.
- **Same alpha.** The silhouette and the soft edges stay exactly as they were, so hit areas, masks and outlines still
  line up. The pipeline changes the colours and copies the original alpha channel back.
- **Spine stays valid.** A Spine page's `.atlas` (where each part is) and the skeleton `.json` / `.skel` (how the
  parts move) are never changed; only the pixels inside each part are.
- **Letters and numbers stay readable** (WIN, WILD, ×10, A K Q J).

What follows from this: **the reskin changes materials, colours and patterns, not shapes.** Nothing can be added
outside an object's silhouette (a laurel wreath sticking out of a hat), and an object cannot become a different
object. Spine moves the old shapes part by part, so a different object would not fit its animation.

## 2. The tools

All in `AIGamestyle/_pipeline/tools/`. Run them with Python 3.11 (Pillow + numpy) while ComfyUI is running.

| Tool | What it does |
|---|---|
| `restyle_game.py <plan>` | **the main tool**: copies the game, restyles every picture the plan lists, Spine pages part by part; resumable |
| `restyle_pilot.py <picture> <out> --prompt ... --denoise 0.5 0.7` | one picture at several strengths, for trying a style |
| `spine_page_restyle.py <page.atlas> <out> --prompt ...` | one Spine page part by part (what `restyle_game.py` does per page) |
| `spine_page_preview.py <original.atlas> <restyled_dir> <out.png>` | puts a page's parts back together (before / after) to check a whole object |
| `repair_edges.py <plan>` | applies step 6 to an output made before it existed: every restyled picture, and each restyled Spine part inside its own footprint; no AI run |
| `check_restyle.py <original> <restyled>` | proves the output is a drop-in replacement: same files, sizes and alpha; other files byte-identical |
| `check_reskin_setup.py` | checks ComfyUI has the nodes and models (setup document, section 7) |
| `make_sprites.py`, `sprite_sheet.py` | new small pixel-art sprites from descriptions (section 9) |

A plan is a JSON file in `AIGamestyle/_pipeline/style_profiles/`. It holds every choice (style, which file is what,
strengths), so a new style or a new game needs no code change. Examples: `christmas_10085.json`, `zeus_10085.json`.

### 2.1 AI models ComfyUI needs

The tools send ComfyUI jobs that ask for these model files **by exact file name**. A missing or differently named
file makes the job fail with `value_not_in_list`, so rename a download to the name in the first column. Put each file
in `AIGamestyle\_comfyui\ComfyUI\models\<folder>\`, or in the shared folder named in `extra_model_paths.yaml`
(setup document, section 5), then restart ComfyUI.

| File name the tools use | ComfyUI folder | Size (bytes) | Download (Hugging Face) | Needed by |
|---|---|---|---|---|
| `Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors` | `checkpoints` | 7 105 348 188 | `RunDiffusion/Juggernaut-XL-v9`, the same file name | restyle |
| `controlnet-union-sdxl-promax.safetensors` | `controlnet` | 2 513 342 408 | `xinsir/controlnet-union-sdxl-1.0`, file `diffusion_pytorch_model_promax.safetensors` (**rename**) | restyle, derived sprites |
| `ip-adapter-plus_sdxl_vit-h.safetensors` | `ipadapter` | 847 517 512 | `h94/IP-Adapter`, file `sdxl_models/ip-adapter-plus_sdxl_vit-h.safetensors` | restyle, sprites |
| `CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors` | `clip_vision` | 2 528 373 448 | `h94/IP-Adapter`, file `models/image_encoder/model.safetensors` (**rename**) | restyle, sprites |
| `sd_xl_base_1.0.safetensors` | `checkpoints` | 6 938 078 334 | `stabilityai/stable-diffusion-xl-base-1.0`, the same file name | pixel sprites only |
| `pixel-art-xl.safetensors` | `loras` | 170 543 052 | `nerijs/pixel-art-xl`, the same file name | pixel sprites only |

A download link is `https://huggingface.co/<repository>/resolve/main/<file>`, for example
`https://huggingface.co/xinsir/controlnet-union-sdxl-1.0/resolve/main/diffusion_pytorch_model_promax.safetensors`.
Compare the size after downloading: a cut-off file fails only when a job runs.

Which tool needs what:

| Tool | Models | Custom nodes |
|---|---|---|
| `restyle_game.py`, `restyle_pilot.py`, `spine_page_restyle.py` | the first four (about 13 GB): Juggernaut XL v9, ControlNet Union promax, IP-Adapter plus SDXL, CLIP-ViT-H | **ComfyUI_IPAdapter_plus** |
| `make_sprites.py` | SDXL base 1.0, pixel-art-xl LoRA, ControlNet Union promax, IP-Adapter plus SDXL, CLIP-ViT-H; plus Inspyrenet's `ckpt_base.pth` (367 MB, downloaded by itself on first use to `%USERPROFILE%\.transparent-background\`; copy it there on a PC without internet) | **ComfyUI_IPAdapter_plus**, **ComfyUI-Inspyrenet-Rembg** |
| `repair_edges.py`, `check_restyle.py`, `spine_page_preview.py`, `sprite_sheet.py` | none (no ComfyUI needed) | none |

The names are set in the code: `CHECKPOINT` and `CONTROLNET` at the top of `restyle_pilot.py` (the IP-Adapter and
CLIP-Vision names in its `workflow()`), and `CHECKPOINT` and `LORA` at the top of `make_sprites.py`. With ComfyUI
running, `check_reskin_setup.py` lists each file and node as `OK` or `MISSING`. Installing the nodes and the model
folders is in [Reskin_Setup_New_PC.md](Reskin_Setup_New_PC.md), sections 4 and 5.

## 3. How one picture is restyled

`restyle_pilot.py` (used by everything else):

1. **Prepare.** The picture is put on a neutral grey background (128, 128, 128) and scaled so its long side is 1024,
   the size SDXL works at; both sides are multiples of 8.
2. **Restyle.** SDXL img2img with **Juggernaut XL v9** changes it toward the prompt (`subject, kind's extra words,
   style`). **ControlNet Union** in canny mode holds its outlines, so the object keeps its shape. Sampler `dpmpp_2m_sde`
   with the `karras` schedule, 28 steps, CFG 6. Two numbers matter:
   - **denoise** (strength): how far the picture may move. About 0.7–0.8 for objects, 0.45 for text.
   - **control**: how hard the outlines are held. 0.6 for objects, 0.85 for text.
3. **Optional reference.** IP-Adapter plus (weight 0.35, "linear") makes the result follow another picture's look. Used
   so every Spine part of one object matches that object's restyled symbol.
4. **Finish.** Scaled back to the exact original size; the **original alpha** is put back.
5. **Take the grey back out of see-through pixels.** A semi-transparent pixel was restyled mixed with the grey
   (shown = colour × a + grey × (1 − a)), so its own colour is recovered as (shown − grey × (1 − a)) / a. Without
   this, dark see-through buttons come out grey and every soft edge gets a grey fringe. Pixels under 8 % alpha keep
   the original colour.
6. **Rebuild the soft edges** (`fix_edges`). Step 5 divides by alpha, so on nearly transparent edge pixels small
   errors blow up to white: a jagged, whitish fringe on hanging flags and on the prize wheel's rim. And the model drew
   no dark rim where the silhouette met the grey. So every pixel that is not solid in the original (alpha below 250)
   and lies within 4 px of a solid one takes the colour of the nearest solid pixel of the restyled picture, darkened
   or lightened as much as the original's edge pixel was against its own nearest solid pixel. The result is the new
   colour with the original's rim. See-through areas farther inside (glass, a veil) keep their restyled colour. Alpha
   is never touched. On 10085's wheel rim the near-white edge pixels went from 30 % to 0 %.

**A picture with letters or numbers** uses the same steps with a low strength (0.45), a strong outline hold (0.85) and
"keep the exact same letters and numbers, crisp readable lettering" added to the prompt.

## 4. Spine pages: part by part

A Spine page is many parts packed together, and its `.atlas` says where each one is. The page is **never restyled
as one picture**: neighbouring parts would bleed into each other and their edges would move, and the animation would
show seams. Instead, `restyle_game.py` restyles every part on its own:

1. **Cut out** at its rectangle. Spine 3.x uses `xy` / `size`; 4.x uses `bounds`; both are read. A part with
   `rotate: true` (or `rotate: 90`) is stored turned 90° counter-clockwise, so it is turned upright first, and "snow on
   top" lands on top.
2. **Restyled alone** at its own size, keeping its alpha. Trimmed parts (`orig` / `offset`) have only their trimmed
   pixels on the page; those are what is restyled.
3. **Following the object's look.** An object's parts use its already restyled static symbol as the reference
   (IP-Adapter, weight 0.35). The prompt names the part first ("only the brim, …").
   **A part that is a base under other parts** (an empty plate, a cake's plain body) gets its own description in the
   plan and **no reference**. Otherwise the reference paints the whole object onto it (sausages on the empty plate),
   and it shows twice once animated.
4. **Put back** exactly in its footprint, turned back if it was rotated. The pixels between parts are untouched.

Because every part keeps its place, size and alpha, **mesh attachments** (deforming parts) stay right too: their UVs
point into the same rectangle. Nothing in Spine needs re-exporting.

Parts are sorted by name into three kinds:

| Kind | Which parts | What happens |
|---|---|---|
| keep | names with an effect word (`fx`, `light`, `glow`, `flash`, `star`, `spark`, `smoke`, `shadow`, `bubble`, `liquid` … the plan's `keep_words`), **flipbook frames** (`name_00`, `name_01` …, the plan's `sequence_pattern`), parts under 40 px (`min_size`), a page's `keep_parts` (e.g. a character's face, skin and hair) | unchanged: a repaint breaks additive blending, frames repainted one by one would flicker, and faces drift |
| text | `text_names` (`A`, `K`, `wild`, `x10` …) and names starting with `text_prefixes` (`logo/`, `txt_`) | gently, the letters kept |
| object | everything else | fully, following the page's reference |

`restyle_game.py <plan> --dry-run` prints the kind of every part before anything runs.

## 5. Static pictures

Each picture listed in the plan's `static` (or matched by a `glob`) is restyled as in section 3, using its `kind`
(`object`, `text`, `frame`, `scene` or `keep`) and its one-line `subject`. Restyle the static pictures first, because
Spine pages use them as references; `restyle_game.py` does that order by itself. Engine files next to the pictures
are copied unchanged. For Cocos Creator these are `AutoAtlas.pac`, `.prefab` and `.meta`: Cocos packs the separate
PNGs at build time, so replacing the PNGs is enough.

## 6. The plan file

```jsonc
{
  "about": "one line: what this reskin is",
  "source": "TestData/10085",          // the original game, relative to AIGamestyle/ (or an absolute path)
  "output": "output/10085_zeus",       // where the reskinned copy goes (the original is never changed)
  "style": "ancient greek theme, white marble, gold laurel ...",   // added to every prompt
  "negative": "blurry, low quality, deformed, misspelled text ...", // what to avoid, every picture
  "kinds": {                           // strength (denoise) and outline hold (control) per kind
    "object": { "denoise": 0.8,  "control": 0.6 },
    "text":   { "denoise": 0.45, "control": 0.85, "prompt_extra": "keep the exact same letters and numbers, crisp readable lettering" },
    "frame":  { "denoise": 0.6,  "control": 0.7 },
    "scene":  { "denoise": 0.6,  "control": 0.55 },
    "keep":   {}
  },
  "static": [                          // first entry naming a file wins, so specific files go before globs
    { "file": "ab_maingame/textures/symbols/H3.png", "kind": "object", "subject": "a green alpine hat with a gold laurel band ..." },
    { "file": "ab_maingame/textures/game/guide_page.png", "kind": "keep" },
    { "glob": "ab_maingame/textures/panel/*.png", "kind": "text", "subject": "a game control button, ... keep its icon" }
  ],
  "spine": {
    "keep_words": ["fx", "light", "glow", "..."],
    "sequence_pattern": "_\\d{2,3}$",  // flipbook frames
    "min_size": 40,
    "text_names": ["A", "K", "Q", "J", "wild", "x10"],
    "text_prefixes": ["logo/", "txt_"],
    "reference_weight": 0.35,
    "pages": [
      {
        "atlas": "ab_maingame/spine/anim_sym_h3.atlas",
        "reference": "ab_maingame/textures/symbols/H3.png",   // its restyled static symbol (read from the output)
        "subject": "part of a green alpine hat in ancient greek style",
        "denoise": 0.78, "control": 0.45,                     // optional, for this page's object parts
        "keep_parts": ["head", "front bangs"],                // optional: left as they are
        "parts": {                                            // optional: one part's own description
          "tray": { "subject": "an empty round greek pottery plate, nothing on it", "reference": false, "denoise": 0.6 },
          "torso": { "keep_skin": true }                      // skin and clothes in one part: the skin stays as it was
        },
        "also": ["ab_maingame/spine/anim_role/anim_epic.atlas"]   // a note only, not read by the tool: see below
      }
    ]
  }
}
```

**Several atlases on one page.** A `.png` page is restyled once, even when several `.atlas` files point at it.
The first atlas listed decides the parts; `also` only records the others (10085's three waitress skeletons share
`pic.png` with identical regions). If another atlas cuts the same page into **different** regions, its extra parts
are not restyled, not even when it has its own `pages` entry. The tool does not support that case yet; check with the
`--dry-run` part lists.

The two 10085 plans are complete, working examples of every field.

## 7. Reskinning another game, step by step

1. **Set up the PC** ([Reskin_Setup_New_PC.md](Reskin_Setup_New_PC.md)) and start ComfyUI. `check_reskin_setup.py`
   should say `ready`.
2. **Copy the game's art folder** somewhere the tools can read, such as `AIGamestyle/TestData/<game>`. The tool copies it to
   the output and never writes to the source.
3. **Look at what is in it.** Static pictures (symbols, background, frames, buttons), Spine pages (`.atlas` + `.png`
   + `.json`/`.skel`), and engine files. Decide per picture: object, text, frame, scene, or keep. Keep pictures that
   are mostly text paragraphs (rules pages), pure black or shadow layers, and effect sheets.
4. **Write the plan.** Copy `zeus_10085.json` to `style_profiles/<style>_<game>.json`. Set `source`, `output`, `style`
   and `negative`, then write one `subject` per static picture. Describe **the same object** dressed in the new style
   ("a green alpine hat with a gold laurel band"), not a new object. List every Spine `.atlas` under `spine.pages` with
   a subject and its static symbol as `reference`.
5. **Dry run:** `python restyle_game.py ..\style_profiles\<plan>.json --dry-run`. Check how every Spine part sorts.
   - A face, skin or hair part listed as `object`: add it to `keep_parts`.
   - An effect listed as `object`: add its word to `keep_words`.
   - Lettering listed as `object`: add it to `text_names`.
   - A part that sits under other parts (plates, bodies, bases): give it its own `parts` entry with `"reference": false`.
6. **Pilot** three or four key pictures before the full run: the main symbol, a text symbol and the background.
   `python restyle_game.py <plan> --only symbols/H3`, then look at `<output>/_previews/sheet_01.png`. If an object only
   changes colour, raise `kinds.object.denoise` (0.72 → 0.8). If letters warp, lower `kinds.text.denoise`. Redo with
   `--force`.
7. **Full run:** `python restyle_game.py <plan>`. It is resumable: what is done is in `<output>/_restyle_report.json`
   and is skipped next time. A 69-picture game takes about 35 minutes on an RTX 4070.
8. **Review** every `<output>/_previews/sheet_NN.png` (before | after pairs). For a Spine object, also run
   `spine_page_preview.py` to see its parts put together. Redo a bad one with
   `--only <path part> --force`, after changing its subject, its `parts` entry, or `--seed`.
9. **Check it is a drop-in:** `python check_restyle.py <source> <output>`. It must report every picture with the same
   size and alpha, and every other file byte-identical.
10. **Try it in the game:** replace the game's folder with the output, or copy over the changed files, and play.

**Another style for a game already done:** copy its plan, change `style`, `negative`, the subjects and `output`, pilot
and run. Most of the Spine part rules (keep_parts, parts) carry over unchanged.

## 8. Lessons from the first runs

| Found | What to do |
|---|---|
| dark see-through buttons came out grey, soft edges got a grey fringe | fixed in the tool: the grey is taken out of semi-transparent pixels (section 3, step 5) |
| plates got the whole object painted on, a cake's plain body got its icing (parts under other parts) | per-part `subject` with `"reference": false`; name the part in the prompt; reference weight 0.35, not higher |
| white highlight frames got coloured | effect outlines: `keep_parts` |
| a character's dress barely changed | that page at strength 0.78 with a looser outline hold (0.45); face, skin and hair in `keep_parts` |
| objects only changed colour (Zeus pilot at 0.72) | `kinds.object.denoise` 0.8 |
| a new shape asked for (a wreath around a hat) did not appear | expected: the alpha is the original's, so a style shows through materials and patterns inside the shape. Ask for what can be inside |
| text pictures change little | the price of exact letters. A stronger change means a higher text strength and checking each letter |
| a static symbol and its Spine parts came out different (static at text strength, parts at object strength) | compare each static symbol with its Spine page on the sheets. Give such symbols their own kind between text and object (`badge`, 0.6 / 0.85), and plain Spine parts matching `parts` subjects |
| jagged white fringe on soft edges (hanging flags, the wheel's rim), seen in the game | fixed in the tool: the soft edges are rebuilt from the nearest solid pixels with the original's rim (section 3, step 6). Older outputs: `repair_edges.py <plan>` (both 10085 outputs were repaired, 2026-10-18). A ComfyUI "polish" pass would not help: the original alpha is put back after it, and the edge pixels would come out the same |
| the waitress's neck came out white (the `torso` part holds her neck, shoulders and dress top, and was restyled as clothes); in the game a white neck showed under her chin | the part's `"keep_skin": true`: after the restyle, the original's bare skin is put back (`restyle_pilot.keep_skin`). Skin is found by colour in the original: warm hue 8–40°, saturation 0.18–0.82 (shaded skin reaches 0.78; a red bodice beside it is near 0° and about 0.92), not dark. Only the biggest such area (and any other at least a quarter of its size) counts, so trim or an orange fold of the same hue stays restyled; the edge is feathered. Both 10085 outputs have it (2026-10-19). Watch any part where skin and clothes share one image |
| a "keep the old word" prompt was not obeyed | put the new style's own unwanted words in `negative` (the Zeus plan has `christmas, snow, modern`) |

## 9. New pixel-art sprites (make_sprites.py)

For a game with small pixel sprites, such as TOMS (32 × 32), `make_sprites.py` draws **new** sprites from
descriptions instead of restyling old ones.

- **New things** (creatures, people, items): SDXL base with the **pixel-art-xl LoRA** draws at 1024 on a plain
  background. IP-Adapter ("style transfer", weight 0.3) follows a sheet of the game's own sprites of that kind, and
  **Inspyrenet** removes the background. The picture is then shrunk to the tile, its colours are boosted back, alpha is
  made hard, the palette is cut to 8 colours, and a 1-pixel dark outline is drawn.
- **Variants of existing things** (doors, keys, stairs, walls, floors): **derived** from the game's own sprite. The
  sprite is turned grey, enlarged, changed by img2img at strength 0.8 with ControlNet canny, and shrunk back with the
  base's exact silhouette and outline pixels.

```
python make_sprites.py <out_dir> --plan ..\style_profiles\toms_sprites.json [--only <id prefix>] [--force] [--seed N]
python sprite_sheet.py sheet.png <out_dir> --big
```

The plan (`toms_sprites.json`, written by `make_toms_sprites_plan.py`) has categories with a `style`, a `reference`
(which game sprites the look follows), a `mode`, and for derived ones a `base` and `denoise`. How the sprites became
game data in TOMS is in the TOMS repository: `docs/27_AI_SPRITES.md`.

## 10. Results: game 10085

10085 contains:

| Folder | What | How it is restyled |
|---|---|---|
| `ab_maingame/textures/symbols/` | 19 symbols (270 × 240): 3 objects, WILD, A K Q J, ×1–×15, 2 wheel symbols, 3 tile backgrounds | one by one, as static pictures |
| `ab_maingame/textures/game/` | background (`main_bg.jpg`), reel frames, banners, the prize wheel | one by one; `black_back`, `bg_reel_right` (shadow) and `guide_page` (a paragraph of rules) are kept |
| `ab_maingame/textures/panel/` | 18 buttons with numbers or icons | as text pictures (the numbers kept) |
| `*/textures/*/AutoAtlas.pac` | Cocos Creator auto-atlas settings | copied unchanged |
| `ab_maingame/spine/`, `ab_freegame/spine/` | 22 Spine 3.8 skeletons on 19 pages | part by part |
| `ab_freegame/prefab/` | Cocos prefabs | copied unchanged |

**Christmas** (`christmas_10085.json`, output `output/10085_christmas`, 2026-10-17). The owner's choices:

- the same objects dressed for Christmas: snow, holly, candy-cane stripes, red / green / gold, and the waitress in a
  red dress with white fur;
- letters kept exact;
- effects unchanged.

Result: 63 of 69 pictures restyled (47 static, 16 Spine pages: 81 parts restyled, 227 kept) and 6 kept.
`check_restyle.py` passes. A full run takes about 35 minutes.

**Zeus / ancient Greek** (`zeus_10085.json`, output `output/10085_zeus`, 2026-10-18). The owner's choices:

- the same objects Greek-dressed;
- a marble, gold and sky-blue palette: white marble, gold laurel and Greek key patterns, Aegean blue, clouds, small
  gold lightning;
- effects unchanged;
- letters exact.

Objects run at strength 0.8. Result: 63 of 69 pictures restyled (47 static, 16 Spine pages: 81 parts restyled,
227 kept) and 6 kept. `check_restyle.py` passes. A full run took about 35 minutes. What changed:

- the background became a marble temple hall on Olympus;
- the waitress wears a white chiton with gold trim;
- the sausages sit on a painted Greek plate;
- the prize wheel is gold and marble;
- the symbol tiles, panels and reel frame are marble with gold.

The review found that the static ×-badges (text strength) had stayed wooden, while their Spine discs (object strength)
had turned to marble, one of them with a horse painted on. The symbol would change look when it animated. The fix:

- the badges got their own kind, `badge` (strength 0.6, outline hold 0.85, "keep the exact same number");
- the three plain Spine discs got matching `parts` subjects with `"reference": false`.

Both now show marble with the numbers unchanged.

## 11. Known limits

- **Large pictures are made at 1024 and scaled up** (`bg_reel.png` is 1574 × 1190), so they come out softer than the
  original. A tiled upscale pass would be the next step if it shows in the game.
- **Text pictures change only a little** at the strength that keeps letters exact.
- **Effects keep their colours.** A hue shift without AI is possible if wanted.
- **Results depend on the seed.** Each plan uses one seed (1225 by default, `--seed`). A bad picture is redone with
  `--only <path> --force`, or with another seed.
- **The game itself is not run by the tools.** Pictures are checked as pictures and as reassembled Spine parts; play
  the game to be sure.
- Only `.png`, `.jpg`, `.jpeg` and `.webp` are restyled. Other texture formats (`.ktx`, `.pvr`, `.astc`, …) must be
  converted first.
