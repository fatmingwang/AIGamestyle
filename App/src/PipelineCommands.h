#pragma once
#include <QString>
#include <QStringList>
#include "AppConfig.h"

struct LaunchSpec {
    QString     script;      // absolute path to the Python script
    QStringList args;        // args passed after "python -u <script>"
};

enum class StepIndex : int {
    SimpleBatch  = -1,  // batch_reskin.py <sourceFolder> <outputFolder>
    Scan         =  0,  // asset_pipeline.py scan <assetRoot>
    Classify     =  1,  // asset_pipeline.py classify
    Link         =  2,  // asset_pipeline.py link
    PlanStyle    =  3,  // asset_pipeline.py plan-style --style-profile-id ... --batch-id ...
    ComfyUiBatch =  4,  // run_comfyui_batch.py --batch-id ... --asset-root ...
};

LaunchSpec buildStep(StepIndex index, const AppConfig& cfg, const QString& projectRoot);
QString stepName(StepIndex index);

constexpr int STEP_COUNT = 5;
