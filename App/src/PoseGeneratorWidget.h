#pragma once
#include <QWidget>
#include <QProcess>
#include <functional>
#include <vector>

class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QTimer;

// ── Widget ───────────────────────────────────────────────────────────────────
// Subject Type picks which pipeline runs underneath:
//   Human / 2-Legged Animal -> skeleton section: AI writes a keyframe pose
//     JSON (poses_library/<name>.json) for generate_pose_sequence.py's human
//     biped rig, then renders an OpenPose PNG sequence for ControlNet.
//   4-Legged Animal / Bird / Fish / Other -> creature section: a reference
//     image + motion prompt drive Kling image-to-video via
//     generate_creature_animation.py, no skeleton involved.
// The human rig cannot represent non-biped body plans (see
// Doc/Sprite_Animation_To_Atlas_Pipeline.md) -- that's why these are two
// different pipelines rather than one combo hint fed into the same rig.

class PoseGeneratorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PoseGeneratorWidget(QWidget* parent = nullptr);

    void setRunConfig(const QString& pythonExe,
                      const QString& projectRoot,
                      const QString& comfyuiServer);

public slots:
    void retranslateUi();

private slots:
    void onSubjectTypeChanged(int index);
    void onTestBackendUrl();
    void onGeneratePoseData();
    void onGenProcessReadyRead();
    void onGenProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onGenerateHeartbeat();
    void onLoadJson();
    void onSaveJson();
    void onBrowseOutputDir();
    void onGeneratePictures();
    void onRenderProcessReadyRead();
    void onRenderProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onBrowseRefImage();
    void onGenerateAnimation();
    void onCreatureProcessReadyRead();
    void onCreatureProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onCreatureHeartbeat();

private:
    void buildUi();
    void appendLog(const QString& text, const QColor& color = Qt::white);
    void updateButtons();
    QString posesLibraryDir() const;
    bool isSkeletonMode() const;

    // Config (injected by MainWindow)
    QString pythonExe_     = QStringLiteral("python");
    QString projectRoot_;
    QString comfyuiServer_ = QStringLiteral("127.0.0.1:8188");

    QProcess* genProcess_      = nullptr;
    QProcess* renderProcess_   = nullptr;
    QProcess* creatureProcess_ = nullptr;
    QString   lastJsonPath_;

    QTimer* genHeartbeat_      = nullptr;
    qint64  genStartSecs_      = 0;
    QTimer* creatureHeartbeat_ = nullptr;
    qint64  creatureStartSecs_ = 0;

    std::vector<std::function<void()>> retranslates_;

    // Shared top row
    QLineEdit*   edtName_;
    QComboBox*   cbxSubjectType_;
    QLineEdit*   edtOutputDir_;

    // Skeleton pipeline (Human / 2-Legged Animal)
    QWidget* skeletonSection_;

    QComboBox*   cbxBackend_;
    QLineEdit*   edtUrl_;
    QPushButton* btnTest_;
    QLabel*      lblTestStatus_;

    QCheckBox*      chkLoop_;
    QSpinBox*       spnHintFrames_;
    QPlainTextEdit* edtDescription_;
    QPushButton*    btnGenerate_;
    QLabel*         lblGenerateStatus_;

    QPlainTextEdit* edtJson_;
    QPushButton*    btnLoadJson_;
    QPushButton*    btnSaveJson_;
    QLabel*         lblJsonPath_;

    QComboBox*   cbxFacing_;
    QSpinBox*    spnRenderFrames_;
    QPushButton* btnGeneratePictures_;
    QLabel*      lblRenderStatus_;

    // Creature pipeline (4-Legged Animal / Bird / Fish / Other)
    QWidget* creatureSection_;

    QLineEdit*      edtRefImage_;
    QPlainTextEdit* edtMotionPrompt_;
    QLineEdit*      edtNegativePrompt_;
    QComboBox*      cbxDuration_;
    QComboBox*      cbxAspectRatio_;
    QComboBox*      cbxKlingModel_;
    QComboBox*      cbxKlingMode_;
    QSpinBox*       spnCreatureFrames_;
    QPushButton*    btnGenerateAnimation_;
    QLabel*         lblCreatureStatus_;

    // Shared bottom
    QLabel*         lblPreview_;
    QPlainTextEdit* logView_;
};
