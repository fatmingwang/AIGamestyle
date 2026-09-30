#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// Spine .atlas parse/extract/merge logic, shared between AtlasWidget (GUI) and HeadlessRunner
// (the atlas-extract/atlas-merge headless commands) so there's exactly one implementation of
// "what does extracting/merging an atlas actually do" — the GUI additionally resolves its two
// interactive choices (multi-page layout, save conflict) via QMessageBox before calling in;
// headless mode takes them as plain command args. Every function here is pure/UI-free: progress
// and errors go into the `log` list the caller passes in, nothing is printed or shown directly.
namespace AtlasOps {

struct SpriteEntry {
    QString name;
    bool rotate = false;
    int x = 0, y = 0;         // position in atlas texture
    int w = 0, h = 0;         // size in atlas (may be swapped if rotated)
    int origW = 0, origH = 0;
    int offX = 0, offY = 0;   // Spine bottom-left offset from orig canvas
    int index = -1;
};

struct AtlasPage {
    QString textureName;
    int texW = 0, texH = 0;
    QList<SpriteEntry> sprites;
};

// Parses a Spine .atlas text file into pages/sprites, appending progress/error lines to `log`.
// Returns false (pages left however far parsing got) if the file couldn't be opened or no page
// was found at all.
bool parseAtlas(const QString& atlasPath, QList<AtlasPage>& pages, QStringList& log);

// "" for 1.0x, "_0.5x" for 0.5, "_2x" for 2.0, etc. — the filename tag a given scale gets.
QString scaleTag(double scale);

// Writes a new .atlas file with every coordinate multiplied by `scale`.
void saveAtlasFile(const QString& outPath, const QList<AtlasPage>& pages,
                    double scale, const QString& tag, QStringList& log,
                    const QList<int>& pageXOffsets = {});

struct ExtractResult {
    bool    ok = false;
    QString error;
    QString outputDir;
    int     saved   = 0;
    int     skipped = 0;
};

// Extracts every sprite out of the atlas's packed texture(s) into individual PNGs — trimmed
// region un-rotated, scaled, and placed back on its original-size (scaled) canvas — plus a
// companion scaled .atlas file next to the source atlas, into "<atlas dir>/atlas<scaleTag>/".
ExtractResult extractAtlas(const QString& atlasPath, double scale, QStringList& log);

// Multi-page atlases can be merged back as one combined image or as separate per-page images —
// AtlasWidget asks via a QMessageBox when there's more than one page; headless mode takes this
// as an explicit arg instead. Ignored (treated as SeparatePages) when the atlas has only one page.
enum class MergeMode { CombineHorizontal, SeparatePages };

// What to do when the destination texture file already exists — AtlasWidget asks via a
// QMessageBox per file; headless mode takes this as an explicit arg instead.
enum class SaveConflict { Replace, RenameMerged, Fail };

struct MergeResult {
    bool       ok = false;
    QString    error;
    QStringList savedFiles;
    int        merged  = 0;
    int        missing = 0;
};

// Packs the (possibly hand-edited or regenerated) PNGs from extractAtlas()'s output folder back
// into the atlas texture(s), producing a scaled .atlas file alongside. Requires extractAtlas()
// to have already been run at the same `scale` (its output folder is the expected source).
MergeResult mergeAtlas(const QString& atlasPath, double scale, MergeMode mode,
                        SaveConflict conflict, QStringList& log);

} // namespace AtlasOps
