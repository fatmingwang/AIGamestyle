#pragma once
#include <QWidget>
#include <QList>
#include <QProcess>
#include <functional>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// ── Data ────────────────────────────────────────────────────────────────────

struct T2IItem {
    QString name;
    QString prompt;
    QString saveFilename;   // name with spaces stripped
    QString lastOutputPath;
    int     width  = 0;
    int     height = 0;
};

// ── Widget ───────────────────────────────────────────────────────────────────

class Text2ImageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit Text2ImageWidget(QWidget* parent = nullptr);

    void setRunConfig(const QString& pythonExe,
                      const QString& projectRoot,
                      const QString& comfyuiServer);

public slots:
    void retranslateUi();

private slots:
    void onAddItem();
    void onRemoveItem();
    void onImportJson();
    void onExportJson();
    void onParseTable();
    void onWakeOllama();
    void onWakeLlama();
    void onParseHeartbeat();
    void onParseProcessReadyRead();
    void onParseProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onItemSelectionChanged();
    void onNameEdited(const QString& text);
    void onPromptEdited();
    void onRunSingle();
    void onRunAll();
    void onStop();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void buildUi();
    void refreshItemDetail(int row);
    void updateButtons();
    void markItemColor(int row, const QColor& color);
    void startRun(int itemIndex);
    void startNextInBatch();
    void appendLog(const QString& text, const QColor& color = Qt::white);

    QList<T2IItem> items_;
    int            curItem_      = -1;
    bool           busy_         = false;

    // Run state
    QProcess*  process_          = nullptr;
    bool       batchRunning_     = false;
    QList<int> batchQueue_;
    int        batchCurrent_     = -1;
    QString    outDir_;

    // Parse-table state
    QProcess*  parseProcess_     = nullptr;
    QString    parseTempFile_;
    QString    parseOutputFile_;
    QString    parseBackend_;   // saved at dialog-accept, used on success to persist URL
    QString    parseApiUrl_;

    // Config (injected by MainWindow)
    QString pythonExe_     = QStringLiteral("python");
    QString projectRoot_;
    QString comfyuiServer_ = QStringLiteral("127.0.0.1:8188");

    std::vector<std::function<void()>> retranslates_;

    // UI
    QListWidget*    wItems_;
    QLineEdit*      edtName_;
    QLabel*         lblSaveFilename_;
    QLabel*         lblSize_;
    QPlainTextEdit* edtPrompt_;
    QPushButton*    btnRunSingle_;
    QPushButton*    btnRunAll_;
    QPushButton*    btnStop_;
    QLabel*         lblOutputPreview_;
    QPlainTextEdit* logView_;
    QPushButton*    btnParseTable_    = nullptr;
    QLabel*         lblParseStatus_   = nullptr;
    QTimer*         parseHeartbeat_   = nullptr;
    qint64          parseStartSecs_   = 0;
    QPushButton*    btnWakeOllama_   = nullptr;
    QLabel*         lblOllamaStatus_ = nullptr;
    QPushButton*    btnWakeLlama_    = nullptr;
    QLabel*         lblLlamaStatus_  = nullptr;
};
