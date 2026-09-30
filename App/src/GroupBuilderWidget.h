#pragma once
#include <QWidget>
#include <QList>
#include <QProcess>
#include <functional>
#include <vector>
#include "SymbolGroupWidget.h"   // reuses SymbolGroup / SymbolItem structs

class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QDoubleSpinBox;
class QComboBox;
class QLabel;

class GroupBuilderWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GroupBuilderWidget(QWidget* parent = nullptr);

    void setPythonExe(const QString& exe);
    void setProjectRoot(const QString& root);

public slots:
    void retranslateUi();

signals:
    // Emitted by "→ Open in Symbol Groups"; MainWindow forwards to SymbolGroupWidget.
    void groupsReadyToLoad(const QList<SymbolGroup>& groups);

private slots:
    void onBrowseFolder();
    void onScan();
    void onNewGroupFromSelected();
    void onAutoGroupByPrefix();
    void onVisionGroup();
    void onGroupRowChanged(int row);
    void onDeleteGroup();
    void onGroupNameEdited(const QString& name);
    void onRefEdited(const QString& path);
    void onBrowseRef();
    void onBrowseStyleRef();
    void onAnalysisReadyRead();
    void onAnalysisFinished(int exitCode, QProcess::ExitStatus status);
    void onCheckBackend();
    void onExportJson();
    void onSendToSymbolGroups();

private:
    void buildUi();
    void refreshGroupList();
    void refreshGroupDetail(int g);
    QStringList checkedFilePaths() const;
    void setDetailEnabled(bool v);
    bool loadJsonFile(const QString& path);
    void appendLog(const QString& text, const QColor& color = Qt::white);

    QList<SymbolGroup> groups_;
    int     curGroup_    = -1;
    bool    busy_        = false;
    QString pythonExe_   = QStringLiteral("python");
    QString projectRoot_;

    std::vector<std::function<void()>> retranslates_;

    // Vision analysis process
    QProcess* analysisProcess_ = nullptr;

    // Top bar
    QLineEdit*      edtFolder_;
    QLineEdit*      edtStyleRef_;
    QLineEdit*      edtStyleDirective_;
    QPushButton*    btnBrowseStyleRef_;
    QComboBox*      cbxBackend_;
    QComboBox*      cbxApiUrl_;   // editable, history persisted in QSettings
    QPushButton*    btnCheckBackend_;
    QLineEdit*      edtModel_;
    QPushButton*    btnVisionGroup_;
    QLabel*         lblVisionStatus_;

    // Left panel — file list
    QListWidget*    wFiles_;
    QLabel*         lblPreview_;

    // Right panel — group list
    QListWidget*    wGroups_;
    QPushButton*    btnDeleteGroup_;

    // Right panel — group detail
    QLineEdit*      edtGroupName_;
    QLineEdit*      edtRef_;
    QPushButton*    btnBrowseRef_;
    QDoubleSpinBox* spnIp_;
    QDoubleSpinBox* spnDen_;
    QPlainTextEdit* edtPrompt_;
    QPlainTextEdit* edtNeg_;

    // Bottom bar
    QPushButton*    btnSend_;

    // Log
    QPlainTextEdit* logView_;
};
