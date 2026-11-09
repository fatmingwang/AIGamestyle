#pragma once
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// The song helpers for ACE-Step graphs (README.md "Music"): the singer and the voice as words in the tags, the key one
// semitone up or down, and a finished song transposed with ffmpeg (the same melody, higher or lower).
namespace SongTools {

// One choice of a list: its id (settings, headless), its English label (tr() in the window) and the words it puts in
// the tags. An empty id is "leave the tags as they are".
struct Choice {
    QString id;
    const char* label;
    QString tags;
};
const QList<Choice>& singers();   // male, female, duet, choir, instrumental
const QList<Choice>& voices();    // soft, breathy, powerful, falsetto, raspy

// The tags with every comma item of that list taken out and the chosen one put first (id empty: only taken out).
QString applyChoice(const QString& tags, const QList<Choice>& list, const QString& id);
// The id whose words are in the tags (the first one found), else "".
QString detectChoice(const QString& tags, const QList<Choice>& list);

// "D minor" one semitone up -> "D# minor" (or "Eb minor": the spelling the list has first). Empty when it is not a key.
QString stepKey(const QString& keyscale, int semitones, const QStringList& choices);

// A song language's full name for its code ("en" -> "English", "yue" -> "Cantonese"), in the window's language. The
// code itself is what the graph keeps. An unknown code comes back as it is.
QString languageName(const QString& code);

// ffmpeg: the setting, else next to the exe, else the TOMS copy (tools/ffmpeg), else PATH. Empty: none.
QString findFfmpeg(const QString& setting = QString());

// The output name for a transposed song: song.mp3 + 3 -> song_+3st.mp3 (next to it).
QString transposedPath(const QString& in, int semitones);

// Same melody, `semitones` higher (or lower: negative), same length: asetrate + atempo. Runs ffmpeg hidden and calls
// done(ok, message) when it ends (on the calling thread's event loop).
void transpose(const QString& ffmpeg, const QString& in, int semitones, const QString& out,
               const std::function<void(bool ok, const QString& message)>& done);

}  // namespace SongTools
