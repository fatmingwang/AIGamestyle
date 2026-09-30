#pragma once
#include <QMainWindow>
#include <QTcpSocket>
#include <QJsonObject>
#include <QColor>

class QLineEdit;
class QComboBox;
class QPushButton;
class QPlainTextEdit;
class QLabel;

// A small standalone GUI for exercising GameStyleAI's `--headless` TCP server by hand: connect,
// pick a command, send it, watch the NDJSON events come back — the GUI equivalent of
// App/tools/headless_client.py. See Doc/Commands_Reference.md §12 for the protocol this speaks.
class ClientWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit ClientWindow(QWidget* parent = nullptr);

private slots:
    void onConnectClicked();
    void onSendClicked();
    void onBrowseClicked();
    void onHelpClicked();
    void onCommandChanged(const QString& command);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void buildUi();
    void sendCommand(const QString& command, const QJsonObject& args);
    void handleLine(const QByteArray& line);
    void appendLog(const QString& text, const QColor& color = QColor(220, 220, 220));
    void setConnectedState(bool connected);

    QTcpSocket* socket_ = nullptr;

    QLineEdit*      edtHost_     = nullptr;
    QLineEdit*      edtPort_     = nullptr;
    QPushButton*    btnConnect_  = nullptr;
    QLabel*         lblStatus_   = nullptr;

    QComboBox*      cboCommand_  = nullptr;
    QLineEdit*      edtArgs_     = nullptr;
    QPushButton*    btnBrowse_   = nullptr;
    QPushButton*    btnHelp_     = nullptr;
    QPushButton*    btnSend_     = nullptr;

    QPlainTextEdit* logView_     = nullptr;
    QPushButton*    btnClearLog_ = nullptr;
};
