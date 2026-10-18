> 英文原文：[Reskin_Game_Art.md](Reskin_Game_Art.md)

# 用 AI 替遊戲美術換皮（restyle_game.py + ComfyUI）

如何用 `AIGamestyle/` 的本機 AI 流程替一款完成的遊戲換上新美術風格，讓每張新圖都能直接取代原圖：同檔案、同大小、
同透明度，Spine 動畫照常運作。要用同樣方法替**另一款遊戲**換皮，請讀本文；第 7 節是新遊戲的逐步流程。

- 安裝電腦（ComfyUI、自訂節點、模型與放置位置）：[Reskin_Setup_New_PC.zh-TW.md](Reskin_Setup_New_PC.zh-TW.md)
- 實例：遊戲 **10085**（啤酒節老虎機，Cocos Creator + Spine 3.8）的**聖誕**與**宙斯／古希臘**風格（第 10 節）。

## 1. 「可直接放入」的意思

換皮後的圖必須不動其他任何東西就能放進遊戲：

- **相同檔名、資料夾與像素大小。** 遊戲的版面、prefab 與圖集都參照它們。
- **相同 alpha。** 輪廓與柔邊完全不變，所以點擊範圍、遮罩與外框都對得上。流程只改顏色，並把原本的 alpha 通道複製回去。
- **Spine 仍然有效。** Spine 頁面的 `.atlas`（每個部件在哪裡）與骨架 `.json`／`.skel`（部件怎麼動）都不改；
  只改每個部件內的像素。
- **文字與數字保持可讀**（WIN、WILD、×10、A K Q J）。

由此可知：**換皮改變的是材質、顏色與花紋，不是形狀。** 物件輪廓外不能加東西（例如從帽子伸出來的月桂冠），
物件也不能變成另一種物件。Spine 是一個部件一個部件地動舊形狀，換成別的物件就對不上動畫。

## 2. 工具

都在 `AIGamestyle/_pipeline/tools/`。在 ComfyUI 執行中時，用 Python 3.11（Pillow + numpy）執行。

| 工具 | 功能 |
|---|---|
| `restyle_game.py <計畫檔>` | **主要工具**：複製遊戲，依計畫換皮每張圖，Spine 頁面逐部件處理；可續跑 |
| `restyle_pilot.py <圖> <輸出> --prompt ... --denoise 0.5 0.7` | 一張圖試多種強度，用來試風格 |
| `spine_page_restyle.py <頁面.atlas> <輸出> --prompt ...` | 一個 Spine 頁面逐部件處理（`restyle_game.py` 對每頁做的事） |
| `spine_page_preview.py <原始.atlas> <換皮資料夾> <輸出.png>` | 把頁面的部件組回去（前後對照），檢查整個物件 |
| `check_restyle.py <原始> <換皮>` | 證明輸出可直接取代：檔案、大小與 alpha 相同；其他檔案逐位元組相同 |
| `check_reskin_setup.py` | 檢查 ComfyUI 有需要的節點與模型（安裝文件第 7 節） |
| `make_sprites.py`、`sprite_sheet.py` | 依描述畫新的小像素圖（第 9 節） |

計畫檔是 `AIGamestyle/_pipeline/style_profiles/` 中的 JSON。它記錄所有選擇（風格、哪個檔案是什麼、強度），
所以新風格或新遊戲都不需要改程式。範例：`christmas_10085.json`、`zeus_10085.json`。

## 3. 一張圖怎麼換皮

`restyle_pilot.py`（其他工具都用它）：

1. **準備。** 把圖放在中性灰背景（128, 128, 128）上，縮放到長邊 1024（SDXL 的工作尺寸），兩邊都是 8 的倍數。
2. **換皮。** 以 **Juggernaut XL v9** 做 SDXL img2img，朝提示詞（`主題, 類別附加詞, 風格`）改畫。**ControlNet Union**
   的 canny 模式維持輪廓，所以物件形狀不變。取樣器 `dpmpp_2m_sde`、排程 `karras`、28 步、CFG 6。有兩個數值最重要：
   - **denoise**（強度）：圖可以變多少。物件約 0.7–0.8，文字 0.45。
   - **control**：輪廓維持多強。物件 0.6，文字 0.85。
3. **可選的參考圖。** IP-Adapter plus（權重 0.35，「linear」）讓結果跟隨另一張圖的外觀。用來讓一個物件的每個 Spine
   部件都配合該物件已換皮的符號。
4. **收尾。** 縮回原始大小，放回**原始 alpha**。
5. **把半透明像素中的灰色去掉。** 半透明像素是和灰色混在一起被換皮的（顯示色 = 顏色 × a + 灰 × (1 − a)），所以用
   (顯示色 − 灰 × (1 − a)) / a 還原它自己的顏色。少了這一步，深色半透明按鈕會變灰，每個柔邊都會有灰邊。alpha 低於
   8 % 的像素保留原色。

**有文字或數字的圖**用相同步驟，但強度低（0.45）、輪廓維持強（0.85），並在提示詞加上「keep the exact same letters
and numbers, crisp readable lettering」。

## 4. Spine 頁面：逐部件處理

一個 Spine 頁面是許多部件擠在一起，`.atlas` 記錄每個部件的位置。頁面**絕不能當成一張圖換皮**：相鄰部件會互相
滲色、邊緣會移動，動畫會出現接縫。所以 `restyle_game.py` 把每個部件單獨處理：

1. **依矩形切出。** Spine 3.x 用 `xy`／`size`，4.x 用 `bounds`，兩種都能讀。`rotate: true`（或 `rotate: 90`）的部件
   是逆時針轉 90° 存放的，所以先轉正，「頂上有雪」才會落在頂上。
2. **單獨換皮**，保持自己的大小與 alpha。被裁切的部件（`orig`／`offset`）在頁面上只有裁切後的像素，換皮的就是這些。
3. **跟隨物件外觀。** 物件的部件以該物件已換皮的靜態符號作參考（IP-Adapter，權重 0.35）。提示詞先寫部件名稱
   （「only the brim, …」）。
   **墊在其他部件下面的部件**（空盤子、蛋糕的素色本體）在計畫檔中有自己的描述，且**不用參考圖**。否則參考圖會把
   整個物件畫上去（空盤子上出現香腸），動起來時會出現兩次。
4. **放回**原本的位置，旋轉過的轉回去。部件之間的像素不動。

因為每個部件的位置、大小與 alpha 都不變，**網格附件**（會變形的部件）也正確：它們的 UV 指向同一個矩形。
Spine 不需要重新匯出。

部件依名稱分成三類：

| 類別 | 哪些部件 | 處理 |
|---|---|---|
| keep | 名稱含特效字（`fx`、`light`、`glow`、`flash`、`star`、`spark`、`smoke`、`shadow`、`bubble`、`liquid`……計畫檔的 `keep_words`）、**序列影格**（`name_00`、`name_01`……計畫檔的 `sequence_pattern`）、小於 40 px 的部件（`min_size`）、頁面的 `keep_parts`（例如角色的臉、皮膚與頭髮） | 不變：重畫會破壞加法混合，逐張重畫的影格會閃爍，臉會走樣 |
| text | `text_names`（`A`、`K`、`wild`、`x10`……）與以 `text_prefixes`（`logo/`、`txt_`）開頭的名稱 | 輕度處理，保留字母 |
| object | 其他所有部件 | 完整處理，跟隨頁面的參考圖 |

`restyle_game.py <計畫檔> --dry-run` 會在執行前列出每個部件的類別。

## 5. 靜態圖

計畫檔 `static` 中列出（或被 `glob` 比對到）的每張圖，依第 3 節處理，使用它的 `kind`（`object`、`text`、`frame`、
`scene` 或 `keep`）與一行 `subject`。先換皮靜態圖，因為 Spine 頁面以它們作參考；`restyle_game.py` 會自動依此順序。
圖片旁的引擎檔案原樣複製。Cocos Creator 的是 `AutoAtlas.pac`、`.prefab` 與 `.meta`：Cocos 在建置時才打包各張 PNG，
所以換掉 PNG 就夠了。

## 6. 計畫檔

```jsonc
{
  "about": "一行說明這次換皮",
  "source": "../tests/10085",          // 原始遊戲，相對於 AIGamestyle/（或絕對路徑）
  "output": "../tests/10085_zeus",     // 換皮後副本的位置（原始檔絕不更改）
  "style": "ancient greek theme, white marble, gold laurel ...",   // 加到每個提示詞
  "negative": "blurry, low quality, deformed, misspelled text ...", // 每張圖都要避免的
  "kinds": {                           // 每類的強度（denoise）與輪廓維持（control）
    "object": { "denoise": 0.8,  "control": 0.6 },
    "text":   { "denoise": 0.45, "control": 0.85, "prompt_extra": "keep the exact same letters and numbers, crisp readable lettering" },
    "frame":  { "denoise": 0.6,  "control": 0.7 },
    "scene":  { "denoise": 0.6,  "control": 0.55 },
    "keep":   {}
  },
  "static": [                          // 第一個指名某檔案的條目優先，所以個別檔案要放在 glob 前面
    { "file": "ab_maingame/textures/symbols/H3.png", "kind": "object", "subject": "a green alpine hat with a gold laurel band ..." },
    { "file": "ab_maingame/textures/game/guide_page.png", "kind": "keep" },
    { "glob": "ab_maingame/textures/panel/*.png", "kind": "text", "subject": "a game control button, ... keep its icon" }
  ],
  "spine": {
    "keep_words": ["fx", "light", "glow", "..."],
    "sequence_pattern": "_\\d{2,3}$",  // 序列影格
    "min_size": 40,
    "text_names": ["A", "K", "Q", "J", "wild", "x10"],
    "text_prefixes": ["logo/", "txt_"],
    "reference_weight": 0.35,
    "pages": [
      {
        "atlas": "ab_maingame/spine/anim_sym_h3.atlas",
        "reference": "ab_maingame/textures/symbols/H3.png",   // 已換皮的靜態符號（從輸出讀取）
        "subject": "part of a green alpine hat in ancient greek style",
        "denoise": 0.78, "control": 0.45,                     // 可選，用於這頁的 object 部件
        "keep_parts": ["head", "front bangs"],                // 可選：保持原樣
        "parts": {                                            // 可選：某部件自己的描述
          "tray": { "subject": "an empty round greek pottery plate, nothing on it", "reference": false, "denoise": 0.6 }
        },
        "also": ["ab_maingame/spine/anim_role/anim_epic.atlas"]   // 只是備註，工具不讀：見下方
      }
    ]
  }
}
```

**多個圖集共用一個頁面。** 即使有好幾個 `.atlas` 指向同一個 `.png` 頁面，它也只換皮一次。由第一個列出的圖集決定部件；
`also` 只是記錄其他圖集（10085 的三個女服務生骨架共用 `pic.png`，區域完全相同）。若另一個圖集把同一頁切成**不同**的
區域，它多出的部件不會被換皮，即使它有自己的 `pages` 條目也一樣。工具目前不支援這種情況；請用 `--dry-run` 的部件清單檢查。

10085 的兩個計畫檔是涵蓋所有欄位、可實際執行的完整範例。

## 7. 替另一款遊戲換皮，逐步流程

1. **安裝電腦**（[Reskin_Setup_New_PC.zh-TW.md](Reskin_Setup_New_PC.zh-TW.md)）並啟動 ComfyUI。
   `check_reskin_setup.py` 應顯示 `ready`。
2. **複製遊戲的美術資料夾**到工具讀得到的地方，例如 `tests/<遊戲>` 資料夾。工具會把它複製到輸出，絕不寫入來源。
3. **看看裡面有什麼。** 靜態圖（符號、背景、框、按鈕）、Spine 頁面（`.atlas` + `.png` + `.json`／`.skel`）與引擎檔案。
   逐張決定：物件、文字、框、場景，或保留。保留的有：大多是文字段落的圖（規則頁）、純黑或陰影圖層、特效圖。
4. **寫計畫檔。** 把 `zeus_10085.json` 複製成 `style_profiles/<風格>_<遊戲>.json`。設定 `source`、`output`、`style`
   與 `negative`，再替每張靜態圖寫一個 `subject`。描述換上新風格的**同一個物件**（「a green alpine hat with a gold laurel
   band」），而不是新物件。在 `spine.pages` 列出每個 Spine `.atlas`，寫上主題，並以它的靜態符號作 `reference`。
5. **試跑：** `python restyle_game.py ..\style_profiles\<計畫檔>.json --dry-run`。檢查每個 Spine 部件的分類：
   - 臉、皮膚或頭髮被列為 `object`：加到 `keep_parts`。
   - 特效被列為 `object`：把它的字加到 `keep_words`。
   - 文字被列為 `object`：加到 `text_names`。
   - 墊在其他部件下的部件（盤子、本體、底座）：給它自己的 `parts` 條目，並設 `"reference": false`。
6. **全面執行前先試做**三、四張關鍵圖：主要符號、一張文字符號與背景。
   `python restyle_game.py <計畫檔> --only symbols/H3`，然後看 `<輸出>/_previews/sheet_01.png`。物件只換了顏色就調高
   `kinds.object.denoise`（0.72 → 0.8）；字母變形就調低 `kinds.text.denoise`。用 `--force` 重做。
7. **全面執行：** `python restyle_game.py <計畫檔>`。可續跑：完成的記錄在 `<輸出>/_restyle_report.json`，下次會略過。
   69 張圖的遊戲在 RTX 4070 上約 35 分鐘。
8. **檢視**每張 `<輸出>/_previews/sheet_NN.png`（前後對照）。Spine 物件另外用 `spine_page_preview.py` 看部件組合起來的
   樣子。改了主題、`parts` 條目或 `--seed` 之後，用 `--only <路徑片段> --force` 重做不好的圖。
9. **確認可直接取代：** `python check_restyle.py <來源> <輸出>`。必須回報每張圖大小與 alpha 相同，其他檔案逐位元組相同。
10. **在遊戲中試玩：** 用輸出取代遊戲資料夾，或複製有變更的檔案，然後實際玩。

**替已做過的遊戲做另一種風格：** 複製它的計畫檔，改 `style`、`negative`、各主題與 `output`，試做後執行。
大部分 Spine 部件規則（keep_parts、parts）可直接沿用。

## 8. 前幾次執行的經驗

| 發現 | 做法 |
|---|---|
| 深色半透明按鈕變灰，柔邊有灰邊 | 工具已修正：去除半透明像素中的灰色（第 3 節第 5 步） |
| 盤子被畫上整個物件、蛋糕的素色本體被畫上糖霜（墊在下面的部件） | 每部件自己的 `subject` 並設 `"reference": false`；在提示詞寫出部件名稱；參考權重 0.35，不要更高 |
| 白色亮光影格被上色 | 屬於特效外框：放進 `keep_parts` |
| 角色的洋裝幾乎沒變 | 那一頁用強度 0.78、較鬆的輪廓維持（0.45）；臉、皮膚與頭髮放進 `keep_parts` |
| 物件只換了顏色（宙斯試做用 0.72） | `kinds.object.denoise` 改為 0.8 |
| 要求的新形狀（帽子外圍的花冠）沒有出現 | 正常：alpha 沿用原圖，所以風格只能透過輪廓內的材質與花紋表現。只要求輪廓內能出現的東西 |
| 文字圖片變化很小 | 保持字母完全相同的代價。要變化更大就得提高文字強度並逐字檢查 |
| 舊風格的字詞還是出現 | 把不想要的字詞放進 `negative`（宙斯計畫檔有 `christmas, snow, modern`） |

## 9. 新的像素圖（make_sprites.py）

對使用小像素圖的遊戲（例如 TOMS，32 × 32），`make_sprites.py` 依描述畫**新**圖，而不是替舊圖換皮。

- **新東西**（生物、人物、道具）：SDXL base 加 **pixel-art-xl LoRA** 在素色背景上以 1024 繪製。IP-Adapter
  （「style transfer」，權重 0.3）參考遊戲同類舊圖的拼圖，**Inspyrenet** 去背。接著縮到圖塊大小、調回鮮明顏色、
  alpha 改為硬邊、調色盤減到 8 色，並畫上 1 像素深色外框。
- **既有物件的變體**（門、鑰匙、樓梯、牆、地板）：從遊戲原圖**衍生**。原圖轉灰、放大，以強度 0.8 的 img2img 加
  ControlNet canny 改畫，再縮回並套用原圖的精確輪廓與外框像素。

```
python make_sprites.py <輸出資料夾> --plan ..\style_profiles\toms_sprites.json [--only <id 前綴>] [--force] [--seed N]
python sprite_sheet.py sheet.png <輸出資料夾> --big
```

計畫檔（`toms_sprites.json`，由 `make_toms_sprites_plan.py` 產生）的類別有 `style`、`reference`（外觀參考哪些遊戲圖）、
`mode`，衍生類別還有 `base` 與 `denoise`。這些圖如何在 TOMS 中變成遊戲資料，見 TOMS 原始碼庫的
`docs/zh_TW/27_AI_SPRITES.md`。

## 10. 結果：遊戲 10085

10085 的內容：

| 資料夾 | 內容 | 換皮方式 |
|---|---|---|
| `ab_maingame/textures/symbols/` | 19 個符號（270 × 240）：3 個物件、WILD、A K Q J、×1–×15、2 個轉輪符號、3 個底板 | 逐張，當作靜態圖 |
| `ab_maingame/textures/game/` | 背景（`main_bg.jpg`）、轉輪框、橫幅、獎勵轉盤 | 逐張；`black_back`、`bg_reel_right`（陰影）與 `guide_page`（一段規則文字）保留 |
| `ab_maingame/textures/panel/` | 18 個有數字或圖示的按鈕 | 當作文字圖（保留數字） |
| `*/textures/*/AutoAtlas.pac` | Cocos Creator 自動圖集設定 | 原樣複製 |
| `ab_maingame/spine/`、`ab_freegame/spine/` | 19 個頁面上的 22 個 Spine 3.8 骨架 | 逐部件 |
| `ab_freegame/prefab/` | Cocos prefab | 原樣複製 |

**聖誕**（`christmas_10085.json`，輸出 `tests/10085_christmas`，2026-10-17）。擁有者的選擇：

- 相同物件換上聖誕裝飾：雪、冬青、拐杖糖條紋、紅／綠／金，女服務生穿白毛邊紅洋裝；
- 字母保持完全相同；
- 特效不變。

結果：69 張中 63 張換皮（47 張靜態、16 個 Spine 頁面：81 個部件換皮、227 個保留），6 張保留。`check_restyle.py`
通過。全面執行約 35 分鐘。

**宙斯／古希臘**（`zeus_10085.json`，輸出 `tests/10085_zeus`，2026-10-18）。擁有者的選擇：

- 相同物件換上希臘裝扮；
- 大理石、金色與天藍配色：白色大理石、金色月桂與希臘回紋、愛琴海藍、雲朵、小金色閃電；
- 特效不變；
- 字母保持完全相同。

物件強度用 0.8。背景變成奧林帕斯山上的大理石神殿大廳，香腸放在彩繪希臘陶盤上。

## 11. 已知限制

- **大圖以 1024 製作再放大**（`bg_reel.png` 是 1574 × 1190），所以比原圖柔和。若在遊戲中看得出來，下一步是分塊放大。
- **文字圖在保持字母完全相同的強度下變化很小。**
- **特效保留原色。** 需要的話可以不用 AI 做色相偏移。
- **結果取決於種子。** 每個計畫檔用一個種子（預設 1225，`--seed`）。不好的圖用 `--only <路徑> --force` 或換種子重做。
- **工具不會執行遊戲本身。** 圖片是當作圖片、以及組回去的 Spine 部件來檢查；要確定請實際玩遊戲。
- 只處理 `.png`、`.jpg`、`.jpeg` 與 `.webp`。其他貼圖格式（`.ktx`、`.pvr`、`.astc`……）需先轉換。
