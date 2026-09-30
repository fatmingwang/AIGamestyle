# GameStyleAI — 指令參考手冊（繁體中文版）

本文件盤點 GameStyleAI 桌面應用程式（`App/`）可執行的每一個指令：實際執行了什麼（子行程／HTTP
呼叫／程式內邏輯）、執行前必須具備哪些條件，以及各步驟之間的先後順序。文件最後也說明新增的
**無介面（headless）模式**（`GameStyleAI --headless ...`），可在不啟動 Qt GUI 的情況下執行核心
素材處理管線。

> 本文為 [Commands_Reference.md](Commands_Reference.md) 的繁體中文翻譯版。若兩份文件內容有出
> 入，請以英文版為準（英文版會優先更新）。

資料來源：`App/src/*.cpp/.h`，以及它們所呼叫、位於 `_pipeline/tools/` 與 `TestData/` 下的
Python 腳本。文中的 file:line 參照對應到本次盤點時（2026-09-18）的程式碼；若這些檔案之後有異
動，行號可能會改變，請重新核對。

## 目錄

1. [架構總覽（一段話說明）](#架構總覽一段話說明)
2. [指令數量統計](#指令數量統計)
3. [Pipeline 分頁（核心 5 步驟管線）](#1-pipeline-分頁)
4. [Symbol Groups 分頁](#2-symbol-groups-分頁)
5. [Group Builder 分頁](#3-group-builder-分頁)
6. [Image With Prompt 分頁](#4-image-with-prompt-分頁)
7. [Text2Image 分頁](#5-text2image-分頁)
8. [Pose Generator 分頁](#6-pose-generator-分頁)
9. [Atlas 分頁](#7-atlas-分頁)
10. [Resource Replace 分頁](#8-resource-replace-分頁)
11. [先後順序／前置條件速查表](#9-先後順序前置條件速查表)
12. [本次盤點發現的既有缺陷與落差](#10-本次盤點發現的既有缺陷與落差)
13. [存在但未串接進 GUI 的腳本](#11-存在但未串接進-gui-的腳本)
14. [無介面（Headless）模式](#12-無介面headless模式)

---

## 架構總覽（一段話說明）

GUI 本身幾乎不做任何實際工作。每個按鈕不是（a）以特定的參數列（argv）與環境變數，透過
`QProcess` 啟動一個 Python 腳本，就是（b）直接發出 HTTP 請求（僅
`ResourceReplaceWidget` 如此），或是（c）在 C++ 端純粹做本地檔案／圖片處理
（`AtlasWidget`、JSON 匯入匯出、清單編輯等）。真正與 ComfyUI 對話（上傳圖片、透過其
REST／WebSocket API 排入生成任務）或與 LLM 後端（Ollama／OpenAI／Anthropic／本地的
llama.cpp 伺服器）溝通的，都是這些 Python 腳本。`MainWindow` 持有大部分分頁都需要的三項共用
設定——`pythonExe`、`projectRoot`、`comfyuiServer`——並透過 `setRunConfig()` 傳遞給各個分頁元
件。

## 指令數量統計

| 分頁／模組 | 使用者可觸發的獨立指令數 |
|---|---|
| Pipeline（MainWindow 核心） | 9 個（Scan、Classify、Link、Plan Style、ComfyUI Batch、Simple Batch、Launch ComfyUI、Run All、Run Step）+ Restart／Stop 兩個控制動作 |
| Symbol Groups | 4 個（Import/Export JSON、Apply→Pipeline、Run Item、Run Checked） |
| Group Builder | 7 個（Scan、New Group from Selected、Auto-Group by Prefix、Auto-Group by Vision、Check backend、Export JSON、Open in Symbol Groups） |
| Image With Prompt | 5 個（Import/Export JSON、Run Item、Run All、Stop） |
| Text2Image | 8 個（Import/Export JSON、Run Item、Run All、Parse Table→Generate Prompts、Analyze Style Image、Test backend、Wake Ollama、Wake LLaMA） |
| Pose Generator | 5 個（Test backend、Generate Pose Data、Generate Pictures、Generate Animation、Load/Save JSON） |
| Atlas | 2 個（Extract、Merge Back） |
| Resource Replace | 4 個（Info、Start Replace、Commit && Push、Upload Output to NAS） |
| **總計獨立指令數** | **約 44 個**，背後由 **17 種不同的 Python 腳本／內嵌探測程式** 以及 **3 個 HTTP 端點** 支撐 |

（「獨立指令」只計算會做出超越單純本地清單／文字編輯的使用者行為；像 Browse／Clear／Help 這類
純 UI 操作不計入統計，但仍會在各節中提及以求完整。）

---

## 1. Pipeline 分頁

核心 5 步驟管線，程式碼位於 `App/src/MainWindow.cpp` + `App/src/PipelineCommands.cpp`。

```
Scan  →  Classify  →  Link  →  Plan Style  →  ComfyUI Batch
（寫出 active_target.json     （寫出 step4_style_plan.json，
 + step1_assets_inventory.json）  以 scanned_asset_root 為鍵值）
```

`Simple Batch` 與 `Group Run`（見第 2 節）是兩條平行、獨立的路徑，會跳過
scan/classify/link/plan-style 的整套紀錄機制，直接送進 ComfyUI。

| # | 指令 | 實際執行內容 | 前置檢查 |
|---|---|---|---|
| 1 | **Scan** | `python -u _pipeline/tools/asset_pipeline.py --output-folder <out> scan <assetRoot>` | 輸出資料夾非空確認（`confirmOutputFolder`，MainWindow.cpp:1222） |
| 2 | **Classify** | `... classify [--target-folder]`（預設會讀取 `_pipeline/reports/active_target.json` 取得上次掃描的目標資料夾） | 必須先執行過 Scan——否則會拋出 `ValueError("No active target folder found...")`（asset_pipeline.py:222） |
| 3 | **Link** | `... link [--target-folder]` | 需要 Classify 產生的 `step1_assets_inventory.json` |
| 4 | **Plan Style** | `... plan-style --style-profile-id <id> --batch-id <id>` | 需要 Link 產生的 `step3_assets_linked.json` |
| 5 | **ComfyUI Batch** | `python -u _pipeline/tools/run_comfyui_batch.py --batch-id <id> --asset-root <root> [--output-dir] [--hero-reference] [--positive-prompt] [--negative-prompt] [--include-spine]` | (a) 輸出資料夾檢查、(b) **style-plan 新鮮度檢查**（`checkStylePlanFresh`，MainWindow.cpp:1270）：需要 Plan Style 產生的 `step4_style_plan.json`，其中記錄的 `scanned_asset_root` 必須與目前的 Asset Root 相符——若計畫過期，會跳出「重新產生／繼續使用／取消」對話框。(c) ComfyUI 伺服器必須是可連線的——**app 本身不會檢查**，只有在腳本連線失敗時才會顯示錯誤。 |
| 6 | **Simple Batch**（「Batch Reskin」） | `python -u TestData/Reskin/batch_reskin.py <assetRoot> <outputFolder>` | 僅有輸出資料夾檢查；完全跳過 scan/classify/link/plan-style；直接與 ComfyUI 溝通；每張圖片使用隨機（而非確定性）種子 |
| 7 | **Launch ComfyUI** | TCP 探測（`python -c "socket connect_ex(...)"`）→ 若無法連線，於 Windows 上執行 `cmd.exe /c _pipeline/tools/start_comfyui_with_flags.bat --enable-manager --listen 0.0.0.0`，並開啟新的主控台視窗。非 Windows 平台：未自動化——只會記錄訊息提示使用者手動啟動。 | `.bat` 檔案必須存在；ComfyUI 及其虛擬環境（venv）必須已安裝於 `_comfyui/ComfyUI/.venv` |
| 8 | **Run All** | 自動依序串接執行步驟 1→5，一旦有步驟回傳非 0 結束碼就停止（若模式為 Simple Batch，則只會執行步驟 6） | — |
| 9 | **Run Step** | 只執行目前選取的步驟，不會自動串接下一步 | — |
| — | Restart | 將步驟清單重置回第 0 步；不會啟動任何程序 | — |
| — | Stop | 終止正在執行中步驟的程序 | — |

**每個步驟共用的環境變數：** `COMFYUI_SERVER=<comfyuiServer>`、`PYTHONIOENCODING=utf-8`，工作
目錄為專案根目錄。每個腳本皆以 `python -u <script> <args>` 方式啟動（不緩衝輸出，因此 stdout
會即時串流顯示）。

**結構化進度協定：** `asset_pipeline.py` 與 `run_comfyui_batch.py` 會輸出
`[GS:STEP_START step=...]`、`[GS:PROGRESS step=... current=N total=M]`、
`[GS:STEP_DONE step=...]`、`[GS:STEP_DETAIL step=... json=...]`、
`[GS:ERROR step=... msg=...]` 這類標記行，由 `App/src/LogParser.cpp` 解析後驅動進度條與細節面
板。**`batch_reskin.py` 不會輸出這些標記**——它的輸出永遠只會以原始文字行的形式出現在紀錄
（log）中。

### Group Run（另一條會經過相同兩個腳本的路徑）

由 Symbol Groups 分頁的「Run Checked」觸發，但實際的子行程排程邏輯位於
`MainWindow::onRunGroups`／`runNextGroupItem`（MainWindow.cpp:875-1001）。詳見第 2.4 節。

---

## 2. Symbol Groups 分頁

`App/src/SymbolGroupWidget.cpp`。管理具名的圖片群組，每個群組各自擁有風格參考圖、IPAdapter
weight／denoise 設定，以及逐項的提示詞。

| # | 指令 | 實際執行內容 | 前置條件 |
|---|---|---|---|
| — | Import/Export JSON | 純檔案讀寫 | — |
| — | Apply → Pipeline | 將欄位複製到 Pipeline 分頁並切換過去（不會啟動任何程序） | — |
| 1 | **Run Item**（單一符號） | 依腳本模式下拉選單而定，執行 `run_imagewithprompt.py --reference <item> --output-dir <dir> --output-name <base> --ip-weight <w> --denoise <d> [--positive-prompt] [--negative-prompt]` **或** `batch_reskin.py <folderOfItem> <outDir>` | 已設定 `projectRoot`；腳本檔案存在；**ComfyUI 必須正在執行**（說明文字：*「請確認 ComfyUI 正在執行（Pipeline → Launch）」*，SymbolGroupWidget.cpp:114） |
| 2 | **Run Checked ▶ (N)** | 對每個「已勾選」群組中的每一個項目執行相同的兩支腳本，透過 `MainWindow` 唯一的 `groupProcess_`（MainWindow.cpp:919-1001）**依序**排隊執行。執行每個項目前，也會先寫出一份對應的臨時 `step4_style_plan.json`。 | 條件同上。**單一項目失敗不會中止整個佇列**——每個項目各自記錄成功／失敗，並繼續下一個（不同於主 Pipeline 一旦失敗就整體停止的行為）。 |

---

## 3. Group Builder 分頁

`App/src/GroupBuilderWidget.cpp`。用於將一個資料夾內的圖片，依檔名前綴或請具視覺理解能力的
LLM 進行分組。

| # | 指令 | 實際執行內容 | 前置條件 |
|---|---|---|---|
| 1 | Scan | 本地遞迴掃描檔案（不啟動任何程序） | 來源資料夾必須存在 |
| 2 | New Group from Selected | 純記憶體內操作 | 至少勾選 1 個檔案 |
| 3 | Auto-Group by Prefix | 記憶體內操作，依檔名開頭字母分組 | 檔案清單非空（即已執行過 Scan） |
| 4 | **Auto-Group by Vision** | `python -u _pipeline/tools/auto_group_symbols.py --folder <f> --output <f>/symbol_groups_auto.json --backend {ollama\|openai\|anthropic\|llama} [--api-url] [--model] [--reference] [--style-directive]` | 資料夾已掃描；腳本存在；**沒有自動的後端可連線性檢查**——失敗只會在啟動後透過 stderr／非 0 結束碼顯示 |
| 5 | **Check**（後端可連線性檢查） | `python -c "<針對 URL 欄位的 TCP connect_ex 探測>"`，環境變數 `CHECK_URL=<url>` | URL 欄位非空。**手動／選用**——並非 Auto-Group by Vision 執行前的必要條件，也不會被它自動呼叫 |
| 6 | Export JSON | 本地檔案寫入 | groups 非空 |
| 7 | Open in Symbol Groups | 發出 Qt signal 給 MainWindow，不啟動程序 | groups 非空 |

後端對應的預設 URL：`ollama` → `http://localhost:11434`、`openai` →
`https://api.openai.com/v1`、`anthropic` → `https://api.anthropic.com`、`llama`（本地
llama.cpp／LM Studio，OpenAI 相容介面）→ `http://127.0.0.1:8666/v1`。使用 `anthropic`／
`openai` 後端時需要環境變數 `ANTHROPIC_API_KEY`／`OPENAI_API_KEY`——**app 本身不會設定這些**。

---

## 4. Image With Prompt 分頁

`App/src/ImageWithPromptWidget.cpp`。針對單張圖片透過 ComfyUI IPAdapter 進行重繪風格化，並可
逐項覆寫類別／提示詞。

| # | 指令 | 實際執行內容 | 前置條件 |
|---|---|---|---|
| — | Browse / Import / Export JSON | 本地掃描／檔案讀寫 | — |
| 1 | **Run Item** | `run_imagewithprompt.py --reference <path> --output-dir <dir> --output-name <base> --seed <n> --steps <n> --cfg <f> --denoise <f> --ip-weight <f> [--positive-prompt \| --category] [--negative-prompt]` | 已設定 `projectRoot`；腳本存在；**ComfyUI 必須可連線**（此分頁不會事先探測——只有腳本端連線失敗時才會出錯） |
| 2 | **Run All** | 對每個「已勾選」項目依序執行相同腳本，共用同一個批次輸出資料夾 `output/IWP_batch_<timestamp>` | 項目清單非空；至少勾選 1 項；已設定 `projectRoot` |
| — | Stop | 終止執行中的程序，清空批次佇列 | — |

---

## 5. Text2Image 分頁

`App/src/Text2ImageWidget.cpp`。依據「名稱＋提示詞」清單生成圖片，並附帶可從貼上的表格
和／或一張風格參考圖，透過 AI 輔助建立該清單的工具。

| # | 指令 | 實際執行內容 | 前置條件 |
|---|---|---|---|
| — | Add/Remove/Import/Export JSON | 本地操作，不啟動程序 | — |
| 1 | **Run Item** | `run_text2image.py --prompt <p> --output-name <name> --output-dir <dir> [--width --height]` | 已設定 `projectRoot`；提示詞非空（若為空則該項目會被跳過，而非視為失敗）；**ComfyUI 必須可連線**（不會事先探測） |
| 2 | **Run All** | 對清單中「每一個」項目依序執行相同腳本（此分頁沒有逐項勾選框） | 項目清單非空；已設定 `projectRoot` |
| 3 | **Test**（於「AI Parse Table」對話框內） | `python -c "<針對 API URL 的 TCP 探測>"` | URL 欄位非空 |
| 4 | **Analyze ▶**（風格參考圖 → 風格提示） | `analyze_style_image.py --image <path> --backend <b> --api-url <url>` | 已選擇風格參考圖；腳本存在 |
| 5 | **Generate Prompts**（解析貼上的表格） | 先將貼上的 TSV 文字寫入暫存檔，再執行 `parse_table_to_items.py --table <tmp> --output <out.json> --backend <b> --api-url <url> [--style <hint>]` | 表格文字非空；腳本存在；**所選後端必須可連線**（Ollama／llama／OpenAI／Anthropic——不會事先檢查） |
| 6 | **Wake Ollama** | 對 `127.0.0.1:11434` 進行 TCP 探測；若未啟動，執行 `ollama serve`（須已在 PATH 中），以 detached 方式啟動；3 秒後重新探測一次 | 無——若系統未安裝 `ollama`，只會以狀態訊息軟性失敗 |
| 7 | **Wake LLaMA** | 對 `127.0.0.1:8666` 進行 TCP 探測；若未啟動，會尋找 `run_llama.bat`（先讀已儲存路徑 → 再試常見相對路徑 → 最後跳出檔案選取視窗），並以 `cmd.exe /k <bat>` 啟動——這是一個**互動式主控台**，使用者必須在其中手動選擇模型編號 | bat 檔案必須能被找到；**由於批次檔的選單需要人為互動，此指令無法在不改寫該批次檔的情況下做到全無介面化** |

Parse Table／Analyze 用的後端預設值：`anthropic` → `https://api.anthropic.com`、`ollama` →
`http://localhost:11434`（注意：與 Wake Ollama 按鈕固定探測的
`127.0.0.1:11434` 不同）、`openai` → `https://api.openai.com/v1`、`llama` →
`http://127.0.0.1:8666/v1`。

---

## 6. Pose Generator 分頁

`App/src/PoseGeneratorWidget.cpp`。依「Subject 類型」下拉選單分成兩條獨立的子管線。

| # | 指令 | 實際執行內容 | 前置條件 |
|---|---|---|---|
| 1 | Test（後端 URL） | TCP 探測單行指令 | 無 |
| 2 | **Generate Pose Data (AI)** — 骨架管線 | `generate_pose_from_prompt.py --name <n> --description <d> --subject-type {human\|biped_animal} --loop <bool> --frames <n> --backend <b> --api-url <url> --output <poses_library>/<n>.json` | 已設定 `projectRoot`；Name 與 Description 皆已填寫；腳本存在；若判定為非雙足生物，會拒絕生成（結束碼 1），並提示改用 Creature 管線 |
| — | Load JSON / Save to Library | 本地檔案讀寫 | — |
| 3 | **Generate Pictures** — 骨架渲染 | `generate_pose_sequence.py --animation <json> --facing <l\|r> --output-dir <dir> --prefix pose_<n> [--frames]` | 已存在姿勢 JSON（來自步驟 2 或 Load JSON）；純本地渲染，**不需要 ComfyUI，也不需要任何 AI 後端** |
| 4 | **Generate Animation (Video AI)** — 生物管線 | `generate_creature_animation.py --reference-image <path> --motion-prompt <t> [--negative-prompt] --name <n> --frames <n> --duration <5\|10> --aspect-ratio <r> --model-name <kling-*> --mode <std\|pro> --output-dir <dir>` | 參考圖片必須已存在（例如來自 Text2Image 分頁）；**ComfyUI 必須正在執行，且已安裝 Kling 影片節點，並且已在 ComfyUI 內設定好 comfy.org 帳號／API 授權（`auth_token_comfy_org`）**——這是一個以點數計費的付費雲端節點；該元件與腳本都不會替使用者設定此授權資訊 |

---

## 7. Atlas 分頁

`App/src/AtlasWidget.cpp`。是唯一**完全不涉及子行程／Python** 的分頁——純粹在 C++ 端用
`QImage`／`QPainter` 處理 Spine／libGDX 的 `.atlas` 精靈圖集。

| # | 指令 | 前置條件 |
|---|---|---|
| **Extract** | 將選定 `.atlas` 檔案中的每個精靈區塊拆解成獨立 PNG，放到 `<atlasDir>/atlas<scaleTag>/` 底下，並附帶一份重新縮放後的 `.atlas` 檔。目的是在把這些 PNG 送進 ComfyUI 重繪風格化**之前**先執行（可透過 Pipeline 分頁的「Include Spine Textures」或 Symbol Groups）。 |
| **Merge Back** | 將（可能已重繪風格化過的）各個精靈 PNG 重新合併回整張圖集。需要先前 Extract 產生、且使用**相同 Scale 數值**的對應 `atlas<scaleTag>/` 資料夾；應在 ComfyUI 重繪步驟**之後**執行。個別缺失的 PNG 只會被記錄為 `[Missing]` 並在合併結果中留空透明，不會中止整個合併；覆寫既有檔案時的逐檔提示也可以選擇「Cancel」跳過（會記錄為 `[Cancel] Skipping merge save`，這並非錯誤）。 |

---

## 8. Resource Replace 分頁

`App/src/ResourceReplaceWidget.cpp`。是唯一**直接使用 HTTP**（而非 CLI 腳本）的分頁。所有請
求都停用了 TLS 憑證驗證（以容忍自簽憑證），且**完全沒有任何登入／認證步驟**——每個呼叫都是
匿名請求。

| # | 指令 | 呼叫內容 | 前置條件 |
|---|---|---|---|
| 1 | Info | `GET <server>/api/sharedcode` | 伺服器欄位非空；若失敗則改用系統瀏覽器開啟該網址 |
| 2 | **Start Replace** | `POST <server>/api/onlineGameResourceReplace`，內容為 `{nasRoot, targetFolder, gameId, platformName, deployEnv:[...], userName}`，會依序嘗試多組候選網址（`https://host:3443` → 原樣輸入 → `http://...`） | NAS root／target folder／game ID／user name 皆已填寫；Dev／QA 至少勾選一項；伺服器欄位已填寫。**不會**強制要求先完成 NAS 上傳（僅為建議流程，非程式碼強制） |
| 3 | **Commit && Push** | `POST <server>/api/resourceCommitPush`，內容為 `{nasRoot, targetFolder, gameId, userName, commitMessage}` | 需先通過「是／否」確認對話框；其餘欄位檢查同上。理論上應在 Start Replace 驗證確認之**後**才執行（僅為使用慣例，非程式碼強制） |
| 4 | **Upload Output to NAS** | `robocopy <outputFolder> <nasTarget> /E /IS /IT`（本地／UNC 檔案複製，非 HTTP） | 尚未有其他上傳作業進行中；本地輸出資料夾存在；NAS 目標已設定。建議在 Start Replace／Commit && Push **之前**先執行（因為這兩者的 `nasRoot`／`targetFolder` 欄位預期檔案已經放在該處） |

預設伺服器位址：`https://localhost:3443`。可連線性是以被動方式偵測的（也就是嘗試完所有候選
網址後 POST 仍失敗）——那句
`"Cannot connect to server: %1\n\nPlease check the server is running."` 的錯誤訊息，正是來自
`tryUrls()` 把候選清單全部試完仍失敗時所顯示的內容。

---

## 9. 先後順序／前置條件速查表

| 想執行這個... | ...必須先完成 |
|---|---|
| Classify | 已執行 Scan（需要 `active_target.json` + `step1_assets_inventory.json`） |
| Link | 已執行 Classify |
| Plan Style | 已執行 Link |
| ComfyUI Batch | 已用**相同的 Asset Root** 執行過 Plan Style（新鮮度會在前端與 `run_comfyui_batch.py` 內部各檢查一次）；**ComfyUI 正在執行** |
| Simple Batch | 不需要前面的 pipeline 步驟，但**ComfyUI 必須正在執行** |
| Symbol Groups：Run Item／Run Checked | **ComfyUI 正在執行**（請先到 Pipeline 分頁啟動它） |
| Image With Prompt：Run Item／All | **ComfyUI 正在執行** |
| Text2Image：Run Item／All | **ComfyUI 正在執行** |
| Pose Generator：Generate Pictures | 已先完成 Generate Pose Data（或 Load JSON） |
| Pose Generator：Generate Animation | 已先產生參考圖片（例如透過 Text2Image）；**ComfyUI 正在執行，且已設定好 Kling 節點與 comfy.org 授權** |
| Text2Image：backend=`ollama` 的 Parse Table／Analyze Style | 已執行 Wake Ollama（或 Ollama 已在執行中） |
| Text2Image：backend=`llama` 的 Parse Table／Analyze Style | 已執行 Wake LLaMA，並在其主控台中選好模型且完全載入完成 |
| Group Builder：Auto-Group by Vision | 所選後端伺服器可連線（此為軟性要求，不會被強制檢查） |
| Atlas：Merge Back | 已用**相同 Scale** 執行過 Atlas：Extract，且中間通常已完成一輪 ComfyUI 重繪風格化 |
| Resource Replace：Start Replace／Commit && Push | 已執行 Upload Output to NAS（僅為流程慣例，非程式碼強制） |
| Launch ComfyUI | ComfyUI 及其 Python 虛擬環境必須已安裝於 `_comfyui/ComfyUI/.venv`（Windows：透過 `start_comfyui_with_flags.bat`；GUI 在 Linux／macOS 上沒有自動啟動路徑） |

---

## 10. 本次盤點發現的既有缺陷與落差

以下都是**目前 GUI 中既有的行為**，為避免改變既定行為而維持原樣，但值得留意：

1. **Pipeline 分頁的 ComfyUI Batch 步驟會悄悄忽略 `--ip-weight`／`--denoise`／`--steps`。**
   `run_comfyui_batch.py` 其實三個都支援（run_comfyui_batch.py:431-433），Pipeline 分頁上也有
   對應的三個數值調整欄位，但 `PipelineCommands::buildStep()`
   （PipelineCommands.cpp:37-51）從未把它們傳遞過去——這三個值只會被讀寫進 `QSettings`，實際
   上只在 **Group Run**（第 1 節的 Group Run／第 2 節的 Run Item/Checked）中才真正生效，對
   單純的「ComfyUI Batch」pipeline 步驟並不生效。*無介面模式（第 12 節）修正了這一點，只要使
   用者明確傳入該旗標，就會一併轉送過去。*
2. **在某條路徑中，`--hero-reference` 被傳給一個不接受此參數的腳本。**
   `MainWindow::runNextGroupItem`（Symbol Groups → Run Checked）在群組有設定參考圖時，會傳
   `--hero-reference <group.referencePath>` 給 `run_imagewithprompt.py`
   （MainWindow.cpp:986-988），但該腳本的 argparse 並沒有這個參數
   （`run_imagewithprompt.py:250-289`）。任何在 `imagewithprompt` 模式、且群組有設定參考圖的
   「Run Checked」批次執行，該項目都會因「unrecognized arguments」錯誤而失敗——會被記錄為
   FAILED，佇列則會繼續處理下一個項目而不會整個中止。
3. **`COMFYUI_SERVER` 存在兩種不同的 Python 端預設值。** Qt app 的 C++ 端預設值是
   `127.0.0.1:8188`；而共用的 Python 輔助模組
   （`TestData/ComgyUIPythonCommonFunction.py:10`）在環境變數未設定時，會回退到
   `localhost:8188`（一個特定的區網位址）。在正常的 GUI 操作下 app 一定會設定此環境變
   數，所以不會觸發此情況——但若在 app 之外手動執行這些腳本、且未設定
   `COMFYUI_SERVER`，就會悄悄指向該區網主機，而非本機。
4. **`batch_reskin.py` 不會輸出任何 `[GS:...]` 進度標記**，因此 Simple Batch 執行時永遠不會
   填入結構化的進度條／細節面板，只會顯示原始文字紀錄。
5. **在啟動任何生成步驟之前，都不會檢查 ComfyUI 是否可連線**（包含 ComfyUI Batch、Simple
   Batch、Image With Prompt、Text2Image、Symbol Groups、Pose Generator 的動畫步驟）——只有專
   屬的「Launch ComfyUI」按鈕會先做探測。其餘所有路徑都只能透過所啟動腳本本身的連線失敗，才
   會發現 ComfyUI 無法連線。
6. **`start_comfyui_with_flags.bat` 自身的 `--listen 127.0.0.1` 會被 app 額外附加的
   `--listen 0.0.0.0` 覆蓋掉**，因為 app 傳入的旗標會被接在 bat 檔自帶旗標之後，而 ComfyUI 的
   argparse 會以最後出現的值為準——最終實際生效的監聽位址會是 `0.0.0.0`，而非 bat 檔原本預設
   的位址。

---

## 11. 存在但未串接進 GUI 的腳本

以下位於 `_pipeline/tools/` 之下——對自動化／開發工作很有用，但目前沒有任何按鈕會呼叫它們：

| 腳本 | 用途 |
|---|---|
| `asset_pipeline.py visual-link` | 以 CLIP 圖像嵌入做視覺相似度比對，將 static 與 spine 資產互相連結（是 `link` 步驟以群組名稱比對方式的替代／補充做法） |
| `asset_pipeline.py render-visual-previews` | 為視覺連結後的群組建立合併預覽圖 |
| `check_style_pipeline.py` | 就緒狀態檢查：git／python 是否存在、ComfyUI 與其 venv 是否已安裝、checkpoint／ControlNet／IPAdapter／CLIP-Vision 模型是否存在、ComfyUI 埠號是否可連線。**無介面模式的 `check` 指令（第 12 節）正是包裝這支腳本。** |
| `run_pipeline.py` | 一支簡易的 `scan+classify+link+plan-style` 一次性包裝腳本——功能已被 `asset_pipeline.py all` 以及現在的無介面模式 `all` 指令取代 |
| `smoke_test.py` | 依序執行 scan→classify→link→plan-style 並輸出合併後的 JSON 結果——是一個基本的自我測試，並非任何分頁的一部分 |
| `comfy_style_runner.py` | 較底層的 ComfyUI workflow 修補／排入佇列輔助工具；`run_comfyui_batch.py` 並未引用（import）它，GUI 也不會呼叫它 |

---

## 12. 無介面（Headless）模式

`GameStyleAI --headless <command> [--flag value ...]` 可在不啟動任何 GUI 的情況下執行**核心
5 步驟管線**（第 1 節）——不會建立 `QApplication`、不會開啟視窗，也不依賴先前桌面 GUI 執行時
留下的 `QSettings`。它重用了與 GUI 完全相同的
`AppConfig`／`PipelineCommands::buildStep()` 程式碼，因此為每支 Python 腳本組出的參數列，與
GUI 實際會執行的完全一致。

實作位置：`App/src/HeadlessRunner.h/.cpp`，由 `App/src/main.cpp` 接線啟用——若 `argv[1] ==
"--headless"`，app 會建立 `QCoreApplication`（而非 `QApplication`），並進入
`runHeadless()`，整個流程完全是同步執行（每個 Python 子行程都使用
`QProcess::ForwardedChannels` + `waitForFinished(-1)`，讓其 stdout/stderr 即時直接串流輸出——
不需要事件迴圈）。在 Windows 上，此執行檔仍以 `WIN32`（無主控台）方式建置以配合 GUI 需求；無
介面模式只有在完全沒有任何 stdout/stderr handle 存在時，才會重新附掛到父行程的主控台，因此不
會干擾 shell 端既有的輸出重導向（例如 `> log.txt`、CI 執行環境等）。

### 一套核心邏輯、兩種前端介面、以 JSON 溝通

`App/src/PipelineEngine.h/.cpp` 是唯一負責（a）判斷輸出資料夾／style plan 這兩道關卡是否通
過，以及（b）為某個 pipeline 步驟組出實際程式／參數／環境變數的地方。`MainWindow`（GUI）與
`HeadlessRunner`（CLI）都呼叫同樣的四個函式，取得的都是單純的 `QJsonObject` 判斷結果／執行請
求——兩邊都不會各自再重新推導一次這些邏輯：

| `PipelineEngine` 函式 | 回傳內容（JSON） | GUI 拿到後怎麼用 | CLI 拿到後怎麼用 |
|---|---|---|---|
| `checkOutputFolderState(cfg)` | `{applicable, targetFolder, empty, entryCount}` | 當 `empty` 為 false 時，跳出 Delete／Overwrite／Cancel 的 `QMessageBox` | 當 `empty` 為 false 時，需要 `--overwrite`／`--clean`，否則以結束碼 2 失敗 |
| `checkStylePlanState(cfg)` | `{applicable, planPath, planExists, planRoot, fresh, reason}` | 當 `fresh` 為 false 時，跳出 Regenerate／Continue／Cancel 的 `QMessageBox` | 當 `fresh` 為 false 時，需要 `--regenerate`／`--continue-stale`，否則以結束碼 3 失敗 |
| `buildStepRequest(step, cfg, projectRoot, extra)` | `{step, program, args:[...], workingDirectory, env:{...}}` | 把這些欄位套用到自己既有的非同步 `process_` 上（藉此保留進度條／Stop 按鈕／即時 `[GS:...]` 標記解析等功能） | 直接把這份 JSON 傳給 `runStepRequestSync()` |
| `runStepRequestSync(request, onEvent?)` | `{started, exitCode, success, error}` | *（GUI 不會用到——GUI 仍維持非同步執行以支援即時進度顯示）* | 同步執行該步驟，並在 stderr 印出 `[headless] ...` 的說明訊息；若有加上 `--json`，還會多傳入一個 `onEvent` callback，把每一行輸出轉成 stdout 上的 `marker`／`log` JSON 事件（見下方「`--json`」一節） |

`configToJson`／`configFromJson` 也讓 `AppConfig` 在兩端共用同一種 JSON 結構，這正是
`--config <path.json>` 所載入的格式。

實際效果：「輸出資料夾是否可以寫入」只有一套判斷邏輯、「style plan 是否過期」只有一套判斷邏
輯、「步驟 X 實際上會執行什麼指令」也只有一套組出方式——GUI 與無介面 CLI 各自只是替這一套會
用 JSON 溝通的核心邏輯包上不同的前端（對話框 vs. 旗標／結束碼），而不是各自維護一份相同判斷邏
輯的複本。

### 涵蓋範圍

無介面模式僅涵蓋 Pipeline 分頁（第 1 節）：`check`、`comfyui-status`、`comfyui-launch`、
`scan`、`classify`、`link`、`plan-style`、`comfyui-batch`、`simple-batch`、`all`。其餘分頁
（Symbol Groups、Group Builder、Image With Prompt、Text2Image、Pose Generator、
Resource Replace、Atlas）目前仍僅限於 GUI 使用——它們本質上是需要人在迴圈中檢視圖片與 JSON
的工作流程／編輯工具，而非批次自動化流程；而且 Text2Image 的「Wake LLaMA」明確需要在互動式
主控台中選擇模型編號。若日後真的需要在建置伺服器上自動化這些功能，第 2～8 節中每項指令的完整
盤點資訊已足以用相同方式擴充。

### 指令

| 指令 | 對應的 GUI 動作 | 備註 |
|---|---|---|
| `check` | —（就緒狀態檢查） | 包裝 `_pipeline/tools/check_style_pipeline.py`；加上 `--json` 可取得機器可讀格式的輸出 |
| `comfyui-status` | 「Launch ▶」中的可連線性檢查部分 | 印出 `reachable`／`unreachable`，結束碼分別為 0／1 |
| `comfyui-launch` | 「Launch ▶」 | 先探測；若無法連線則啟動它（Windows：使用既有的 `.bat`；Linux／macOS：由於這些平台沒有對應的 `.bat`，改為直接執行 `_comfyui/ComfyUI/.venv/bin/python main.py`），接著最多輪詢 `--wait-timeout` 秒（預設 60 秒） |
| `scan` | Scan | 受輸出資料夾檢查限制 |
| `classify` | Classify | |
| `link` | Link | |
| `plan-style` | Plan Style | |
| `comfyui-batch` | ComfyUI Batch | 受輸出資料夾檢查與 style-plan 新鮮度檢查限制 |
| `simple-batch` | Simple Batch | 受輸出資料夾檢查限制 |
| `all` | Run All（完整管線） | 依序串接 scan→classify→link→plan-style→comfyui-batch，一旦有步驟回傳非 0 結束碼即停止 |

### 非互動式確認機制

GUI 在輸出資料夾非空、或既有 `step4_style_plan.json` 看起來已過期時，會跳出 `QMessageBox` 對
話框。無介面模式沒有人可以點擊按鈕，因此改為需要明確傳入旗標：

- 輸出資料夾非空 → 傳入 `--overwrite`（仍照樣寫入）或 `--clean`（先刪除該資料夾）；否則指令
  會以結束碼 2 失敗，並顯示清楚的錯誤訊息。
- style plan 已過期（僅 `comfyui-batch` 會檢查此項）→ 傳入 `--regenerate`（會重新依序執行
  scan→classify→link→plan-style→comfyui-batch，等同於呼叫 `all`）或 `--continue-stale`
  （仍沿用既有計畫繼續執行）；否則結束碼為 3。

### 旗標

```
--config <path.json>          從 JSON 檔案載入預設值（欄位名稱與 AppConfig 相同：
                               projectRoot、outputFolder、assetRoot、heroReference、
                               positivePrompt、negativePrompt、styleProfileId、batchId、
                               comfyuiServer、pythonExe、ipAdapterWeight、denoise、steps、
                               includeSpine）。範例請見
                               App/headless_config.example.json。
--project-root <dir>          專案根目錄，內含 _pipeline/、TestData/、_comfyui/。
--output-folder <dir>
--asset-root <dir>
--style-profile-id <id>
--batch-id <id>
--comfyui-server <host:port>  預設為 127.0.0.1:8188。
--python-exe <path>           預設為 "python"（Windows）／"python3"（其他平台）。
--hero-reference <path>       僅用於 comfyui-batch / all。
--positive-prompt <text>      僅用於 comfyui-batch / all。
--negative-prompt <text>      僅用於 comfyui-batch / all。
--include-spine               僅用於 comfyui-batch / all。
--ip-weight <0..1>            僅用於 comfyui-batch / all——會轉送給 run_comfyui_batch.py
--denoise <0..1>               （對照前述缺陷 #1：GUI 不會轉送這三個參數；無介面模式會轉送，
--steps <n>                    但僅在使用者確實有傳入該旗標時才會轉送）。
--overwrite / --clean          輸出資料夾檢查機制（見上）。
--regenerate / --continue-stale  style-plan 新鮮度檢查機制，僅用於 comfyui-batch（見上）。
--wait-timeout <seconds>      僅用於 comfyui-launch，預設 60。
--json                        額外在 stdout 輸出一份 NDJSON 格式的工作進度／結果協定，供其他
                               程式解析（見下方「--json」一節）。所有指令皆可使用，不僅限於
                               check。
```

**此模式刻意不會讀取 GUI 的 `QSettings`**——無介面執行應該只依靠命令列參數／`--config` 檔案
本身即可完全重現，而不受某個桌面使用者上次在 GUI 儲存了什麼設定影響。

### 範例

```sh
# 在排程任何工作之前，先做一次就緒狀態檢查
./GameStyleAI --headless check --project-root /srv/gamestyleai --json

# 確認 ComfyUI 已啟動，最多等待 2 分鐘
./GameStyleAI --headless comfyui-launch --project-root /srv/gamestyleai --wait-timeout 120

# 使用已存好的設定檔執行完整管線，只覆寫 batch id
./GameStyleAI --headless all \
    --config /srv/gamestyleai/nightly_build.json \
    --batch-id "nightly_$(date +%Y%m%d)" \
    --overwrite

# 針對已是最新的計畫，只重新執行 ComfyUI 那一步
./GameStyleAI --headless comfyui-batch --config nightly_build.json --continue-stale

# 讓另一支程式啟動這個指令，並讀取結構化的進度／結果輸出（見下方「--json」一節）
./GameStyleAI --headless all --config nightly_build.json --overwrite --json
```

結束碼說明：`0` 成功；`1` 所啟動的 Python 腳本本身執行失敗；`2` 前置條件／設定有問題（輸出資
料夾異常、檔案缺失等）；`3` style plan 已過期，且未傳入 `--regenerate` 或
`--continue-stale`；`64` 未知指令／使用方式錯誤。

### `--json`：讓其他程式呼叫本工具的工作進度／結果協定

在任何無介面指令後面加上 `--json`，除了原本會印在 **stderr** 的人類可讀 `[headless] ...` 訊息
（完全不受影響，照常輸出）之外，程序還會在 **stdout** 額外輸出一行一個、格式緊湊的 JSON 物
件——也就是 [NDJSON](http://ndjson.org/) 格式——用來描述這個工作（job）執行過程中發生的事。
這是設計給另一支應用程式使用的：把 `GameStyleAI --headless ... --json` 當作子行程啟動，逐行讀
取其 stdout，就能知道工作進行到哪裡、結果如何，而不必去解析人類閱讀用的文字說明。若不加
`--json`，行為完全不變：stdout 仍然只是原封不動轉發所啟動 Python 腳本印出的內容（與加入這項
功能之前完全相同）。

**stdout 上的每一行都是一個完整、獨立的 JSON 物件——請逐行讀取，而不是把整個輸出當成一份大
JSON 文件解析。** 每個物件都有一個 `"type"` 欄位：

| `type` | 何時輸出 | 欄位 |
|---|---|---|
| `job_start` | 一開始只會出現一次 | `command` |
| `step_start` | 每個 pipeline 步驟各一次 | `step`、`index`、`total`（在這個工作中的第幾步，從 1 開始——例如 `all` 的第 2／5 步） |
| `marker` | 腳本每印出一行 `[GS:...]` 標記就會出現一次 | `step`、`marker`（`STEP_START`\|`PROGRESS`\|`STEP_DONE`\|`STEP_DETAIL`\|`ERROR`），並依標記種類附上 `scriptStep`／`message`／`current`／`total`／`detail` 等欄位 |
| `log` | 腳本印出的其他每一行（非 `[GS:...]` 標記）都會出現一次 | `step`、`stream`（`stdout`\|`stderr`）、`line` |
| `result` | 僅限 `check`，一次 | `command`、`report`（解析後的就緒狀態檢查 JSON）；若無法解析則改用 `raw` |
| `step_end` | 每個 pipeline 步驟各一次 | `step`、`exitCode`、`success` |
| `job_end` | **一定只會出現一次，且永遠是最後一行** | `command`、`code`、`success`、`message` |

**`job_end` 就是「這個工作到底發生了什麼事」的答案**：`code` 與該行程自身的結束碼是同一個整
數（0／1／2／3／64，如前所述——所以 `success` 就等同於 `code == 0`），`message` 則是一句人類
可讀的結果摘要（例如 `"Step 'ComfyUI Batch' failed (exit 1)."`、
`"Pipeline completed successfully (scan, classify, link, plan-style, comfyui-batch)."`、
`"ComfyUI is not reachable."`）。若呼叫端只在乎成功或失敗，可以忽略其他所有行，只看最後一行
即可。

`marker`／`log` 事件重用了 `App/src/LogParser.cpp`——與 GUI 記錄檢視畫面解析
`asset_pipeline.py`、`run_comfyui_batch.py`（見第 1 節）所印出的
`[GS:STEP_START/PROGRESS/STEP_DONE/STEP_DETAIL/ERROR ...]` 標記時，用的是**完全同一套**解析
邏輯——因此 `PROGRESS` 標記裡的 `current`／`total` 欄位，就是 GUI 進度條所顯示的那組逐一資產
進度值。`batch_reskin.py`（Simple Batch）不會印出這些標記（缺陷 #4），所以它的所有輸出都只會
以一般的 `log` 事件呈現。

範例——`all` 執行到一半失敗的情況：

```json
{"type":"job_start","command":"all"}
{"type":"step_start","step":"Scan","index":1,"total":5}
{"type":"marker","step":"Scan","marker":"STEP_START","scriptStep":"scan"}
{"type":"marker","step":"Scan","marker":"STEP_DONE","scriptStep":"scan","message":"STEP_DONE step=scan assets=74]"}
{"type":"step_end","step":"Scan","exitCode":0,"success":true}
{"type":"step_start","step":"Classify","index":2,"total":5}
{"type":"step_end","step":"Classify","exitCode":0,"success":true}
{"type":"step_start","step":"Link","index":3,"total":5}
{"type":"step_end","step":"Link","exitCode":0,"success":true}
{"type":"step_start","step":"Plan Style","index":4,"total":5}
{"type":"step_end","step":"Plan Style","exitCode":0,"success":true}
{"type":"step_start","step":"ComfyUI Batch","index":5,"total":5}
{"type":"log","step":"ComfyUI Batch","stream":"stderr","line":"Cannot reach ComfyUI at 127.0.0.1:8188"}
{"type":"step_end","step":"ComfyUI Batch","exitCode":1,"success":false}
{"type":"job_end","command":"all","code":1,"success":false,"message":"Step 'ComfyUI Batch' failed (exit 1)."}
```

呼叫端的應用程式可以在收到 `step_start`／`step_end`／`PROGRESS` 標記時即時更新進度畫面，並在
看到 `job_end` 的當下立刻判斷整個工作是成功還是失敗。

實作備註：`--json` 模式會自行擷取所啟動腳本的 stdout／stderr（而不是直接透傳），以便逐行解
析——詳見 `PipelineEngine::runStepRequestSync` 的 `onEvent` callback 參數，以及
`HeadlessRunner.cpp` 裡的 `JsonReporter` 結構。
