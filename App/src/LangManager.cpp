#include "LangManager.h"

LangManager& LangManager::instance()
{
    static LangManager inst;
    return inst;
}

LangManager::LangManager(QObject* parent)
    : QObject(parent)
{
    initTables();
}

void LangManager::setLang(AppLang l)
{
    if (lang_ == l) return;
    lang_ = l;
    emit langChanged();
}

QString LangManager::get(const char* key) const
{
    return get(QString::fromUtf8(key));
}

QString LangManager::get(const QString& key) const
{
    if (lang_ == AppLang::ZH_TW) {
        auto it = zh_.find(key);
        if (it != zh_.end())
            return it.value();
    }
    return key;
}

void LangManager::initTables()
{
    // ── App ──────────────────────────────────────────────────────────────────
    zh_[QStringLiteral("GameStyleAI — Batch Reskin Tool")] =
        QStringLiteral("GameStyleAI — 批量換膚工具");

    // ── Tab names ────────────────────────────────────────────────────────────
    zh_[QStringLiteral("Pipeline")]               = QStringLiteral("流程");
    zh_[QStringLiteral("Symbol Groups")]           = QStringLiteral("符號群組");
    zh_[QStringLiteral("Group Builder")]           = QStringLiteral("群組建立");
    zh_[QStringLiteral("Online Resource Replace")] = QStringLiteral("線上遊戲資源替換");
    zh_[QStringLiteral("Atlas Tools")]             = QStringLiteral("Atlas 工具");

    // ── Toolbar ──────────────────────────────────────────────────────────────
    zh_[QStringLiteral("Language:")]  = QStringLiteral("語言：");

    // ── Configuration panel ──────────────────────────────────────────────────
    zh_[QStringLiteral("Configuration")]          = QStringLiteral("設定");
    zh_[QStringLiteral("Project Root:")]          = QStringLiteral("專案根目錄：");
    zh_[QStringLiteral("Working Directory:")]     = QStringLiteral("工作目錄：");
    zh_[QStringLiteral("Set")]                    = QStringLiteral("設定");
    zh_[QStringLiteral("Output Folder:")]         = QStringLiteral("輸出資料夾：");
    zh_[QStringLiteral("Asset Root:")]            = QStringLiteral("資產根目錄：");
    zh_[QStringLiteral("Hero Reference:")]        = QStringLiteral("風格參考圖：");
    zh_[QStringLiteral("ComfyUI URL:")]           = QStringLiteral("ComfyUI 位址：");
    zh_[QStringLiteral("Style Profile:")]         = QStringLiteral("樣式設定檔：");
    zh_[QStringLiteral("Batch ID:")]              = QStringLiteral("批次 ID：");
    zh_[QStringLiteral("Mode:")]                  = QStringLiteral("模式：");
    zh_[QStringLiteral("Category:")]              = QStringLiteral("類別：");
    zh_[QStringLiteral("Include Spine Textures")] = QStringLiteral("包含 Spine 貼圖");
    zh_[QStringLiteral("IPAdapter Wt:")]          = QStringLiteral("IP 強度：");
    zh_[QStringLiteral("Denoise:")]               = QStringLiteral("去雜強度：");
    zh_[QStringLiteral("Steps:")]                 = QStringLiteral("步驟數：");
    zh_[QStringLiteral("Positive Prompt:")]       = QStringLiteral("正向提示詞：");
    zh_[QStringLiteral("Negative Prompt:")]       = QStringLiteral("\xe8\xb2\xa0\xe5\x90\x91\xe6\x8f\x50\xe7\xa4\xba\xe8\xa9\x9e\xef\xbc\x9a");
    zh_[QStringLiteral("Browse...")]              = QStringLiteral("瀏覽...");
    zh_[QStringLiteral("Launch ▶")]    = QStringLiteral("啟動 ▶");
    zh_[QStringLiteral("Full Pipeline")]          = QStringLiteral("完整流程");
    zh_[QStringLiteral("Simple Batch")]           = QStringLiteral("簡單批次");

    // ── Pipeline panel / Log ─────────────────────────────────────────────────
    zh_[QStringLiteral("Pipeline Steps")] = QStringLiteral("流程步驟");
    zh_[QStringLiteral("Step Detail")]    = QStringLiteral("步驟詳情");
    zh_[QStringLiteral("Log Output")]     = QStringLiteral("日誌輸出");
    zh_[QStringLiteral("Run All")]        = QStringLiteral("全部執行");
    zh_[QStringLiteral("Run Step")]       = QStringLiteral("執行步驟");
    zh_[QStringLiteral("Stop")]           = QStringLiteral("停止");
    zh_[QStringLiteral("Clear")]          = QStringLiteral("清除");

    // ── SymbolGroupWidget ────────────────────────────────────────────────────
    zh_[QStringLiteral("Import JSON...")]        = QStringLiteral("匯入 JSON...");
    zh_[QStringLiteral("Export JSON...")]        = QStringLiteral("匯出 JSON...");
    zh_[QStringLiteral("File:")]                 = QStringLiteral("檔案：");
    zh_[QStringLiteral("(no file loaded)")]      = QStringLiteral("（未載入檔案）");
    zh_[QStringLiteral("(from Group Builder)")]  = QStringLiteral("（來自群組建立）");
    zh_[QStringLiteral("Groups")]                = QStringLiteral("群組");
    zh_[QStringLiteral("☑ All")]      = QStringLiteral("☑ 全選");
    zh_[QStringLiteral("☐ None")]     = QStringLiteral("☐ 全不選");
    zh_[QStringLiteral("Group Settings")]        = QStringLiteral("群組設定");
    zh_[QStringLiteral("Reference:")]            = QStringLiteral("參考圖：");
    zh_[QStringLiteral("IP Weight:")]            = QStringLiteral("IP 強度：");
    zh_[QStringLiteral("Apply → Pipeline")]  = QStringLiteral("套用至流程 →");
    zh_[QStringLiteral("Run Checked ▶")]     = QStringLiteral("執行已選取 ▶");
    zh_[QStringLiteral("Items")]                 = QStringLiteral("項目");
    zh_[QStringLiteral("Item Detail")]           = QStringLiteral("項目詳情");
    zh_[QStringLiteral("(select an item)")]      = QStringLiteral("（請選擇項目）");
    zh_[QStringLiteral("Path to style reference image")] =
        QStringLiteral("風格參考圖路徑");
    zh_[QStringLiteral("Custom positive prompt for this symbol...")] =
        QStringLiteral("\xe6\xad\xa4\xe7\xac\xa6\xe8\x99\x9f\xe7\x9a\x84\xe6\xad\xa3\xe5\x90\x91\xe6\x8f\x50\xe7\xa4\xba\xe8\xa9\x9e...");
    zh_[QStringLiteral("Optional negative prompt...")] =
        QStringLiteral("\xe5\x8f\xaf\xe9\x81\xb8\xe7\x9a\x84\xe8\xb2\xa0\xe5\x90\x91\xe6\x8f\x50\xe7\xa4\xba\xe8\xa9\x9e...");
    zh_[QStringLiteral("(use path from style profile JSON)")] =
        QStringLiteral("（使用樣式設定檔 JSON 中的路徑）");

    // ── GroupBuilderWidget ───────────────────────────────────────────────────
    zh_[QStringLiteral("Style Reference:")]       = QStringLiteral("風格參考圖：");
    zh_[QStringLiteral("Select a folder containing symbol images")] =
        QStringLiteral("選擇包含符號圖片的資料夾");
    zh_[QStringLiteral("Reference image — defines the target art style (optional)")] =
        QStringLiteral("參考圖——定義目標美術風格（可選）");
    zh_[QStringLiteral("Vision Backend:")]        = QStringLiteral("視覺後端：");
    zh_[QStringLiteral("URL:")]                   = QStringLiteral("位址：");
    zh_[QStringLiteral("Model:")]                 = QStringLiteral("模型：");
    zh_[QStringLiteral("Auto-Group by Vision ▶")] =
        QStringLiteral("視覺自動分組 ▶");
    zh_[QStringLiteral("Ollama (local, free)")]   = QStringLiteral("Ollama（本地，免費）");
    zh_[QStringLiteral("New Group from Selected ▶")] =
        QStringLiteral("從選取項目建立群組 ▶");
    zh_[QStringLiteral("Auto-Group by Prefix ▶")] =
        QStringLiteral("依前綴自動分組 ▶");
    zh_[QStringLiteral("Group Name:")]            = QStringLiteral("群組名稱：");
    zh_[QStringLiteral("Delete Group")]           = QStringLiteral("刪除群組");
    zh_[QStringLiteral("→ Open in Symbol Groups")] =
        QStringLiteral("→ 在符號群組中開啟");
    zh_[QStringLiteral("Scan ▶")]      = QStringLiteral("掃描 ▶");

    // ── ResourceReplaceWidget ────────────────────────────────────────────────
    zh_[QStringLiteral("Online Resource Replace")] = QStringLiteral("線上遊戲資源替換");
    zh_[QStringLiteral("Upload to NAS")]           = QStringLiteral("上傳至 NAS");
    zh_[QStringLiteral("NAS Target Folder:")]      = QStringLiteral("NAS 目標資料夾：");
    zh_[QStringLiteral("Upload Output to NAS")]    = QStringLiteral("上傳 Output 至 NAS");
    zh_[QStringLiteral("Server Settings")]         = QStringLiteral("伺服器設定");
    zh_[QStringLiteral("Server Address:")]         = QStringLiteral("伺服器位址：");
    zh_[QStringLiteral("Server Help")]             = QStringLiteral("說明");
    zh_[QStringLiteral("Info")]                    = QStringLiteral("說明");
    zh_[QStringLiteral("Close")]                   = QStringLiteral("關閉");
    zh_[QStringLiteral("Replace Settings")]        = QStringLiteral("替換設定");
    zh_[QStringLiteral("NAS root:")]               = QStringLiteral("NAS 根目錄：");
    zh_[QStringLiteral("Target folder:")]          = QStringLiteral("目標資料夾：");
    zh_[QStringLiteral("Game ID:")]                = QStringLiteral("遊戲 ID：");
    zh_[QStringLiteral("Platform:")]               = QStringLiteral("平台名稱：");
    zh_[QStringLiteral("Operator:")]               = QStringLiteral("操作者：");
    zh_[QStringLiteral("Operator Name:")]          = QStringLiteral("操作者名稱：");
    zh_[QStringLiteral("Deploy Env:")]             = QStringLiteral("部署環境：");
    zh_[QStringLiteral("Deploy Environment:")]     = QStringLiteral("部署環境：");
    zh_[QStringLiteral("Dev (development)")]       = QStringLiteral("Dev（開發環境）");
    zh_[QStringLiteral("Dev (Development)")]       = QStringLiteral("Dev（開發環境）");
    zh_[QStringLiteral("QA (test)")]               = QStringLiteral("QA（測試環境）");
    zh_[QStringLiteral("QA (Testing)")]            = QStringLiteral("QA（測試環境）");
    zh_[QStringLiteral("Start Replace")]           = QStringLiteral("開始替換");
    zh_[QStringLiteral("Replacing...")]            = QStringLiteral("替換中...");
    zh_[QStringLiteral("Uploading...")]            = QStringLiteral("上傳中...");
    // Validation/error messages
    zh_[QStringLiteral("Validation Error")]        = QStringLiteral("驗證錯誤");
    zh_[QStringLiteral("Error")]                   = QStringLiteral("錯誤");
    zh_[QStringLiteral("Warning")]                 = QStringLiteral("警告");
    zh_[QStringLiteral("Confirm Commit")]          = QStringLiteral("確認提交");
    zh_[QStringLiteral("Target Folder Not Empty")] = QStringLiteral("目標資料夾不為空");
    zh_[QStringLiteral("Please enter NAS root")]   = QStringLiteral("請輸入 NAS 根目錄");
    zh_[QStringLiteral("Please enter target folder")] = QStringLiteral("請輸入目標資料夾");
    zh_[QStringLiteral("Please enter Game ID")]    = QStringLiteral("請輸入遊戲 ID");
    zh_[QStringLiteral("Please enter operator name")] = QStringLiteral("請輸入操作者名稱");
    zh_[QStringLiteral("Please select at least one deployment environment")] =
        QStringLiteral("請至少選擇一個部署環境");
    zh_[QStringLiteral("Please enter server address")] = QStringLiteral("請輸入伺服器位址");
    zh_[QStringLiteral("Upload in progress, please wait...")] = QStringLiteral("上傳中，請稍候...");
    zh_[QStringLiteral("Output Folder is not set. Please configure it in the Pipeline tab first.")] =
        QStringLiteral("Output Folder 未設定，請先在 Pipeline 頁面設定");
    zh_[QStringLiteral("Please enter NAS target folder.")] = QStringLiteral("請輸入 NAS 目標資料夾");
    zh_[QStringLiteral("Upload complete! Files copied to NAS.")] = QStringLiteral("上傳完成！檔案已複製至 NAS。");
    zh_[QStringLiteral("Have you tested and confirmed you want to Commit & Push?\n\nThis will commit file updates to the Git repository.")] =
        QStringLiteral("您已經測試並確認要 Commit & Push 嗎？\n\n這將提交檔案更新至 Git 倉庫。");

    // ── AtlasWidget ──────────────────────────────────────────────────────────
    zh_[QStringLiteral("Atlas Tools")]       = QStringLiteral("Atlas 工具");
    zh_[QStringLiteral("Atlas File")]        = QStringLiteral("Atlas 檔案");
    zh_[QStringLiteral("Atlas Files")]       = QStringLiteral("Atlas 檔案");
    zh_[QStringLiteral("Atlas File:")]       = QStringLiteral("Atlas 檔案：");
    zh_[QStringLiteral("Scale:")]            = QStringLiteral("縮放比例：");
    zh_[QStringLiteral("Extract")]           = QStringLiteral("提取圖片");
    zh_[QStringLiteral("Extract Sprites")]   = QStringLiteral("提取圖片 (Extract)");
    zh_[QStringLiteral("Merge Back")]        = QStringLiteral("還原合併");
    zh_[QStringLiteral("Clear Log")]         = QStringLiteral("清除日誌");
    zh_[QStringLiteral("Select a .atlas file")] = QStringLiteral("選擇 .atlas 檔案");
    zh_[QStringLiteral("Select Atlas File")]    = QStringLiteral("選擇 Atlas 檔案");
    zh_[QStringLiteral("Extract Complete")]     = QStringLiteral("提取完成");
    zh_[QStringLiteral("Done")]                 = QStringLiteral("完成");
    zh_[QStringLiteral("Merge Back complete!")]  = QStringLiteral("還原合併完成！");
    zh_[QStringLiteral("File Already Exists")]   = QStringLiteral("檔案已存在");
    zh_[QStringLiteral("Multi-page Atlas")]      = QStringLiteral("多頁 Atlas");
    // Atlas description label
    zh_[QStringLiteral("Extract: unpack atlas texture to atlas/ sub-folder (preserves orig size and alpha)\n"
                        "Merge Back: read images from atlas/ sub-folder and composite into atlas texture")] =
        QStringLiteral("提取：將 Atlas 紋理拆解至 atlas/ 子資料夾（保留 orig 尺寸與透明度）\n"
                        "還原合併：讀取 atlas/ 子資料夾的圖片，重新合成為 Atlas 紋理");

    // ── Help button (all tabs) ───────────────────────────────────────────────
    zh_[QStringLiteral("?  Help")]  = QStringLiteral("？  說明");

    // ── Help dialog titles ───────────────────────────────────────────────────
    zh_[QStringLiteral("Pipeline -- Help")]               = QStringLiteral("流程 -- 說明");
    zh_[QStringLiteral("Symbol Groups -- Help")]          = QStringLiteral("符號群組 -- 說明");
    zh_[QStringLiteral("Group Builder -- Help")]          = QStringLiteral("群組建立 -- 說明");
    zh_[QStringLiteral("Online Resource Replace -- Help")] = QStringLiteral("線上遊戲資源替換 -- 說明");
    zh_[QStringLiteral("Atlas Tools -- Help")]            = QStringLiteral("Atlas 工具 -- 說明");

    // ── Help dialog texts ────────────────────────────────────────────────────
    zh_[QStringLiteral("help.pipeline.text")] = QStringLiteral(
        "對遊戲資源圖片執行 AI 換膚流程。\n\n"
        "快速開始\n"
        "  1. 設定「專案根目錄」為 gamestyleai 資料夾。\n"
        "  2. 設定「來源資料夾」為要換膚的圖片。\n"
        "  3. 設定「輸出資料夾」存放結果。\n"
        "  4. 設定「資產根目錄」（如：game）。\n"
        "  5. 選擇「風格參考圖」（定義目標美術風格）。\n"
        "  6. 確認 ComfyUI 正在執行——點擊「啟動」按鈕。\n"
        "  7. 點擊「全部執行」。");

    zh_[QStringLiteral("help.pipeline.details")] = QStringLiteral(
        "欄位說明\n"
        "  專案根目錄  — 定位流程腳本的根目錄。\n"
        "  來源資料夾 — 要處理的原始圖片。\n"
        "  輸出資料夾 — 換膚結果的儂存位置。\n"
        "  資產根目錄 — 流程使用的子資料夾鍵值。\n"
        "  風格參考圖 — IPAdapter 使用的風格參考圖。\n"
        "  ComfyUI 位址 — 伺服器位址（預設：127.0.0.1:8188）。\n"
        "  樣式設定檔 — style_plan.json 設定檔 ID（空白=預設）。\n"
        "  批次 ID    — 唯一執行名稱；_pipeline/batches/ 下的鍵值。\n"
        "  模式       — 完整流程（5 步驟）或簡單批次。\n"
        "  類別       — 從 reskin_categories.json 載入預設提示詞。\n"
        "  包含 Spine — 同時處理 Spine atlas PNG 貼圖。\n"
        "  IP 強度    — 風格強度 0.0-1.0（越高越接近參考圖）。\n"
        "  去雜強度   — 踊化程度（越高越有創意）。\n"
        "  步驟數     — 擴散步驟數（通常 20-30）。\n\n"
        "流程步驟（完整流程模式）\n"
        "  掃描 → 分類 → 連結 → 規劃風格 → ComfyUI 批次\n\n"
        "需求\n"
        "  - Python 3.9+ 在 PATH 中\n"
        "  - ComfyUI 正在執行並載入 IPAdapter 工作流程\n"
        "  - pip install requests Pillow\n\n"
        "常見問題\n"
        "  「 ComfyUI 無法連線」   — 點擊「啟動」或手動啟動 ComfyUI。\n"
        "  「找不到 style_plan」  — 使用「全部執行」從頭建立。\n"
        "  「找不到 Python」      — 確認 python.exe 在系統 PATH 中。\n"
        "  「Style plan 已過時」  — 點擊「重新產生」重建。\n"
        "  輸出資料夾不為空      — 選擇「取代」或「新資料夾（時間戳）」。");

    zh_[QStringLiteral("help.symgroup.text")] = QStringLiteral(
        "管理遊戲符號群組。每個群組有自己的風格參考圖和設定，每個符號有獨立的 AI 提示詞。\n\n"
        "快速開始\n"
        "  1. 點擊「匯入 JSON」載入符號群組設定檔。\n"
        "     （可在「群組建立」頁面自動生成。）\n"
        "  2. 在左側面板勾選/取消群組。\n"
        "  3. 選擇群組以調整「參考圖」、「IP 強度」、「去雜強度」。\n"
        "  4. \xe9\x81\xb8\xe6\x93\x87\xe9\xa0\x85\xe7\x9b\xae\xe4\xbb\xa5\xe6\x9f\xa5\xe7\x9c\x8b\xe6\x88\x96\xe7\xb7\xa8\xe8\xbc\xaf\xe6\xaf\x8f\xe5\x80\x8b\xe7\xac\xa6\xe8\x99\x9f\xe7\x9a\x84\xe6\x8f\x50\xe7\xa4\xba\xe8\xa9\x9e\xe3\x80\x82\n"
        "  5a. 「套用至流程 →」— 填入流程頁面供手動控制。\n"
        "  5b. 「執行已選取 ▶」— 一鍵執行所有勾選的群組。");

    zh_[QStringLiteral("help.symgroup.details")] = QStringLiteral(
        "面板說明\n"
        "  群組（左側）  — 勾選的群組會一起執行。\n"
        "  群組設定      — 每個群組的參考圖、IP 強度、去雜強度。\n"
        "  項目          — 選定群組所包含的圖片檔案。\n"
        "  項目詳情      — \xe6\xaf\x8f\xe5\x80\x8b\xe7\xac\xa6\xe8\x99\x9f\xe7\x9a\x84\xe6\xad\xa3\xe5\x90\x91\xe8\x88\x87\xe8\xb2\xa0\xe5\x90\x91\xe6\x8f\x50\xe7\xa4\xba\xe8\xa9\x9e\xe3\x80\x82\n\n"
        "按鈕說明\n"
        "  ☑ 全選 / ☐ 全不選  — 選取或取消所有群組。\n"
        "  套用至流程 →        — 複製群組設定到流程頁面。\n"
        "  執行已選取 ▶ (N)    — 透過 ComfyUI 執行所有 N 個已選群組。\n"
        "                        所有輸出存入同一個時間戳資料夾。\n\n"
        "常見問題\n"
        "  沒有群組顯示     — 匯入 JSON 檔案，或在「群組建立」頁面建立。\n"
        "  執行按鈕停用     — 沒有勾選的群組有項目。\n"
        "  ComfyUI 錯誤     — 確認 ComfyUI 正在執行（流程 → 啟動）。");

    zh_[QStringLiteral("help.groupbuilder.text")] = QStringLiteral(
        "掃描圖片資料夾，透過手動或 AI 建立群組，並生成「符號群組」頁面所需的 JSON 設定檔。\n\n"
        "快速開始（AI 視覺分組）\n"
        "  1. 設定「來源資料夾」並點擊「掃描」。\n"
        "  2. 設定「風格參考圖」為目標美術風格的示例圖。\n"
        "  3. 選擇後端「Ollama（本地，免費）」。\n"
        "  4. 設定伺服器位址（預設：http://localhost:11434）。\n"
        "  5. 點擊「視覺自動分組 ▶」。\n"
        "  6. \xe6\xa4\x9c\xe8\xa6\x96\xe4\xb8\xa6\xe7\xb7\xa8\xe8\xbc\xaf\xe7\xbe\xa4\xe7\xb5\x84\xe5\x92\x8c\xe6\x8f\x50\xe7\xa4\xba\xe8\xa9\x9e\xe3\x80\x82\n"
        "  7. 點擊「→ 在符號群組中開啟」。");

    zh_[QStringLiteral("help.groupbuilder.details")] = QStringLiteral(
        "分組方式\n"
        "  視覺自動分組   — AI 分析每張圖片並分配說义群組類型\n"
        "                  （WILD、High Value、 Card Letters、 Bonus）\n"
        "                  \xe4\xb8\xa6\xe7\x94\x9f\xe6\x88\x90\xe6\xaf\x8f\xe5\x9c\x96\xe6\x8f\x50\xe7\xa4\xba\xe8\xa9\x9e\xe3\x80\x82\n"
        "  依前綴自動分組 — 依檔名前綴字母分組\n"
        "                  （如 H1.png、H2.png → 群組「H」）。\n"
        "  從選取項目建立 — 在左側選取圖片，點擊按鈕手動新建群組。\n\n"
        "視覺後端\n"
        "  Ollama（本地，免費）\n"
        "    安裝：https://ollama.com\n"
        "    下載模型：ollama pull llava\n"
        "    啟動伺服器：ollama serve\n"
        "    Python 套件：pip install ollama\n"
        "    位址格式：http://<host>:11434\n\n"
        "  OpenAI (gpt-4o)\n"
        "    設定環境變數：OPENAI_API_KEY=sk-...\n"
        "    Python 套件：pip install openai\n\n"
        "  Anthropic (Claude)\n"
        "    設定環境變數： ANTHROPIC_API_KEY=sk-ant-...\n"
        "    Python 套件：pip install anthropic\n\n"
        "常見問題\n"
        "  「找不到 ollama 套件」  — pip install ollama\n"
        "  「找不到圖片」          — 資料夾需包含 PNG/JPG/WEBP 檔案。\n"
        "  \xe5\x88\x86\xe6\x9e\x90\xe5\xbe\x8c\xe6\x8f\x50\xe7\xa4\xba\xe8\xa9\x9e\xe7\x82\xba\xe7\xa9\xba        — JSON 解析失敗；請重試或切換後端。\n"
        "  Ollama 無回應          — 執行：ollama serve（並検查位址）。\n"
        "  分組不正確             — 手動編輯群組或重新執行。");

    zh_[QStringLiteral("help.resource.text")] = QStringLiteral(
        "將 AI 換膚後的美術資源上傳至 NAS 並在線上遊戲伺服器中替換。\n\n"
        "操作流程\n"
        "  1. 設定 NAS 目標資料夾，點擊「上傳 Output 至 NAS」。\n"
        "  2. 填寫伺服器位址（如：https://localhost:3443）。\n"
        "  3. 填寫替換設定（NAS root、目標資料夾、遊戲 ID 等）。\n"
        "  4. 選擇部署環境（ Dev 和/或 QA）。\n"
        "  5. 點擊「開始替換」。");

    zh_[QStringLiteral("help.resource.details")] = QStringLiteral(
        "欄位說明\n"
        "  NAS 目標資料夾  — 上傳輸出結果的 NAS 路徑。\n"
        "  伺服器位址      — 管理後台 API 位址（HTTP 或 HTTPS）。\n"
        "  NAS root        — NAS 上的專案根目錄。\n"
        "  目標資料夾      — 遊戲資源在 NAS 上的相對路徑。\n"
        "  遊戲 ID         — 如：10083\n"
        "  平台名稱        — 如： ap\n"
        "  操作者名稱      — 記錄本次操作的執行者。\n"
        "  部署環境        — Dev（開發環境）和/或 QA（測試環境）。\n\n"
        "常見問題\n"
        "  SSL 憑證錯誤    — 已跳過憑證驗證，self-signed 憑證通常可正常使用。\n"
        "  連線被拒絕      — 確認伺服器位址正確且伺服器正在執行。\n"
        "  NAS 上傳失敗      — 確認 NAS 路徑正確且具有寫入權限。\n"
        "  按鈕無法點擊    — 請求正在進行中，請稍候。");

    zh_[QStringLiteral("help.atlas.text")] = QStringLiteral(
        "從 Spine .atlas 貼圖集提取單個精靈圖，編輯後（如 AI 換膚）再合併回去。\n\n"
        "操作流程\n"
        "  提取  →  編輯單個精靈圖  →  還原合併\n\n"
        "步驟 1 — 提取\n"
        "  1. 選擇 Spine .atlas 檔案。\n"
        "  2. 可選：設定縮放比例（ 1.0 = 原始尺寸）。\n"
        "  3. 點擊「提取圖片」。\n"
        "  每個精靈圖儂存為 atlas/ 子資料夾中的獨立 PNG。\n\n"
        "步驟 2 — 編輯精靈圖\n"
        "  使用「流程」或「符號群組」頁面處理提取的 PNG。\n"
        "  保持相同檔名——「還原合併」依檔名匹配。\n\n"
        "步驟 3 — 還原合併\n"
        "  1. 選擇同一個 .atlas 檔案。\n"
        "  2. 點擊「還原合併」。\n"
        "  atlas/ 中的精靈圖合成回貼圖集。");

    zh_[QStringLiteral("help.atlas.details")] = QStringLiteral(
        "縮放比例欄位\n"
        "  1.0  — 原始尺寸；輸出檔案無後綴。\n"
        "  0.5  — 半尺寸；輸出至 atlas_0.5x/ + _0.5x.atlas\n"
        "  2.0  — 雙倍尺寸；輸出至 atlas_2x/ + _2x.atlas\n\n"
        "輸出檔案\n"
        "  提取：    <atlas_dir>/atlas/<精靈名稱>.png（每個精靈獨立一檔）\n"
        "  還原合併：<atlas_dir>/atlas_<縮放>/<貼圖>.png（完整貼圖集）\n"
        "            <atlas_dir>/<atlas名稱>_<縮放>.atlas（縮放後的 .atlas）\n\n"
        "常見問題\n"
        "  「無法開啟 atlas 檔案」 — 確認路徑正確且檔案可讀取。\n"
        "  提取後精靈圖空白        — 該區域在 atlas 中尺寸為 0。\n"
        "  還原合併結果不正確      — 確保提取的 PNG 在合併前未被縮放或重新命名。\n"
        "  合併時缺少精靈圖      — 確認 atlas/ 子資料夾存在且包含所需 PNG。");
}
