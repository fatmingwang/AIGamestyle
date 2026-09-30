Reference: https://www.youtube.com/watch?v=mpuaOKSYkqM

========================================
 CodeX Local Deploy Setup Guide
========================================

1. Download CodeX
----------------------------------------
https://openai.com/zh-Hant/codex/


2. Install Ollama
----------------------------------------
https://ollama.com/download

Ensure version is 0.24 or above:
  ollama --version


3. Launch Model via Ollama
----------------------------------------
In cmd:
  ollama launch codex-app

Note: 4070 GPU can use qwen3.6. For other GPUs, ask AI for a suitable model.


4. Browser Plugin Setup
----------------------------------------
Install the browser plugin to let CodeX control your browser:
  Settings -> Browser -> Enabled
  Install the Computer Use plugin (Chrome)

To avoid content-too-long errors, set the context length limit:
  Settings -> Configuration -> Change length to 8012


5. Speed Up with Llama (Local Server)
----------------------------------------
Step 1 - Update config.toml:
  Settings -> Configuration -> Open config.toml
  Clear all content, then paste from: codeXConfiguration.txt

Step 2 - Set the correct model name in config.toml:
  model = "check model name from llama"

Step 3 - Start the Llama server:
  run_llama.bat  (choose option 5 for CodeX)