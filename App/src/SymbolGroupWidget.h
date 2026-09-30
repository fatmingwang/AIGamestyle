#pragma once
#include <QWidget>
#include <QList>
#include <QColor>
#include <QProcess>
#include <functional>
#include <vector>

class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QDoubleSpinBox;
class QGroupBox;
class QComboBox;

// ── Data structures ──────────────────────────────────────────────────────────

struct SymbolItem {
    QString filePath;
    QString prompt;
    QString negativePrompt;
};

struct SymbolGroup {
    QString name;
    QString referencePath;
    double  ipWeight = 0.75;
    double  denoise  = 0.80;
    QList<SymbolItem> items;
};

// ── Widget ───────────────────────────────────────────────────────────────────

class SymbolGroupWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SymbolGroupWidget(QWidget* parent = nullptr);

    // Called by MainWindow when a group run finishes to re-enable the button.
    void setRunEnabled(bool v);

    // Called by MainWindow to inject groups built in the Group Builder tab.
    void loadGroups(const QList<SymbolGroup>& groups);

    // Called by MainWindow whenever Pipeline config changes.
    void setRunConfig(const QString& pythonExe,
                      const QString& projectRoot,
                      const QString& comfyuiServer);

public slots:
    void retranslateUi();
    void appendGroupLog(const QString& text, const QColor& color = Qt::white);

signals:
    // Emitted when user clicks "Apply Group → Pipeline".
    void applyGroupRequested(const SymbolGroup& group);

    // Emitted when user clicks "Run Checked ▶".
    // Carries every checked group and the selected script mode.
    // Receiver calls setRunEnabled(true) when done.
    void runGroupsRequested(const QList<SymbolGroup>& groups, const QString& scriptMode);

private slots:
    void onImportJson();
    void onExportJson();
    void onGroupChanged(int row);
    void onItemChanged(int row);
    void onBrowseReference();
    void onReferenceEdited(const QString& path);
    void onIpWeightChanged(double v);
    void onDenoiseChanged(double v);
    void onPromptEdited();
    void onNegPromptEdited();
    void onRunSingleItem();
    void onSingleRunReadyRead();
    void onSingleRunFinished(int exitCode, QProcess::ExitStatus status);

private:
    void buildUi();
    bool loadJson(const QString& path);
    bool saveJson(const QString& path) const;
    void refreshGroupList();
    void refreshGroupDetail(int g);
    void refreshItemList(int g);
    void refreshItemDetail(int g, int i);
    void setThumb(const QString& path);
    void updateRunButton();

    QList<SymbolGroup> groups_;
    QString            loadedPath_;
    int                curGroup_ = -1;
    int                curItem_  = -1;
    bool               busy_     = false;

    // Single-item run
    QProcess*  singleRunProcess_  = nullptr;
    QString    pythonExe_         = QStringLiteral("python");
    QString    projectRoot_;
    QString    comfyuiServer_     = QStringLiteral("127.0.0.1:8188");
    QString    singleRunOutputDir_;

    std::vector<std::function<void()>> retranslates_;

    // Top bar
    QLabel*          lblFile_;

    // Left panel
    QListWidget*     wGroups_;

    // Center panel — group settings
    QLineEdit*       edtRef_;
    QLabel*          lblThumb_;
    QDoubleSpinBox*  spnIp_;
    QDoubleSpinBox*  spnDen_;
    QComboBox*       cbxScript_;
    QPushButton*     btnApply_;
    QPushButton*     btnRun_;

    // Run log (below the 3-panel splitter)
    QPlainTextEdit*  logGroup_;

    // Center panel — item list
    QListWidget*     wItems_;

    // Right panel — item detail
    QLabel*          lblItemFile_;
    QLabel*          lblItemPreview_;
    QPlainTextEdit*  edtPrompt_;
    QPlainTextEdit*  edtNeg_;
    QPushButton*     btnRunItem_;
    QPushButton*     btnStopItem_;
    QPlainTextEdit*  logItem_;
    QLabel*          lblOutputPreview_;
};
