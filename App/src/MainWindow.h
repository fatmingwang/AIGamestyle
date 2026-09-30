#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QSettings>
#include <QElapsedTimer>
#include <functional>
#include <vector>
#include "AppConfig.h"
#include "PipelineCommands.h"
#include "StepListWidget.h"

class QLineEdit;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QPlainTextEdit;
class QTextEdit;
class QProgressBar;
class QLabel;
class QPushButton;
class QSplitter;
class QTabWidget;
class QGroupBox;
class ResourceReplaceWidget;
class AtlasWidget;
#include "SymbolGroupWidget.h"
#include "GroupBuilderWidget.h"
#include "ImageWithPromptWidget.h"
#include "Text2ImageWidget.h"
#include "PoseGeneratorWidget.h"

enum class AppState { IDLE, RUNNING, ERROR_STATE, ALL_DONE };

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onBrowseProjectRoot();
    void onBrowseOutput();
    void onBrowseAssetRoot();
    void onBrowseHeroRef();
    void onSetWorkingDirectory();
    void onCategoryChanged(const QString& category);
    void onLaunchComfyUI();
    void onApplyGroup(const SymbolGroup& group);
    void onRunGroups(const QList<SymbolGroup>& groups, const QString& scriptMode);
    void onGroupItemFinished(int exitCode, QProcess::ExitStatus status);
    void onElapsedTick();
    void onRunAll();
    void onRunStep();
    void onRestart();
    void onStop();
    void onClearLog();
    void onStdout();
    void onStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void retranslateUi();

private:
    // ── UI helpers ──────────────────────────────────────────────────────────
    void buildUi();
    QWidget* buildConfigPanel();
    QWidget* buildPipelinePanel();
    QWidget* buildLogPanel();

    void syncConfigToUi();
    void syncUiToConfig();

    void appendLog(const QString& text, const QColor& color = Qt::white);
    // Writes to both the Pipeline log and the Symbol Groups run log.
    void appendGroupLog(const QString& text, const QColor& color = Qt::white);
    void setStepRunning(StepIndex index);
    void setStepDone(StepIndex index, bool success);
    void advanceToNextStep();
    bool confirmOutputFolder();
    bool checkStylePlanFresh();
    void loadCategoryPrompts(const QString& category);
    void launchStep(StepIndex index);
    void setState(AppState s);
    void updateStatusBar();
    QString buildEnvServer() const;
    void runNextGroupItem();
    void writeStylePlanJson(const QString& planPath, const SymbolGroup& group,
                            const SymbolItem& item, const QString& batchId,
                            const QString& assetRoot) const;

    // ── Data ────────────────────────────────────────────────────────────────
    AppConfig      config_;
    QSettings      settings_;
    QProcess       process_;

    // ── Group run state ──────────────────────────────────────────────────────
    QProcess                             groupProcess_;
    QList<QPair<SymbolGroup,SymbolItem>>  groupQueue_;   // (group context, item)
    QString                              groupOutputBase_;
    QString                              groupScriptMode_;
    int                                  groupTotalItems_ = 0;
    int                                  groupDoneItems_  = 0;
    AppState       state_       = AppState::IDLE;
    int            currentStep_ = 0;
    bool           runAll_      = false;
    QElapsedTimer  runTimer_;
    QTimer*        tickTimer_   = nullptr;

    // ── Retranslation ────────────────────────────────────────────────────────
    std::vector<std::function<void()>> retranslates_;

    // ── Widgets ─────────────────────────────────────────────────────────────
    // Config panel
    QGroupBox*      grpConfig_       = nullptr;
    QGroupBox*      grpPipelineSteps_ = nullptr;
    QGroupBox*      grpStepDetail_   = nullptr;
    QGroupBox*      grpLog_          = nullptr;
    QPushButton*    btnClearLog_     = nullptr;

    QLineEdit*      edtWorkingDir_;
    QLineEdit*      edtProjectRoot_;
    QLineEdit*      edtOutput_;
    QLineEdit*      edtAssetRoot_;
    QLineEdit*      edtHeroRef_;
    QPushButton*    btnLaunchComfy_;
    QPlainTextEdit* edtPositive_;
    QPlainTextEdit* edtNegative_;
    QLineEdit*      edtComfyUrl_;
    QLineEdit*      edtStyleProfile_;
    QLineEdit*      edtBatchId_;
    QComboBox*      cbxCategory_;
    QComboBox*      cbxMode_;
    QCheckBox*      chkIncludeSpine_;
    QDoubleSpinBox* spnIpWeight_;
    QDoubleSpinBox* spnDenoise_;
    QSpinBox*       spnSteps_;

    // Pipeline panel
    StepListWidget* stepList_;
    QPushButton*    btnRunAll_;
    QPushButton*    btnRunStep_;
    QPushButton*    btnRestart_;
    QPushButton*    btnStop_;
    QTextEdit*      detailView_;

    // Log panel
    QPlainTextEdit* logView_;

    // Status bar
    QProgressBar*   progressBar_;
    QLabel*         statusLabel_;
    QLabel*         elapsedLabel_;

    // Tab widget
    QTabWidget*           tabWidget_;
    SymbolGroupWidget*      symGroupWidget_    = nullptr;
    GroupBuilderWidget*     groupBuilder_      = nullptr;
    ResourceReplaceWidget*  resourceWidget_    = nullptr;
    AtlasWidget*            atlasWidget_       = nullptr;
    ImageWithPromptWidget*  iwpWidget_         = nullptr;
    Text2ImageWidget*       t2iWidget_         = nullptr;
    PoseGeneratorWidget*    poseGenWidget_     = nullptr;
};
