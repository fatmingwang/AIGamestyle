#pragma once
#include <QWidget>
#include <QList>
#include <QMap>
#include <QProcess>
#include <functional>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QGroupBox;

// ── Data ────────────────────────────────────────────────────────────────────

struct IWPItem {
    QString filePath;       // absolute path to the source PNG
    QString category;       // key from categories.json ("" = default)
    QString positive;       // custom prompt (overrides category if non-empty)
    QString negative;
    QString lastOutputPath; // path of last successful output image
};

// ── Widget ───────────────────────────────────────────────────────────────────

class ImageWithPromptWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImageWithPromptWidget(QWidget* parent = nullptr);

    void setRunConfig(const QString& pythonExe,
                      const QString& projectRoot,
                      const QString& comfyuiServer);

public slots:
    void retranslateUi();

private slots:
    void onBrowseFolder();
    void onImportJson();
    void onExportJson();
    void onItemSelectionChanged();
    void onCategoryIndexChanged(int index);
    void onPositiveEdited();
    void onNegativeEdited();
    void onRunSingle();
    void onRunAll();
    void onSelectAll();
    void onStop();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void buildUi();
    void loadCategories();
    void scanFolder(const QString& folderPath);
    QString matchCategory(const QString& stem) const;
    void refreshItemDetail(int row);
    void updateButtons();
    void markItemColor(int row, const QColor& color);
    void startRun(int itemIndex);
    void startNextInBatch();
    void saveRunManifest(const QList<int>& indices);
    void appendLog(const QString& text, const QColor& color = Qt::white);

    QList<IWPItem> items_;
    QStringList    categoryKeys_;
    QMap<QString, QPair<QString,QString>> categoryPrompts_; // key → (positive, negative)
    QString        folderPath_;
    int            curItem_      = -1;
    int            anchorRow_    = -1;
    bool           busy_         = false;

    // Run state
    QProcess*  process_          = nullptr;
    bool       batchRunning_     = false;
    QList<int> batchQueue_;
    int        batchCurrent_     = -1;
    QString    batchOutDir_;

    // Config (injected by MainWindow)
    QString pythonExe_     = QStringLiteral("python");
    QString projectRoot_;
    QString comfyuiServer_ = QStringLiteral("127.0.0.1:8188");

    std::vector<std::function<void()>> retranslates_;

    // Top bar
    QLineEdit*       edtFolder_;

    // Parameters
    QDoubleSpinBox*  spnIpWeight_;
    QSpinBox*        spnSteps_;
    QDoubleSpinBox*  spnCfg_;
    QDoubleSpinBox*  spnDenoise_;
    QSpinBox*        spnSeed_;
    QCheckBox*       chkAutoSeed_;

    // Item list
    QListWidget*     wItems_;

    // Item detail
    QLabel*          lblItemName_;
    QLabel*          lblItemSize_;
    QLabel*          lblItemPreview_;
    QComboBox*       cbxCategory_;
    QPlainTextEdit*  edtPositive_;
    QPlainTextEdit*  edtNegative_;
    QPushButton*     btnRunSingle_;
    QPushButton*     btnSelectAll_;
    QPushButton*     btnRunAll_;
    QPushButton*     btnStop_;
    QLabel*          lblOutputPreview_;

    // Log
    QPlainTextEdit*  logView_;
};
