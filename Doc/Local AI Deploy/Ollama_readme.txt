 Ollama 本地模型，直接接管 Claude Code / Claude Desktop
 Reference video: https://www.youtube.com/watch?v=6iONJKrFs1k&t=230s

 How it works:
 Claude Desktop/Code --> cc-switch (port 15721) --> Ollama (port 11434) --> local model
 cc-switch intercepts Claude API calls and reroutes them to your local Ollama model.
 The "haiku/sonnet/opus" model names shown in Claude are mapped to your local model by cc-switch.

==============================================================================
STEP 1 — Install Claude Desktop
==============================================================================
 https://claude.com/download


==============================================================================
STEP 2 — Install Ollama and pull a model
==============================================================================
 https://ollama.com/download

 After installing Ollama, pull your chosen model (example: Gemma 4):
   ollama pull gemma4:e4b

 Verify the model downloaded correctly:
   ollama list

Quick test (type a message, then Ctrl+D to exit):
   ollama run gemma4:e4b
But one warning: Gemma 8B may be weak for agent coding. It can work, but for better VS Code agent mode, try a coding model:
   ollama pull qwen2.5-coder:7b
or, if your machine is strong:
   ollama pull qwen2.5-coder:14b

4060Ti
qwen2.5-coder:7b
qwen2.5-coder:14b
deepseek-coder-v2:16b
gemma4:9b

Recommended models by GPU
GPU	Best Models	Experience
4060 Ti 8GB	        Qwen2.5-Coder 7B	OK
4060 Ti 16GB	    Qwen2.5-Coder 14B	Very good
4070 12GB	        Qwen 14B	Very good
4070 SUPER	        Qwen 14B / 32B Q4	Excellent
4090	            Qwen 32B / DeepSeek	Top-tier


 Gemma 4 Hardware Recommendations:
 Model           VRAM          Can Run On                        Notes
 gemma4:e2b      4-6 GB        RTX 3060 6GB / 4050 / 4060       Very light, fast, basic coding
 gemma4:e4b      8-10 GB       RTX 4060 8GB / 3060 12GB         Good lightweight agent
 gemma4:26b      20-24 GB      RTX 4090 / 3090 / dual GPU       Serious coding agent
 gemma4:31b      24-32 GB      RTX 4090 24GB (tight) / A6000    Large context + best quality
 gemma4:*mlx     unified RAM   M1/M2/M3 Mac                     MLX optimized
 gemma4:31b-cloud no GPU       Any PC                           Runs remotely


==============================================================================
STEP 3 — Run Ollama (local-only, same PC as Claude)
==============================================================================
 Ollama starts automatically after install. If you need to start it manually:
   ollama serve

 NOTE: By default Ollama listens on 127.0.0.1:11434 (local only).
 This is correct for cc-switch on the same machine — do NOT change it
 unless you also need other PCs to reach Ollama (see optional section below).

 -- OPTIONAL: Allow access from other PCs on your network --
 Set Ollama to listen on all interfaces (run this before "ollama serve"):
   $env:OLLAMA_HOST = "0.0.0.0:11434"
   ollama serve

 For permanent setup (survives reboots):
   Open Windows "Environment Variables" and add:
     Variable: OLLAMA_HOST
     Value:    0.0.0.0:11434

 WARNING: This exposes Ollama to your entire local network without authentication.
 Only do this if you trust your network.

 -- OPTIONAL: Web UI access from another PC --
 Replace YOUR_LAN_IP below with your machine's actual LAN IP (e.g. 192.168.1.10).
 Find it with: ipconfig | findstr "IPv4"

 Install Docker, start Docker Desktop, then run in PowerShell:
   docker run -d --name open-webui -p 3111:8080 `
     -e OLLAMA_BASE_URL=http://YOUR_LAN_IP:11434 `
     -v open-webui:/app/backend/data `
     --restart always `
     ghcr.io/open-webui/open-webui:main

 Then open in browser: http://YOUR_LAN_IP:3111


==============================================================================
STEP 4 — Install and configure cc-switch
==============================================================================
 Download: https://github.com/farion1231/cc-switch
 Release:  https://github.com/farion1231/cc-switch/releases/tag/v3.15.0
 File:     CC-Switch-v3.15.0-Windows.msi

 NOTE: cc-switch runs on standard Windows 10+. WSL is NOT required.

 In cc-switch, click the Add (+) button and select "Custom Configuration":

   Provider URL:   http://127.0.0.1:11434/v1
   Base format:    OpenAI Chat Completions
   API key:        123456   (any value — must match what you set in Claude Desktop, Step 5)

   Expand Advanced Options:
     Routing format:   OpenAI Chat Completions (require routes)
     Auth field:       ANTHROPIC_API_KEY
     Model Mapping:    gemma4:e4b   (or whichever model you pulled in Step 2)

 Then open cc-switch Settings (gear icon):
   1. Go to the "Routing" section
   2. Enable "Show routing toggle on main page"
   3. Enable "Route master switch"
   4. Enable routing for "Claude"

 After saving, note the Gateway URL shown in the Routing screen — it should be:
   http://127.0.0.1:15721
 You will need this URL in the next step.


==============================================================================
STEP 5 — Configure Claude Desktop for third-party inference
==============================================================================
 Open Claude Desktop:
   Help -> Troubleshooting -> Enable Developer Mode

 In Developer Mode:
   Configure Third Party Inference ->
     Connection -> Gateway Base URL:  http://127.0.0.1:15721
                                      (this is cc-switch's gateway port from Step 4)
     Credential Kind:                 Static API Key
     API Key value:                   123456
                                      (must be the SAME value you entered in cc-switch)

 After applying, you need to add the inferenceModels registry entry so Claude Desktop
 shows model options. Open Registry Editor (regedit) and navigate to:
   HKEY_CURRENT_USER\Software\Anthropic\Claude

 Add a new String Value:
   Name:  inferenceModels
   Value: ["haiku","sonnet","opus"]

 These names (haiku, sonnet, opus) are just labels — cc-switch intercepts all of them
 and routes every request to your local Ollama model (gemma4:e4b).

 Alternatively, create a file named add-inference-models.reg with this content
 and double-click to import it:
   Windows Registry Editor Version 5.00

   [HKEY_CURRENT_USER\Software\Anthropic\Claude]
   "inferenceModels"="[\"haiku\",\"sonnet\",\"opus\"]"


==============================================================================
STEP 6 — Verify the full pipeline
==============================================================================
 1. Ollama is running:         ollama list   (model shows up)
 2. cc-switch is running and routing is enabled (green toggle on main page)
 3. Claude Desktop shows "haiku / sonnet / opus" in model selector
 4. Send a test message in Claude Desktop — response comes from local model

 If it does not work, check:
 - Ollama: http://127.0.0.1:11434  should return a response in browser
 - cc-switch gateway: http://127.0.0.1:15721  should be reachable
 - API key matches exactly in both cc-switch and Claude Desktop
 - "Route master switch" is ON in cc-switch


==============================================================================
STEP 7 — Roll back to official Claude (stop using local model)
==============================================================================
 Option A (quick): Turn off "Route master switch" in cc-switch.
   Claude Desktop will go back to using Anthropic's servers immediately.

 Option B (full removal): Remove the third-party inference settings from Claude Desktop:
   - Delete the inferenceModels registry key from regedit
   - In Claude Desktop Developer Mode, clear the Gateway Base URL field
   - Or edit and remove the relevant keys from:
     C:\Users\<your username>\.claude\settings.json

 Do NOT delete the entire settings.json — that removes all your Claude Code settings.




STEP 8  agent mode
VSCode install Cline
https://marketplace.visualstudio.com/items?itemName=saoudrizwan.claude-dev

in VSCode left side there is a Cline button click it
1.Bring my own APU key
2.Provider select Ollama select qwen2.5-coder:14b
3.Use Cline now you can use agnet mode.

if it's fucking slow
change model to
ollama pull qwen2.5-coder:7b
and context length to 8192
Model: qwen2.5-coder:7b
Context: 8192 or 16384