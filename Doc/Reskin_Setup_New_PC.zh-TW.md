> 英文原文：[Reskin_Setup_New_PC.md](Reskin_Setup_New_PC.md)

# 換皮工具——新電腦安裝

一台 Windows 電腦在執行換皮工具前需要準備的一切。換皮工具有兩個：`restyle_game.py` 把整款遊戲換成新美術風格，
`make_sprites.py` 畫新的像素圖。換皮的原理與如何對一款遊戲執行，見 [Reskin_Game_Art.zh-TW.md](Reskin_Game_Art.zh-TW.md)。

2026-10-18 在可正常運作的電腦上核對；以下版本都是確定可用的版本。

## 0. 檢查清單

| # | 項目 | 大小 | 章節 |
|---|---|---|---|
| 1 | 12 GB 顯示記憶體的 NVIDIA 顯示卡（已測：RTX 4070），較新的 NVIDIA 驅動程式 | — | 1 |
| 2 | Git、Python 3.11 | — | 2 |
| 3 | ComfyUI（0.21.1），位於 `AIGamestyle/_comfyui/ComfyUI`，有自己的 `.venv` 與 PyTorch 2.6.0 + CUDA 12.4 | 含 PyTorch 約 6 GB | 3 |
| 4 | 兩個自訂節點：**ComfyUI_IPAdapter_plus** 與 **ComfyUI-Inspyrenet-Rembg** | 很小 | 4 |
| 5 | 六個模型檔（第 5 節的表） | 約 20 GB | 5 |
| 6 | 執行工具的 Python 需有 Pillow 與 numpy | 很小 | 2 |
| 7 | 檢查：`check_reskin_setup.py` 顯示 `ready` | — | 7 |

換皮工具是單純的 Python 腳本，把工作送到 `127.0.0.1:8188` 的 ComfyUI 伺服器；不需要其他安裝。

## 1. 硬體

- **顯示卡：** 支援 CUDA 的 NVIDIA 顯示卡。在 RTX 4070（12 GB）上 1024 × 1024 每張約 12–14 秒。每個工作會同時載入
  SDXL、ControlNet 與 IP-Adapter，所以請準備 **12 GB 顯示記憶體**。8 GB 的顯示卡加上 ComfyUI 的 `--lowvram`
  或許可行（未測試）。用 CPU 跑速度不堪使用。
- **硬碟：** 約 30 GB 可用空間：模型（約 20 GB）、ComfyUI 與 PyTorch（約 6 GB），以及輸出空間。
- **記憶體：** 建議 32 GB（每個 SDXL 模型載入時約 7 GB）。

## 2. 軟體

| 程式 | 可用版本 | 下載 |
|---|---|---|
| Git | 任一較新版本 | https://git-scm.com/download/win |
| Python | **3.11**（此處為 3.11.9）；安裝時勾選「Add to PATH」 | https://www.python.org/downloads/windows/ |
| NVIDIA 驅動程式 | 支援 CUDA 12.4 以上 | https://www.nvidia.com/drivers |

工具還需要執行它們的 Python 有 **Pillow** 與 **numpy**：

```
python -m pip install pillow numpy
```

ComfyUI 的 `.venv`（第 3 節）已經有這兩個套件，所以也可以用
`AIGamestyle\_comfyui\ComfyUI\.venv\Scripts\python.exe` 執行工具。

## 3. ComfyUI

**一鍵方式：** `AIGamestyle\_pipeline\tools\setup_style_pipeline.bat` 會做完這些事：

- 把 ComfyUI clone 到 `AIGamestyle\_comfyui\ComfyUI`；
- 建立它的 `.venv` 並安裝 `requirements.txt`；
- 加入 ComfyUI-Manager；
- 建立模型資料夾。

[_pipeline/STEP_BY_STEP_HANDBOOK.md](../_pipeline/STEP_BY_STEP_HANDBOOK.md) 第 0.5 與第 2 節說明它的用法，以及失敗時
怎麼辦。第一次啟動（`start_comfyui_with_flags.bat`，第 6 節）會自動安裝 CUDA 12.4 版的 PyTorch。

**手動方式**（相同步驟，在 `AIGamestyle\` 下執行）：

```
git clone https://github.com/comfyanonymous/ComfyUI.git _comfyui\ComfyUI
cd _comfyui\ComfyUI
python -m venv .venv
.venv\Scripts\python.exe -m pip install --upgrade pip
.venv\Scripts\python.exe -m pip install torch==2.6.0 torchvision torchaudio --index-url https://download.pytorch.org/whl/cu124
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

可用的電腦上是 ComfyUI **0.21.1**（commit `26515acd`，2026-05-13）。較新的 ComfyUI 應該也可以：工具只用標準節點。
若新版讓某個節點壞掉，用 `git checkout 26515acd` 退回。

確認抓得到顯示卡：

```
.venv\Scripts\python.exe -c "import torch; print(torch.__version__, torch.cuda.is_available(), torch.cuda.get_device_name(0))"
```

應印出 `2.6.0+cu124 True NVIDIA ...`。

## 4. 自訂節點

有兩個節點不是 ComfyUI 內建的：

| 節點 | 用途 | 原始碼庫 | 此處版本 |
|---|---|---|---|
| **ComfyUI_IPAdapter_plus**（`IPAdapterModelLoader`、`IPAdapterAdvanced`） | 讓結果跟隨參考圖的外觀：Spine 部件跟隨物件已換皮的符號，像素圖跟隨遊戲原有的圖 | https://github.com/cubiq/ComfyUI_IPAdapter_plus | 2.0.0 |
| **ComfyUI-Inspyrenet-Rembg**（`InspyrenetRembg`） | 去除新像素圖的背景。只有 `make_sprites.py` 用到；換皮不用 | https://github.com/john-mnz/ComfyUI-Inspyrenet-Rembg | 1.1.1 |

可用 ComfyUI-Manager 安裝（以 `--enable-manager` 啟動 ComfyUI，然後 Manager → Custom Nodes Manager，搜尋名稱，
Install），或手動安裝：

```
cd AIGamestyle\_comfyui\ComfyUI\custom_nodes
git clone https://github.com/cubiq/ComfyUI_IPAdapter_plus.git comfyui_ipadapter_plus
git clone https://github.com/john-mnz/ComfyUI-Inspyrenet-Rembg.git comfyui-inspyrenet-rembg
..\.venv\Scripts\python.exe -m pip install -r comfyui-inspyrenet-rembg\requirements.txt
```

Inspyrenet 的 requirements 會安裝 `transparent-background`（此處 1.3.4）。它第一次使用時會下載模型 `ckpt_base.pth`
（367 MB）到 `%USERPROFILE%\.transparent-background\`。沒有網路的電腦需要把那個檔案複製過去。

工具用到的其他節點都是 ComfyUI 標準節點：`CheckpointLoaderSimple`、`LoraLoader`、`Canny`、`ControlNetLoader`、
`SetUnionControlNetType`、`ControlNetApplyAdvanced`、`CLIPVisionLoader`、`KSampler`，以及圖片的讀取與儲存節點。

`custom_nodes` 中的其他自訂節點（comfyui_controlnet_aux、comfyui-florence2、comfyui-rmbg、comfyui-tensorops、
ComfyUI-Fast-Style-Transfer）屬於較早的實驗，換皮**不需要**。

## 5. 模型

工具指定的是**精確的檔名**。下載後請改成第一欄的名稱。

| 工具使用的檔名 | ComfyUI 資料夾 | 大小（位元組） | 下載（Hugging Face） | 用途 |
|---|---|---|---|---|
| `Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors` | `checkpoints` | 7 105 348 188 | `RunDiffusion/Juggernaut-XL-v9`，同檔名 | 換皮 |
| `sd_xl_base_1.0.safetensors` | `checkpoints` | 6 938 078 334 | `stabilityai/stable-diffusion-xl-base-1.0`，同檔名 | 像素圖 |
| `controlnet-union-sdxl-promax.safetensors` | `controlnet` | 2 513 342 408 | `xinsir/controlnet-union-sdxl-1.0`，檔案 `diffusion_pytorch_model_promax.safetensors`（**需改名**） | 換皮、衍生像素圖 |
| `ip-adapter-plus_sdxl_vit-h.safetensors` | `ipadapter` | 847 517 512 | `h94/IP-Adapter`，檔案 `sdxl_models/ip-adapter-plus_sdxl_vit-h.safetensors` | 換皮、像素圖 |
| `CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors` | `clip_vision` | 2 528 373 448 | `h94/IP-Adapter`，檔案 `models/image_encoder/model.safetensors`（**需改名**） | 換皮、像素圖 |
| `pixel-art-xl.safetensors` | `loras` | 170 543 052 | `nerijs/pixel-art-xl`，同檔名 | 只有像素圖 |

下載連結是 `https://huggingface.co/<原始碼庫>/resolve/main/<檔案>`，例如
`https://huggingface.co/h94/IP-Adapter/resolve/main/sdxl_models/ip-adapter-plus_sdxl_vit-h.safetensors`。
下載後請比對大小：下載不完整的檔案要到執行工作時才會失敗。只做遊戲換皮需要其中四個，約 13 GB：Juggernaut、
ControlNet、IP-Adapter 與 CLIP-ViT-H。SDXL base 與像素 LoRA 只用於新像素圖。

檔名寫在工具開頭：`_pipeline/tools/restyle_pilot.py` 的 `CHECKPOINT` 與 `CONTROLNET`（IP-Adapter 與 CLIP 的檔名在
它的 `workflow()` 中），以及 `_pipeline/tools/make_sprites.py` 的 `CHECKPOINT` 與 `LORA`。要用其他檔案就改那裡。

**模型放在哪裡。** 二選一：

- **放在 ComfyUI 自己的資料夾：** `AIGamestyle\_comfyui\ComfyUI\models\<資料夾>\`；或
- **放在共用資料夾**（可用的電腦就是這樣：`D:\Work\AI\models\<資料夾>\`），並寫在
  `AIGamestyle\_comfyui\ComfyUI\extra_model_paths.yaml`：

  ```yaml
  qoo_models:
    base_path: D:/Work/AI/models        # 改成這台電腦的資料夾
    checkpoints: checkpoints
    controlnet: controlnet
    ipadapter: ipadapter
    clip_vision: clip_vision
    loras: loras
  ```

  原始碼庫中的這個檔案列出所有資料夾類型，並在註解中保留了建置機的路徑（`C:/Users/client/Desktop/AI/models`）。
  把 `base_path` 改成這台電腦的資料夾。ComfyUI 啟動時讀取這個檔案。

## 6. 啟動 ComfyUI

工具執行期間請讓它保持執行：

```
AIGamestyle\_pipeline\tools\start_comfyui_with_flags.bat
```

需要時它會安裝或修復 CUDA 版 PyTorch，然後在 http://127.0.0.1:8188 提供服務。在「Extra flags」直接按 Enter。
[_pipeline/STEP_BY_STEP_HANDBOOK.md](../_pipeline/STEP_BY_STEP_HANDBOOK.md) 第 6 節說明 CPU 備援與手動啟動。
手動啟動是：

```
cd AIGamestyle\_comfyui\ComfyUI
.venv\Scripts\python.exe main.py --listen 127.0.0.1 --port 8188
```

## 7. 檢查

在 ComfyUI 執行中時：

```
cd AIGamestyle\_pipeline\tools
python check_reskin_setup.py
```

它把工具用到的每個節點與模型檔列為 `OK` 或 `MISSING`，最後顯示 `ready`。接著試一張圖，這是一個約 15 秒的真實工作：

```
python restyle_pilot.py <任一張遊戲圖.png> ..\..\output\pilot --prompt "christmas theme, snow, red and gold" --denoise 0.6
```

結果是 `output\pilot\<名稱>_d60.png`，大小與 alpha 都和輸入相同。

## 8. 已有的其他文件與位置

| 文件 | 內容 |
|---|---|
| [readme.txt](../readme.txt) | 四個安裝步驟的簡短版 |
| [_pipeline/STEP_BY_STEP_HANDBOOK.md](../_pipeline/STEP_BY_STEP_HANDBOOK.md) | venv、一鍵安裝、就緒檢查（`check_style_pipeline.py`）、啟動 ComfyUI、CPU 模式、常見修正。內容屬於較早的掃描／分類／批次流程，但安裝步驟相同 |
| [_pipeline/README.md](../_pipeline/README.md) | 較早的 scan → classify → link → plan-style 流程（換皮工具不需要） |
| [Doc/reskin_parameters.md](reskin_parameters.md) | 較早的 App／批次換皮（`TestData/Reskin`）的參數，不是 `restyle_game.py` 的 |
| [Doc/Reskin_Game_Art.zh-TW.md](Reskin_Game_Art.zh-TW.md) | **換皮的原理與如何對一款遊戲執行** |

## 9. 常見問題

| 現象 | 原因與修正 |
|---|---|
| `check_reskin_setup.py`：連不到 ComfyUI | ComfyUI 沒有啟動，或用了別的連接埠。啟動它（第 6 節）。若 8188 已被占用，表示舊的 ComfyUI 還在執行；用那個，或關掉它 |
| 工作失敗並出現指名某模型的 `value_not_in_list` | 檔案不存在或檔名不同。核對第 5 節的檔名與 `extra_model_paths.yaml` 的 `base_path`，然後重新啟動 ComfyUI |
| 缺少 `IPAdapterAdvanced`／`InspyrenetRembg` | 自訂節點沒有安裝，或匯入失敗。在 ComfyUI 啟動記錄中找 `IMPORT FAILED` |
| `torch.cuda.is_available()` 為 False | 裝到 CPU 版 PyTorch。執行 `start_comfyui_with_flags.bat`（會重裝 CUDA 版），或第 3 節的 pip 指令 |
| 顯示記憶體不足 | 關掉其他用顯示卡的程式。以 `--lowvram` 啟動 ComfyUI |
| 第一個像素圖工作卡住好幾分鐘 | Inspyrenet 正在下載 `ckpt_base.pth`（第 4 節） |
