#include "PipelineCommands.h"
#include <QDir>

static QString p(const QString& root, const QString& rel)
{
    return QDir::toNativeSeparators(root + '/' + rel);
}

LaunchSpec buildStep(StepIndex index, const AppConfig& cfg, const QString& projectRoot)
{
    LaunchSpec spec;

    switch (index) {
    case StepIndex::Scan:
        spec.script = p(projectRoot, "_pipeline/tools/asset_pipeline.py");
        spec.args   = { "--output-folder", cfg.outputFolder, "scan", cfg.assetRoot };
        break;

    case StepIndex::Classify:
        spec.script = p(projectRoot, "_pipeline/tools/asset_pipeline.py");
        spec.args   = { "--output-folder", cfg.outputFolder, "classify" };
        break;

    case StepIndex::Link:
        spec.script = p(projectRoot, "_pipeline/tools/asset_pipeline.py");
        spec.args   = { "--output-folder", cfg.outputFolder, "link" };
        break;

    case StepIndex::PlanStyle:
        spec.script = p(projectRoot, "_pipeline/tools/asset_pipeline.py");
        spec.args   = { "--output-folder", cfg.outputFolder,
                        "plan-style",
                        "--style-profile-id", cfg.styleProfileId,
                        "--batch-id",         cfg.batchId };
        break;

    case StepIndex::ComfyUiBatch:
        spec.script = p(projectRoot, "_pipeline/tools/run_comfyui_batch.py");
        spec.args   = { "--batch-id",    cfg.batchId,
                        "--asset-root",  cfg.assetRoot };
        if (!cfg.outputFolder.isEmpty())
            spec.args << QStringLiteral("--output-dir") << cfg.outputFolder;
        if (!cfg.heroReference.isEmpty())
            spec.args << QStringLiteral("--hero-reference") << cfg.heroReference;
        if (!cfg.positivePrompt.isEmpty())
            spec.args << QStringLiteral("--positive-prompt") << cfg.positivePrompt;
        if (!cfg.negativePrompt.isEmpty())
            spec.args << QStringLiteral("--negative-prompt") << cfg.negativePrompt;
        if (cfg.includeSpine)
            spec.args << QStringLiteral("--include-spine");
        break;

    case StepIndex::SimpleBatch:
        spec.script = p(projectRoot, "TestData/Reskin/batch_reskin.py");
        spec.args   = { cfg.assetRoot, cfg.outputFolder };
        break;

    default:
        break;
    }

    return spec;
}

QString stepName(StepIndex index)
{
    switch (index) {
    case StepIndex::Scan:         return QStringLiteral("Scan");
    case StepIndex::Classify:     return QStringLiteral("Classify");
    case StepIndex::Link:         return QStringLiteral("Link");
    case StepIndex::PlanStyle:    return QStringLiteral("Plan Style");
    case StepIndex::ComfyUiBatch: return QStringLiteral("ComfyUI Batch");
    case StepIndex::SimpleBatch:  return QStringLiteral("Simple Batch");
    default:                      return QStringLiteral("Unknown");
    }
}
