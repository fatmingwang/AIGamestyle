#pragma once
#include <QImage>
#include <QList>
#include <QString>

#include <functional>

// The video helpers (README.md "Video"): a video graph ends in SaveImage, so ComfyUI gives its frames as pictures.
// ComfyLive keeps them as numbered PNG files (what the later tools read: the skeleton, the part cutter) and makes an
// MP4 of them with ffmpeg to look at.
namespace VideoTools {

// hero.mp4 -> hero_frames (next to it): frame_0001.png, frame_0002.png ...
QString framesDirFor(const QString& videoPath);

// The frames as frame_0001.png ... in dir (made if needed; frame_*.png already there are removed first).
bool writeFrames(const QList<QImage>& frames, const QString& dir, QString* why);

// frame_*.png of dir, in order. Empty: none there.
QList<QImage> readFrames(const QString& dir);

// The frames of dir as an H.264 MP4 at `fps` (transparent parts of the frames on grey: the MP4 is for looking at). Runs ffmpeg hidden and calls done(ok, message) when it ends (on the
// calling thread's event loop); message is the MP4's path, or what went wrong.
void encode(const QString& ffmpeg, const QString& framesDir, double fps, const QString& outMp4,
            const std::function<void(bool ok, const QString& message)>& done);

}  // namespace VideoTools
