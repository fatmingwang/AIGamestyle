#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
#include "AppConfig.h"
#include "PipelineCommands.h"

// Shared business logic for the pipeline steps, used by BOTH MainWindow (GUI) and
// HeadlessRunner (the --headless TCP server) so the two modes run byte-for-byte the same
// commands and apply byte-for-byte the same readiness checks. Every function here is pure/UI-free
// and communicates through QJsonObject: the GUI additionally wraps the verdicts in a QMessageBox,
// headless mode additionally wraps them in an args-field decision (e.g. {"overwrite":true}), but
// the underlying logic lives here exactly once.
namespace PipelineEngine {

// AppConfig <-> JSON. Field names match AppConfig's members. Used for --config files and for
// keeping the GUI and CLI's idea of "the current settings" in the same shape.
QJsonObject configToJson(const AppConfig& cfg);
AppConfig   configFromJson(const QJsonObject& obj, AppConfig base = AppConfig());

// Mirrors MainWindow::confirmOutputFolder()'s readiness check (the "is the output folder
// non-empty" gate applied before Scan / ComfyUI Batch / Simple Batch), without any dialog.
// Returns: { applicable, targetFolder, empty, entryCount }
QJsonObject checkOutputFolderState(const AppConfig& cfg);

// Mirrors MainWindow::checkStylePlanFresh()'s readiness check (the "does step4_style_plan.json
// match the current Asset Root" gate applied before ComfyUI Batch), without any dialog.
// Returns: { applicable, planPath, planExists, planRoot, fresh, reason }
QJsonObject checkStylePlanState(const AppConfig& cfg);

// Builds the exact process invocation for one pipeline step: program/args/workingDirectory/env,
// as JSON, so both callers launch the identical command line via PipelineCommands::buildStep().
// `extra` may carry explicit, already-string-formatted overrides — currently "ipWeight"/
// "denoise"/"steps" for StepIndex::ComfyUiBatch — applied only when the key is present, so each
// caller's own opt-in behavior (the GUI currently passes none; see Doc/Commands_Reference.md
// gap #1) is preserved rather than decided here.
// Returns: { step, program, args: [...], workingDirectory, env: {...} }
QJsonObject buildStepRequest(StepIndex idx, const AppConfig& cfg, const QString& projectRoot,
                              const QJsonObject& extra = QJsonObject());

// Splits "host:port" (as stored in AppConfig::comfyuiServer) into host/port, defaulting port
// to "8188" when absent. Shared so the GUI and headless mode parse the same string identically.
void splitHostPort(const QString& hostPort, QString& host, QString& port);

// Probes cfg.comfyuiServer with a raw TCP connect (via a short Python one-liner, since Qt
// Network isn't linked) to check whether a ComfyUI instance is already listening there. Used
// by both the GUI's Launch button and headless's comfyui-status/comfyui-launch commands so a
// fix to the probe only has to be made once.
bool comfyuiReachable(const AppConfig& cfg);

// Builds the exact process invocation to launch ComfyUI for the current platform, mirroring
// buildStepRequest()'s "build the command once, let each caller launch it however it needs"
// pattern: the GUI starts it attached (visible console, live QProcess signals) while headless
// starts it detached and polls comfyuiReachable() afterward, but both use the identical
// program/args/workingDirectory computed here.
// Windows: cmd.exe /c <projectRoot>/_pipeline/tools/start_comfyui_with_flags.bat --enable-manager --listen 0.0.0.0
// Other:   <projectRoot>/_comfyui/ComfyUI/.venv/bin/python main.py --listen 0.0.0.0 --port <port> --enable-manager
// Returns: { ok, error, program, args: [...], workingDirectory } — `error` is only set when
// `ok` is false (the launcher script/venv wasn't found under projectRoot).
QJsonObject buildComfyUiLaunchRequest(const AppConfig& cfg);

// Builds the process invocation for a single Text2Image generation — a prompt straight to
// ComfyUI, with no source asset — mirroring Text2ImageWidget::startRun(). `args` is the
// command's incoming args object; `prompt` is required, everything else has the same default
// the GUI effectively uses. Returns the same {ok, error, program, args, workingDirectory, env}
// shape buildComfyUiLaunchRequest() does, runnable via runStepRequestSync(); `outputPath` (only
// present when `ok`) is where the PNG will land, for the caller to report back.
// args: prompt (required), output-name (default: timestamped), output-dir
//       (default: <projectRoot>/output/Text2Image), width, height (both or neither).
QJsonObject buildText2ImageRequest(const AppConfig& cfg, const QJsonObject& args);

// Builds the process invocation for one Image-With-Prompt run — a reference image + prompt ->
// a prompt-guided variant — mirroring ImageWithPromptWidget::startRun(). seed/steps/cfg/denoise/
// ip-weight fall back to sensible defaults (denoise/steps/ip-weight from cfg's own fields, same
// knobs comfyui-batch already uses) when not given.
// args: reference (required path), output-dir (default: <projectRoot>/output/ImageWithPrompt),
//       output-name (default: reference's base filename), positive-prompt or category
//       (at least one expected — passed through as-is if neither given), negative-prompt,
//       seed, steps, cfg, denoise, ip-weight.
QJsonObject buildImageWithPromptRequest(const AppConfig& cfg, const QJsonObject& args);

// Builds the process invocation for generating pose/skeleton data from a text description —
// mirroring PoseGeneratorWidget's "Generate Pose Data (AI)" action. Uses an LLM backend, not
// ComfyUI (no COMFYUI_SERVER env).
// args: name, description (both required), subject-type (human|biped_animal, default human),
//       loop (bool, default false), frames (default 8), backend, api-url (default: ollama /
//       its default URL), output (default: <projectRoot>/TestData/PoseGenerator/poses_library/<name>.json).
QJsonObject buildPoseFromPromptRequest(const AppConfig& cfg, const QJsonObject& args);

// Runs a request built by buildStepRequest() synchronously and returns the outcome.
// - With no `onEvent` callback: forwards the child's stdout/stderr straight through to this
//   process's own stdout/stderr (QProcess::ForwardedChannels) — the plain-text headless mode.
// - With an `onEvent` callback: captures the child's output line by line instead, runs each
//   line through LogParser::parseLine() (the same marker parser MainWindow's log view uses),
//   and invokes `onEvent` once per line with a JSON event — either a parsed
//   {"type":"marker","marker":"STEP_START"|"PROGRESS"|"STEP_DONE"|"STEP_DETAIL"|"ERROR",...}
//   or, for a line that isn't a "[GS:...]" marker, {"type":"log","stream":"stdout"|"stderr",
//   "line":"..."}. This is how headless mode's --json job-progress protocol is built (see
//   HeadlessRunner.cpp and Doc/Commands_Reference.md §12); nothing here writes to stdout itself.
// Returns: { started, exitCode, success, error }
QJsonObject runStepRequestSync(const QJsonObject& request,
                                 const std::function<void(const QJsonObject&)>& onEvent = nullptr);

} // namespace PipelineEngine
