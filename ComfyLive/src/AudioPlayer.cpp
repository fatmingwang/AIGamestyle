#include "AudioPlayer.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {

const wchar_t* kAlias = L"comfylive_audio";

#ifdef Q_OS_WIN
QString mci(const QString& command, bool* ok = nullptr)
{
    wchar_t answer[256] = {};
    const MCIERROR err = mciSendStringW(reinterpret_cast<const wchar_t*>(command.utf16()), answer, 256, nullptr);
    if (ok)
        *ok = (err == 0);
    if (err != 0) {
        wchar_t text[256] = {};
        mciGetErrorStringW(err, text, 256);
        return QString::fromWCharArray(text);
    }
    return QString::fromWCharArray(answer);
}
#endif

}  // namespace

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent)
{
    poll_.setInterval(200);
    connect(&poll_, &QTimer::timeout, this, [this] {
        const bool now = isPlaying();
        emit position(positionMs(), lengthMs());
        if (now != playing_) {
            playing_ = now;
            emit stateChanged(now);
        }
        if (!now)
            poll_.stop();
    });
}

AudioPlayer::~AudioPlayer()
{
    // silent: whoever listened (the window's Play button) may already be gone when the player goes
    blockSignals(true);
    close();
}

bool AudioPlayer::load(const QString& path, QString* why)
{
    close();
#ifdef Q_OS_WIN
    bool ok = false;
    // "mpegvideo" is DirectShow / Media Foundation: MP3, WAV, and whatever codecs Windows has
    const QString reply = mci(QStringLiteral("open \"%1\" type mpegvideo alias %2").arg(QDir::toNativeSeparators(path), QString::fromWCharArray(kAlias)), &ok);
    if (!ok) {
        if (why)
            *why = reply;
        return false;
    }
    mci(QStringLiteral("set %1 time format milliseconds").arg(QString::fromWCharArray(kAlias)));
    open_ = true;
    path_ = path;
    return true;
#else
    if (why)
        *why = QStringLiteral("playing audio is only built for Windows");
    return false;
#endif
}

void AudioPlayer::play()
{
#ifdef Q_OS_WIN
    if (!open_)
        return;
    const QString a = QString::fromWCharArray(kAlias);
    if (positionMs() >= lengthMs() - 50)
        mci(QStringLiteral("seek %1 to start").arg(a));
    mci(QStringLiteral("play %1").arg(a));
    playing_ = true;
    emit stateChanged(true);
    poll_.start();
#endif
}

void AudioPlayer::stop()
{
#ifdef Q_OS_WIN
    if (!open_)
        return;
    const QString a = QString::fromWCharArray(kAlias);
    mci(QStringLiteral("stop %1").arg(a));
    mci(QStringLiteral("seek %1 to start").arg(a));
    poll_.stop();
    if (playing_) {
        playing_ = false;
        emit stateChanged(false);
    }
    emit position(0, lengthMs());
#endif
}

void AudioPlayer::close()
{
#ifdef Q_OS_WIN
    if (!open_)
        return;
    poll_.stop();
    mci(QStringLiteral("close %1").arg(QString::fromWCharArray(kAlias)));
    open_ = false;
    path_.clear();
    if (playing_) {
        playing_ = false;
        emit stateChanged(false);
    }
#endif
}

bool AudioPlayer::isPlaying() const
{
#ifdef Q_OS_WIN
    return open_ && mci(QStringLiteral("status %1 mode").arg(QString::fromWCharArray(kAlias))) == QStringLiteral("playing");
#else
    return false;
#endif
}

int AudioPlayer::lengthMs() const
{
#ifdef Q_OS_WIN
    return open_ ? mci(QStringLiteral("status %1 length").arg(QString::fromWCharArray(kAlias))).toInt() : 0;
#else
    return 0;
#endif
}

int AudioPlayer::positionMs() const
{
#ifdef Q_OS_WIN
    return open_ ? mci(QStringLiteral("status %1 position").arg(QString::fromWCharArray(kAlias))).toInt() : 0;
#else
    return 0;
#endif
}
