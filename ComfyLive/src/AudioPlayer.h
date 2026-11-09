#pragma once
#include <QObject>
#include <QString>
#include <QTimer>

// Plays one audio file with Windows' own player (MCI, winmm): MP3 and WAV always; FLAC / Opus when Windows has the
// codec. This Qt has no Multimedia module, and a small player is all ComfyLive needs. Elsewhere it does nothing.
//
// Never heard from a run in the background: --headless, --selftest and --screenshot never create one.
class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    bool load(const QString& path, QString* why);   // stops and closes the one before
    void play();                                      // from the start when it had ended
    void stop();
    void close();
    bool isPlaying() const;
    int lengthMs() const;
    int positionMs() const;
    QString path() const { return path_; }

signals:
    void stateChanged(bool playing);
    void position(int ms, int lengthMs);

private:
    QString path_;
    bool open_ = false;
    bool playing_ = false;
    QTimer poll_;
};
