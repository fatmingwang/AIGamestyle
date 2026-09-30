#pragma once
#include <QWidget>
#include <QSettings>
#include <functional>
#include <vector>

class QLineEdit;
class QDoubleSpinBox;
class QPushButton;
class QPlainTextEdit;
class QLabel;
class QGroupBox;

class AtlasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AtlasWidget(QWidget* parent = nullptr);

public slots:
    void retranslateUi();

private slots:
    void onBrowseAtlas();
    void onExtract();
    void onMerge();

private:
    // Parse/extract/merge logic lives in AtlasOps.h (shared with the headless
    // atlas-extract/atlas-merge commands) — this widget only owns the UI and the two
    // interactive choices (multi-page layout, save conflict) AtlasOps takes as plain args.
    void buildUi();
    void appendLog(const QString& msg);

    QSettings        settings_;

    std::vector<std::function<void()>> retranslates_;

    QLabel*          lblTitle_;
    QGroupBox*       grpAtlasFiles_;
    QLabel*          lblAtlasFile_;
    QLabel*          lblScale_;

    QLineEdit*       edtAtlasFile_;
    QDoubleSpinBox*  spnScale_;
    QPushButton*     btnBrowse_;
    QPushButton*     btnExtract_;
    QPushButton*     btnMerge_;
    QPlainTextEdit*  logView_;
};
