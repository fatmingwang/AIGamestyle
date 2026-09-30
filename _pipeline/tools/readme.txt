Run the pipeline:


_pipeline\tools\run_full_pipeline.bat
It will prompt you for three values (press Enter to accept defaults):


Batch ID      [batch_demo]       ← name for this run
Asset root    [game]      ← folder containing textures/ and spine/
Style profile [default_parameters]
Outputs land in output\<batch_id>\

What the bat does in order

Check Python on PATH
Check ComfyUI reachable at :8188  ← exits here if ComfyUI not running
Milestone 1 — scan
Milestone 2 — classify
Milestone 3 — link
Milestone 4 — plan-style  → writes batches/<batch_id>/style_plan.json
Milestone 5 — run_comfyui_batch.py  → generates images
Each step exits immediately on failure with a clear [FAIL] message — it won't silently continue to Milestone 5 with broken data from earlier steps.