# 企業 AI 工作站與本地部署規劃指南

## ComfyUI、Ollama、RAG、Agent Platform 導入評估

版本：2026 Enterprise Edition

---

# GPU VRAM 與 AI 能力對照表

| VRAM | ComfyUI         | LLM   | 企業用途    |
| ---- | --------------- | ----- | ----------- |
| 8GB  | SDXL            | 7B~8B | 學習與PoC   |
| 16GB | SDXL+ControlNet | 14B   | 部門應用    |
| 24GB | Flux Dev        | 32B   | 企業知識庫  |
| 32GB | Video AI        | 70B   | 私有GPT平台 |

---

# ComfyUI 使用體驗比較

| 功能項目       | 8GB  | 16GB | 24GB | 32GB |
| -------------- | ---- | ---- | ---- | ---- |
| SD1.5          | ✅   | ✅   | ✅   | ✅   |
| SDXL           | ⚠️ | ✅   | ✅   | ✅   |
| LoRA           | ✅   | ✅   | ✅   | ✅   |
| ControlNet     | 1組  | 多組 | 多組 | 多組 |
| IPAdapter      | ⚠️ | ✅   | ✅   | ✅   |
| AnimateDiff    | ❌   | ✅   | ✅   | ✅   |
| Flux Schnell   | ❌   | ✅   | ✅   | ✅   |
| Flux Dev       | ❌   | ⚠️ | ✅   | ✅   |
| SD3 Medium     | ❌   | ⚠️ | ✅   | ✅   |
| SD3 Large      | ❌   | ❌   | ⚠️ | ✅   |
| Video Workflow | ❌   | ❌   | ⚠️ | ✅   |
| 4K生成         | ❌   | ⚠️ | ✅   | ✅   |

### 評級說明

✅ 流暢使用

⚠️ 可執行但有限制

❌ 不建議

---

# Ollama / Llama 使用體驗比較

| 項目           | 8GB  | 16GB | 24GB | 32GB |
| -------------- | ---- | ---- | ---- | ---- |
| Llama 8B       | ✅   | ✅   | ✅   | ✅   |
| Qwen 14B       | ⚠️ | ✅   | ✅   | ✅   |
| DeepSeek 14B   | ⚠️ | ✅   | ✅   | ✅   |
| Qwen 32B       | ❌   | ⚠️ | ✅   | ✅   |
| DeepSeek 32B   | ❌   | ⚠️ | ✅   | ✅   |
| Llama 70B      | ❌   | ❌   | ⚠️ | ✅   |
| RAG            | ⚠️ | ✅   | ✅   | ✅   |
| MCP Server     | ⚠️ | ✅   | ✅   | ✅   |
| Agent Workflow | ❌   | ⚠️ | ✅   | ✅   |
| 企業知識庫     | ❌   | ⚠️ | ✅   | ✅   |
| 私有GPT        | ❌   | ❌   | ⚠️ | ✅   |

---

# RAM 規劃建議

| RAM    | 適用場景           | 使用人數  |
| ------ | ------------------ | --------- |
| 32GB   | ComfyUI、Ollama 8B | 1~5人     |
| 64GB   | PoC、MCP、RAG      | 5~20人    |
| 128GB  | 企業知識庫         | 20~100人  |
| 192GB  | Agent平台          | 100~300人 |
| 256GB+ | 大型AI Portal      | 300人以上 |

---

# SSD 規劃建議

| SSD   | 適用場景 |
| ----- | -------- |
| 1TB   | 個人學習 |
| 2TB   | 開發環境 |
| 4TB   | PoC驗證  |
| 8TB   | 企業部署 |
| 16TB+ | AI平台   |

# CPU 規劃建議

| 用途     | CPU          |
| -------- | ------------ |
| 入門     | Ryzen 7700   |
| 開發     | Ryzen 9900X  |
| 企業部署 | Ryzen 9950X  |
| 大型平台 | Threadripper |

---

# AI 工作站採購方案

| 方案     | 預算    | GPU        | RAM       | SSD |
| -------- | ------- | ---------- | --------- | --- |
| 入門     | 5~7萬   | RTX4060 8G | 32GB      | 1TB |
| 開發     | 8~12萬  | 5060Ti 16G | 64GB      | 2TB |
| PoC      | 12~18萬 | 3090 24G   | 128GB     | 4TB |
| 正式部署 | 20~25萬 | 5090 32G   | 128~192GB | 8TB |

---

# 雲端 AI vs 本地 AI

| 項目      | ChatGPT Team | Azure OpenAI | 本地 Ollama |
| --------- | ------------ | ------------ | ----------- |
| 導入速度  | 🟢           | 🟢           | 🟡          |
| 模型能力  | 🟢           | 🟢           | 🟡          |
| 資料安全  | 🟡           | 🟢           | 🟢          |
| 客製化    | 🟡           | 🟢           | 🟢          |
| ERP整合   | 🟡           | 🟢           | 🟢          |
| RAG知識庫 | 🟡           | 🟢           | 🟢          |
| Agent平台 | 🟡           | 🟢           | 🟢          |
| 長期成本  | 🔴           | 🔴           | 🟢          |

---

# 三年總持有成本（TCO）

| 規模  | ChatGPT Team | 本地 AI   |
| ----- | ------------ | --------- |
| 50人  | 162萬        | 20萬      |
| 200人 | 648萬        | 35萬      |
| 500人 | 1620萬       | 100~160萬 |

---

# MyWork 建議導入路線圖

| 階段    | 時間       | 目標          | 建議方案                   |
| ------- | ---------- | ------------- | -------------------------- |
| Phase 1 | 1~3個月    | PoC驗證       | ChatGPT Team、Azure OpenAI |
| Phase 2 | 3~6個月    | 內部知識庫    | Ollama、Qwen 32B、RAG      |
| Phase 3 | 6~12個月   | Agent平台     | MCP Server、Workflow Agent |
| Phase 4 | 12個月以上 | 企業AI Portal | RTX5090、私有GPT平台       |

---

# Hybrid AI 最佳實務架構

| 工作類型 | 建議平台       |
| -------- | -------------- |
| 知識查詢 | 本地 Ollama    |
| ERP查詢  | 本地 Agent     |
| 文件搜尋 | 本地 RAG       |
| 客服系統 | 本地 AI        |
| 高階推理 | GPT-5          |
| 商業分析 | GPT-5 / Claude |
| 策略規劃 | GPT-5          |

---

# 採購建議

## 最佳 CP 值

* RTX 3090 24GB
* RAM 128GB
* SSD 4TB

預算：

約 12~15 萬

---

## 長期部署

* RTX 5090 32GB
* RAM 192GB
* SSD 8TB

預算：

約 20~25 萬

---

# 最終結論

企業 AI 專案最常遇到的瓶頸並非 GPU 算力，而是：

1. RAM 不足
2. SSD 容量不足
3. 向量資料庫成長

因此在預算有限情況下：

優先順序建議：

RAM > SSD > GPU

建議配置：

RTX 3090 24GB + 128GB RAM + 4TB SSD

通常能提供最佳的企業 AI PoC 與導入體驗。

# 專案管理相關

| 縮寫 | 全名                      | 中文         |
| ---- | ------------------------- | ------------ |
| PoC  | Proof of Concept          | 概念驗證     |
| MVP  | Minimum Viable Product    | 最小可行產品 |
| ROI  | Return on Investment      | 投資報酬率   |
| TCO  | Total Cost of Ownership   | 總持有成本   |
| KPI  | Key Performance Indicator | 關鍵績效指標 |

# AI 模型相關

| 縮寫      | 全名                           | 中文         |
| --------- | ------------------------------ | ------------ |
| LLM       | Large Language Model           | 大型語言模型 |
| SLM       | Small Language Model           | 小型語言模型 |
| VLM       | Vision Language Model          | 視覺語言模型 |
| RAG       | Retrieval Augmented Generation | 檢索增強生成 |
| Embedding | Embedding Model                | 向量模型     |

---

## LLM

Large Language Model

例如：

* GPT-5
* Claude
* Llama
* Qwen
* DeepSeek

用途：

* 聊天
* 寫程式
* 文件分析
* Agent

---

## RAG

Retrieval-Augmented Generation

檢索增強生成。

工作流程：

<pre class="code-block" data-language="" data-prosemirror-content-type="node" data-prosemirror-node-name="codeBlock" data-prosemirror-node-block="true"><div class="code-block--start" contenteditable="false"></div><div class="code-block-content-wrapper"><div contenteditable="false"><div class="code-block-gutter-pseudo-element" data-label="1
2
3
4
5
6
7
8
9"></div></div><div class="code-content"><code data-language="" spellcheck="false" data-testid="code-block--code" aria-label="" data-local-id="8cab0cc8-bb4e-4ebf-b13c-a2c3391dd8be">使用者提問
    ↓
搜尋知識庫
    ↓
找到相關文件
    ↓
送給 LLM
    ↓
產生答案</code></div></div><div class="code-block--end" contenteditable="false"></div></pre>

例如：

問：

> 公司請假流程是什麼？

AI 不靠訓練資料回答。

而是：

1. 找到 SOP
2. 閱讀 SOP
3. 回答

---

## Embedding

向量化模型。

用途：

把文件轉換成向量。

例如：

<pre class="code-block" data-language="" data-prosemirror-content-type="node" data-prosemirror-node-name="codeBlock" data-prosemirror-node-block="true"><div class="code-block--start" contenteditable="false"></div><div class="code-block-content-wrapper"><div contenteditable="false"><div class="code-block-gutter-pseudo-element" data-label="1
2
3
4"></div></div><div class="code-content"><code data-language="" spellcheck="false" data-testid="code-block--code" aria-label="" data-local-id="21dff808-efc3-454b-bc98-34bb5328714f">PDF
Word
Excel
Wiki</code></div></div><div class="code-block--end" contenteditable="false"></div></pre>

變成：

<pre class="code-block" data-language="" data-prosemirror-content-type="node" data-prosemirror-node-name="codeBlock" data-prosemirror-node-block="true"><div class="code-block--start" contenteditable="false"></div><div class="code-block-content-wrapper"><div contenteditable="false"><div class="code-block-gutter-pseudo-element" data-label="1"></div></div><div class="code-content"><code data-language="" spellcheck="false" data-testid="code-block--code" aria-label="" data-local-id="204d7545-6bc3-4a3f-be0f-dd64cbb26ad9">[0.123, 0.456, 0.789 ...]</code></div></div><div class="code-block--end" contenteditable="false"></div></pre>

供 RAG 搜尋。

常見：

* BGE-M3
* E5
* Nomic Embed
* OpenAI Embedding

---

# AI Agent 相關

| 縮寫  | 全名                   | 中文           |
| ----- | ---------------------- | -------------- |
| Agent | Autonomous Agent       | 智能代理       |
| MCP   | Model Context Protocol | 模型上下文協議 |
| A2A   | Agent to Agent         | 代理互通協議   |

---

## Agent

AI 代理。

不只是回答問題。

還能：

* 查 ERP
* 發 Email
* 建立工單
* 查詢 CRM

例如：

<pre class="code-block" data-language="" data-prosemirror-content-type="node" data-prosemirror-node-name="codeBlock" data-prosemirror-node-block="true"><div class="code-block--start" contenteditable="false"></div><div class="code-block-content-wrapper"><div contenteditable="false"><div class="code-block-gutter-pseudo-element" data-label="1"></div></div><div class="code-content"><code data-language="" spellcheck="false" data-testid="code-block--code" aria-label="" data-local-id="15b0b622-bd44-4215-a64f-489912b24c8b">幫我查詢客戶ABC本月訂單</code></div></div><div class="code-block--end" contenteditable="false"></div></pre>

AI 自動：

1. 查 ERP
2. 找資料
3. 整理結果
4. 回覆

---

## MCP

Model Context Protocol

由 Anthropic 提出。

作用：

讓 AI 可以連接外部系統。

例如：

<pre class="code-block" data-language="" data-prosemirror-content-type="node" data-prosemirror-node-name="codeBlock" data-prosemirror-node-block="true"><div class="code-block--start" contenteditable="false"></div><div class="code-block-content-wrapper"><div contenteditable="false"><div class="code-block-gutter-pseudo-element" data-label="1
2
3
4
5
6"></div></div><div class="code-content"><code data-language="" spellcheck="false" data-testid="code-block--code" aria-label="" data-local-id="c60b2b79-0ad3-4a7f-bfa3-8dd20c51d25e">ERP
CRM
Google Drive
GitHub
Jira
Database</code></div></div><div class="code-block--end" contenteditable="false"></div></pre>

簡單理解：

> MCP = AI 的 API 標準介面

---

# 企業系統相關

| 縮寫 | 全名                             | 中文         |
| ---- | -------------------------------- | ------------ |
| ERP  | Enterprise Resource Planning     | 企業資源規劃 |
| CRM  | Customer Relationship Management | 客戶關係管理 |
| BPM  | Business Process Management      | 流程管理     |
| DMS  | Document Management System       | 文件管理系統 |
| HRM  | Human Resource Management        | 人資管理系統 |

---

## ERP

企業核心系統。

例如：

* 訂單
* 採購
* 庫存
* 財務

MyWork 的許多客戶都會有 ERP。

---

## CRM

客戶管理系統。

例如：

* 客戶資料
* 商機
* 報價
* 聯絡紀錄

---

## BPM

流程管理系統。

例如：

* 請假
* 採購申請
* 簽核流程

---

# 基礎架構相關

| 縮寫 | 全名                     | 中文         |
| ---- | ------------------------ | ------------ |
| GPU  | Graphics Processing Unit | 顯示卡       |
| VRAM | Video RAM                | 顯示卡記憶體 |
| CPU  | Central Processing Unit  | 中央處理器   |
| RAM  | Random Access Memory     | 系統記憶體   |
| SSD  | Solid State Drive        | 固態硬碟     |

---

# AI 平台架構常見縮寫

| 縮寫      | 說明                                   |
| --------- | -------------------------------------- |
| ComfyUI   | AI 繪圖工作流平台                      |
| Ollama    | 本地 LLM 管理平台                      |
| vLLM      | 高效能 LLM 推論引擎                    |
| OpenWebUI | Ollama 的 Web UI                       |
| Vector DB | 向量資料庫                             |
| API       | Application Programming Interface      |
| ETL       | Extract Transform Load（資料匯入流程） |
