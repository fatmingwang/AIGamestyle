#pragma once
#include <QWidget>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QProcess>
#include <functional>
#include <vector>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class QGroupBox;
class QFrame;
class QPlainTextEdit;
class QJsonObject;

class ResourceReplaceWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ResourceReplaceWidget(QWidget* parent = nullptr);

public slots:
    void retranslateUi();

private slots:
    void onStartReplace();
    void onCommitPush();
    void onUploadToNas();
    void onUploadProcessOutput();
    void onUploadProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void buildUi();
    void loadFormData();
    void saveFormData();
    void showSuccess(const QString& message, const QString& output = {});
    void showError(const QString& message);
    void hideMessages();
    bool validateForm();
    void setAllButtonsEnabled(bool enabled);
    void appendLog(const QString& msg);
    QStringList buildUrls(const QString& serverAddress, const QString& endpoint) const;
    void tryUrls(const QStringList& urls, int idx, const QByteArray& body,
                 std::function<void(bool, const QJsonObject&, const QString&)> cb);

    QSettings               settings_;
    QNetworkAccessManager*  nam_;
    QProcess*               uploadProcess_ = nullptr;

    std::vector<std::function<void()>> retranslates_;

    // Labels / group boxes that need retranslation
    QLabel*         lblTitle_;
    QGroupBox*      grpNasUpload_;
    QLabel*         lblNasRoot_;
    QGroupBox*      grpServer_;
    QGroupBox*      grpForm_;
    QLabel*         lblTargetFolder_;
    QLabel*         lblGameId_;
    QLabel*         lblPlatform_;
    QLabel*         lblUserName_;
    QLabel*         lblDeployEnv_;

    QLineEdit*      edtServer_;
    QLineEdit*      edtNasRoot_;
    QLineEdit*      edtTargetFolder_;
    QLineEdit*      edtGameId_;
    QLineEdit*      edtPlatform_;
    QLineEdit*      edtUserName_;
    QCheckBox*      chkDev_;
    QCheckBox*      chkQA_;

    QPushButton*    btnHelp_;
    QPushButton*    btnReplace_;
    QPushButton*    btnCommitPush_;
    QLineEdit*      edtUploadFolder_;
    QPushButton*    btnBrowseUpload_;
    QLineEdit*      edtNasUploadTarget_;
    QPushButton*    btnUploadToNas_;

    QFrame*         successFrame_;
    QLabel*         successMsg_;
    QFrame*         errorFrame_;
    QLabel*         errorMsg_;
    QFrame*         outputFrame_;
    QPlainTextEdit* outputContent_;

    QPlainTextEdit* logView_;
};
