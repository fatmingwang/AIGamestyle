#include "VideoTools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace VideoTools {

QString framesDirFor(const QString& videoPath)
{
    const QFileInfo f(videoPath);
    return f.dir().filePath(f.completeBaseName() + QStringLiteral("_frames"));
}

bool writeFrames(const QList<QImage>& frames, const QString& dir, QString* why)
{
    if (!QDir().mkpath(dir)) {
        if (why)
            *why = QStringLiteral("could not make the folder ") + dir;
        return false;
    }
    QDir d(dir);
    for (const QString& old : d.entryList({QStringLiteral("frame_*.png")}, QDir::Files))   // a longer take before this one
        d.remove(old);
    for (qsizetype i = 0; i < frames.size(); ++i) {
        const QString path = d.filePath(QStringLiteral("frame_%1.png").arg(i + 1, 4, 10, QLatin1Char('0')));
        if (!frames[i].save(path)) {
            if (why)
                *why = QStringLiteral("could not write ") + path;
            return false;
        }
    }
    return true;
}

QList<QImage> readFrames(const QString& dir)
{
    QList<QImage> out;
    const QDir d(dir);
    for (const QString& name : d.entryList({QStringLiteral("frame_*.png")}, QDir::Files, QDir::Name)) {
        QImage image(d.filePath(name));
        if (!image.isNull())
            out << image;
    }
    return out;
}

void encode(const QString& ffmpeg, const QString& framesDir, double fps, const QString& outMp4,
            const std::function<void(bool, const QString&)>& done)
{
    if (ffmpeg.isEmpty()) {
        done(false, QCoreApplication::translate("Song", "ffmpeg not found: put ffmpeg.exe next to ComfyLive.exe, or on PATH"));
        return;
    }
    // Frames with transparency (Remove background) are put on grey first: H.264 has none, and would show black.
    // libopenh264: the H.264 encoder the LGPL ffmpeg builds have (TOMS's tools/ffmpeg); it wants a bitrate, not a crf
    const QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                           QStringLiteral("-framerate"), QString::number(fps, 'f', 3),
                           QStringLiteral("-i"), QDir(framesDir).filePath(QStringLiteral("frame_%04d.png")),
                           QStringLiteral("-filter_complex"),
                           QStringLiteral("[0:v]format=rgba,split[a][b];[a]drawbox=x=0:y=0:w=iw:h=ih:color=0x9a9a9a@1.0:t=fill:replace=1[bg];"
                                          "[bg][b]overlay=format=auto,format=yuv420p"),
                           QStringLiteral("-c:v"), QStringLiteral("libopenh264"), QStringLiteral("-b:v"), QStringLiteral("8M"),
                           outMp4};
    auto* p = new QProcess;
#ifdef Q_OS_WIN
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
#endif
    QObject::connect(p, &QProcess::finished, p, [p, outMp4, done](int code, QProcess::ExitStatus status) {
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0 && QFileInfo(outMp4).size() > 0;
        done(ok, ok ? outMp4 : (err.isEmpty() ? QStringLiteral("ffmpeg exit %1").arg(code) : err));
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        done(false, QStringLiteral("could not start ffmpeg"));
    });
    p->start(ffmpeg, args);
}

}  // namespace VideoTools
