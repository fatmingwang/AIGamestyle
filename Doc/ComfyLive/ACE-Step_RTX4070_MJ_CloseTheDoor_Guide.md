# ACE-Step 1.5 Prompt Guide

**Michael Jackson Vocal Style × Teddy Pendergrass – Close The Door**

**Optimized for RTX 4070 (12 GB VRAM)**

---

## 1. Hardware Recommendation for RTX 4070

The RTX 4070 has 12 GB of VRAM. According to official ACE-Step documentation, this is the minimum threshold for running the XL (4B) models. It works, but requires CPU offloading and is best used with the faster Turbo variant.

### VRAM Requirements Chart (XL Models)

| VRAM       | Support Status                  | Notes                                              |
|------------|---------------------------------|----------------------------------------------------|
| **≥ 12 GB**    | Usable (Offload + INT8)         | Minimum. RTX 4070 sits here. Slower generation.    |
| **≥ 16 GB**    | Usable (CPU Offload)            | More comfortable. Still needs offload.             |
| **≥ 20 GB**    | Recommended (No Offload)        | Best balance of speed and quality.                 |
| **≥ 24 GB**    | Ideal (Full Quality + 4B LM)    | Fastest + highest quality possible.                |

> **Your RTX 4070 (12 GB)** is at the minimum level. It can run XL models, but the **Turbo** version is strongly recommended for better speed and stability.

### Recommended Settings for RTX 4070

| Priority       | Model            | Why                                                      |
|----------------|------------------|----------------------------------------------------------|
| **1st Choice** | **XL Turbo**     | Fastest (8 steps). Best quality/speed balance on 12 GB.  |
| 2nd Choice     | XL SFT           | Higher quality but slower. Use for final renders only.   |
| Safe Fallback  | 2B Turbo / SFT   | Most stable & fast. Still excellent quality.             |

---

## 2. Optimized Prompt for RTX 4070

This version is tuned for the 12 GB card: slightly shorter duration, cleaner tags, and ready for **XL Turbo** (or XL SFT if you prefer quality over speed).

### Tags / Style Caption

Copy everything below into the **Tags** field:

```
smooth quiet storm R&B, sensual soul ballad, 1970s Philly soul, male tenor vocals in the style of Michael Jackson, high smooth tenor, soft falsetto, emotional intimate delivery, slight vocal hiccups, warm breathy tone, romantic, seductive, lush strings, soft electric piano, gentle bass, subtle drums, clean polished production, 72 bpm
```

### Lyrics (with structure tags)

Copy everything below into the **Lyrics** field:

```
[Verse]
Close the door
Let me give you what you've been waiting for
Baby, I got so much love to give
And I wanna give it all to you

[Verse]
Close the door
No need to worry no more
Let's bring this day to a pleasant end
Girl, it's me and you now

[Chorus]
I've waited all day long
Just to hold you in my arms
And it's exactly like I thought it would be
Me loving you, and you loving me

[Verse]
Close the door
Let me rub your back where you say it's sore
Come on, get closer and closer, so close to me
Let's get lost in each other
Come here, baby

[Chorus]
I've waited all day long
Just to hold you in my arms
And it's exactly like I thought it would be
Me loving you, and you loving me

[Outro]
Close the door, baby
Let me blow your mind
Plenty good lovin' all through the night
Come here, baby
Ooh baby, so good, so good, so good
```

---

## 3. How to Use in ComfyUI (RTX 4070)

1. Update ComfyUI to the latest version.
2. Go to **Template Library → Audio** → choose "ACE-Step 1.5 Music Generation" workflow (or the XL version if available).
3. Download the models: prefer `acestep_v1.5_xl_turbo_bf16.safetensors` for best speed on 12 GB.
4. Paste the **Tags** and **Lyrics** from Section 2 into the corresponding nodes.
5. Set **Duration** to **150–180 seconds** (shorter is safer on 12 GB).
6. Enable **CPU offload / low VRAM mode** if available, then generate.

### Extra Tips for 12 GB Cards

- Start with **XL Turbo**. Only switch to XL SFT when you want a higher-quality final render.
- Keep the Language Model (planner) on the smaller side (**0.6B** or **1.7B**).
- If you still get out-of-memory errors, fall back to the **2B Turbo** model — quality is still very good.
- Generate in shorter segments (2–3 minutes) first to test, then increase length.
- Close other GPU-heavy programs (browsers, games, other AI tools) before generating.

---

*— End of Guide —*

ACE-Step 1.5 is open-source (MIT License). Models available on Hugging Face / Comfy-Org.
