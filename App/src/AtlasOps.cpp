#include "AtlasOps.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QStringConverter>
#include <QImage>
#include <QPainter>
#include <QTransform>

namespace AtlasOps {

bool parseAtlas(const QString& atlasPath, QList<AtlasPage>& pages, QStringList& log)
{
    QFile file(atlasPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        log << QStringLiteral("Cannot open file: %1").arg(atlasPath);
        return false;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    auto parseInts = [](const QString& s, int& a, int& b) {
        const auto parts = s.split(',');
        if (parts.size() >= 2) {
            a = parts[0].trimmed().toInt();
            b = parts[1].trimmed().toInt();
        }
    };

    AtlasPage* curPage = nullptr;
    SpriteEntry* curSprite = nullptr;
    bool expectingTexture = true;

    while (!in.atEnd()) {
        const QString raw = in.readLine();
        const QString trimmed = raw.trimmed();

        if (trimmed.isEmpty()) {
            expectingTexture = true;
            curSprite = nullptr;
            continue;
        }

        // Indented line -> sprite property
        if (!raw.isEmpty() && (raw.at(0) == ' ' || raw.at(0) == '\t')) {
            if (!curSprite || !curPage) continue;
            if (trimmed.startsWith(QLatin1String("rotate:")))
                curSprite->rotate = trimmed.mid(7).trimmed().compare(
                    QStringLiteral("true"), Qt::CaseInsensitive) == 0;
            else if (trimmed.startsWith(QLatin1String("xy:")))
                parseInts(trimmed.mid(3).trimmed(), curSprite->x, curSprite->y);
            else if (trimmed.startsWith(QLatin1String("size:")))
                parseInts(trimmed.mid(5).trimmed(), curSprite->w, curSprite->h);
            else if (trimmed.startsWith(QLatin1String("orig:")))
                parseInts(trimmed.mid(5).trimmed(), curSprite->origW, curSprite->origH);
            else if (trimmed.startsWith(QLatin1String("offset:")))
                parseInts(trimmed.mid(7).trimmed(), curSprite->offX, curSprite->offY);
            else if (trimmed.startsWith(QLatin1String("index:")))
                curSprite->index = trimmed.mid(6).trimmed().toInt();
            continue;
        }

        // Non-indented line
        if (expectingTexture) {
            pages.append(AtlasPage{});
            curPage = &pages.last();
            curPage->textureName = trimmed;
            expectingTexture = false;
            curSprite = nullptr;
            continue;
        }
        if (!curPage) continue;

        // Page-level properties
        if (trimmed.startsWith(QLatin1String("size:"))) {
            parseInts(trimmed.mid(5).trimmed(), curPage->texW, curPage->texH);
        } else if (trimmed.startsWith(QLatin1String("format:")) ||
                   trimmed.startsWith(QLatin1String("filter:")) ||
                   trimmed.startsWith(QLatin1String("repeat:"))) {
            // ignored
        } else {
            // Sprite name
            curPage->sprites.append(SpriteEntry{});
            curSprite = &curPage->sprites.last();
            curSprite->name = trimmed;
        }
    }

    if (pages.isEmpty()) {
        log << QStringLiteral("No pages parsed; confirm the file is a Spine Atlas.");
        return false;
    }
    return true;
}

QString scaleTag(double scale)
{
    if (qFuzzyCompare(scale, 1.0)) return {};           // no suffix for 1x
    const int cents = qRound(scale * 100);
    if (cents % 100 == 0) return QStringLiteral("_%1x").arg(cents / 100);
    if (cents % 10  == 0) return QStringLiteral("_%1x").arg(scale, 0, 'f', 1);
    return                  QStringLiteral("_%1x").arg(scale, 0, 'f', 2);
}

void saveAtlasFile(const QString& outPath, const QList<AtlasPage>& pages,
                    double scale, const QString& tag, QStringList& log,
                    const QList<int>& pageXOffsets)
{
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        log << QStringLiteral("  [Error] Cannot save atlas file: %1").arg(outPath);
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    for (int pi = 0; pi < pages.size(); ++pi) {
        const AtlasPage& page = pages[pi];
        const int xOff = (pi < pageXOffsets.size()) ? pageXOffsets[pi] : 0;

        const QFileInfo fi(page.textureName);
        const QString scaledTex = tag.isEmpty()
            ? page.textureName
            : fi.baseName() + tag + QStringLiteral(".") + fi.suffix();

        out << "\n"
            << scaledTex << "\n"
            << "size: " << qRound(page.texW * scale) << ","
                        << qRound(page.texH * scale) << "\n"
            << "format: RGBA8888\n"
            << "filter: Linear,Linear\n"
            << "repeat: none\n";

        for (const SpriteEntry& sp : page.sprites) {
            out << sp.name << "\n"
                << "  rotate: " << (sp.rotate ? "true" : "false") << "\n"
                << "  xy: "     << qRound(sp.x    * scale) + xOff << ", "
                                << qRound(sp.y    * scale) << "\n"
                << "  size: "   << qRound(sp.w    * scale) << ", "
                                << qRound(sp.h    * scale) << "\n"
                << "  orig: "   << qRound(sp.origW * scale) << ", "
                                << qRound(sp.origH * scale) << "\n"
                << "  offset: " << qRound(sp.offX  * scale) << ", "
                                << qRound(sp.offY  * scale) << "\n"
                << "  index: "  << sp.index << "\n";
        }
    }
    log << QStringLiteral("  [Atlas] Saved: %1").arg(outPath);
}

ExtractResult extractAtlas(const QString& atlasPath, double scale, QStringList& log)
{
    ExtractResult result;

    QList<AtlasPage> pages;
    if (!parseAtlas(atlasPath, pages, log)) {
        result.error = QStringLiteral("Could not parse atlas: %1").arg(atlasPath);
        return result;
    }

    const QString tag = scaleTag(scale);
    const QDir atlasDir = QFileInfo(atlasPath).absoluteDir();
    const QString outDirPath = atlasDir.absoluteFilePath(QStringLiteral("atlas") + tag);
    const QDir outDir(outDirPath);
    result.outputDir = outDirPath;

    if (!outDir.exists() && !QDir().mkpath(outDirPath)) {
        result.error = QStringLiteral("Cannot create output folder: %1").arg(outDirPath);
        return result;
    }

    log << QStringLiteral("=== Start extract (scale=%1) -> %2 ===").arg(scale).arg(outDirPath);

    for (const AtlasPage& page : pages) {
        const QString texPath = atlasDir.absoluteFilePath(page.textureName);
        const QImage tex(texPath);
        if (tex.isNull()) {
            log << QStringLiteral("[Skip] Cannot load texture: %1").arg(texPath);
            continue;
        }

        log << QStringLiteral("[Page] %1  %2x%3  (%4 sprites)")
                   .arg(page.textureName).arg(tex.width()).arg(tex.height())
                   .arg(page.sprites.size());

        for (const SpriteEntry& sp : page.sprites) {
            QImage region = tex.copy(sp.x, sp.y, sp.w, sp.h);
            if (region.isNull()) {
                log << QStringLiteral("  [Skip] %1 - region out of bounds").arg(sp.name);
                ++result.skipped;
                continue;
            }

            if (sp.rotate) {
                QTransform t;
                t.rotate(-90.0);
                region = region.transformed(t, Qt::SmoothTransformation);
            }
            const int natW = sp.rotate ? sp.h : sp.w;
            const int natH = sp.rotate ? sp.w : sp.h;

            const int sNatW = qMax(1, qRound(natW * scale));
            const int sNatH = qMax(1, qRound(natH * scale));
            if (!qFuzzyCompare(scale, 1.0))
                region = region.scaled(sNatW, sNatH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

            const int offY_screen = sp.origH - natH - sp.offY;
            const int sOrigW = qMax(1, qRound(sp.origW * scale));
            const int sOrigH = qMax(1, qRound(sp.origH * scale));
            const int sOffX  = qRound(sp.offX    * scale);
            const int sOffY  = qRound(offY_screen * scale);

            QImage canvas(sOrigW, sOrigH, QImage::Format_ARGB32);
            canvas.fill(Qt::transparent);
            {
                QPainter p(&canvas);
                p.drawImage(sOffX, sOffY, region);
            }

            QString filename = sp.name;
            filename.replace('/', '_').replace('\\', '_');
            const QString outPath = outDir.absoluteFilePath(filename + QStringLiteral(".png"));

            if (!canvas.save(outPath, "PNG")) {
                log << QStringLiteral("  [Error] Cannot save: %1").arg(outPath);
                ++result.skipped;
            } else {
                log << QStringLiteral("  [OK] %1  %2x%3 -> %4x%5")
                           .arg(sp.name).arg(sp.origW).arg(sp.origH).arg(sOrigW).arg(sOrigH);
                ++result.saved;
            }
        }
    }

    {
        const QString atlasOut = atlasDir.absoluteFilePath(
            QFileInfo(atlasPath).baseName() + (tag.isEmpty() ? QStringLiteral("_1x") : tag)
            + QStringLiteral(".atlas"));
        saveAtlasFile(atlasOut, pages, scale, tag, log);
    }

    log << QStringLiteral("=== Done: saved %1, skipped %2 ===").arg(result.saved).arg(result.skipped);
    result.ok = true;
    return result;
}

// Resolves what to actually write `texPath` as, per `conflict` — the headless/pure equivalent
// of AtlasWidget's resolveSavePath() QMessageBox. Empty return = don't save this one.
static QString resolveSavePath(const QString& texPath, SaveConflict conflict, QStringList& log)
{
    if (!QFile::exists(texPath))
        return texPath;

    switch (conflict) {
    case SaveConflict::Replace:
        return texPath;
    case SaveConflict::RenameMerged: {
        const QFileInfo fi(texPath);
        return fi.absolutePath() + QDir::separator() +
               fi.baseName() + QStringLiteral("_merged.") + fi.suffix();
    }
    case SaveConflict::Fail:
    default:
        log << QStringLiteral("[Skip] Target already exists: %1").arg(texPath);
        return QString();
    }
}

MergeResult mergeAtlas(const QString& atlasPath, double scale, MergeMode mode,
                        SaveConflict conflict, QStringList& log)
{
    MergeResult result;

    QList<AtlasPage> pages;
    if (!parseAtlas(atlasPath, pages, log)) {
        result.error = QStringLiteral("Could not parse atlas: %1").arg(atlasPath);
        return result;
    }

    const QString tag = scaleTag(scale);
    const QDir atlasDir = QFileInfo(atlasPath).absoluteDir();

    const QString srcDirPath = atlasDir.absoluteFilePath(QStringLiteral("atlas") + tag);
    const QDir srcDir(srcDirPath);
    if (!srcDir.exists()) {
        result.error = QStringLiteral(
            "Source folder not found: %1 — run atlas-extract first with the same scale.")
            .arg(srcDirPath);
        return result;
    }

    const bool mergeIntoOne = (mode == MergeMode::CombineHorizontal) && pages.size() > 1;

    log << QStringLiteral("=== Start merge back (scale=%1) <- %2 ===").arg(scale).arg(srcDirPath);

    struct PageResult {
        QString texPath;
        QImage  canvas;
        int     merged  = 0;
        int     missing = 0;
    };
    QList<PageResult> results;

    for (const AtlasPage& page : pages) {
        const QString texPath = atlasDir.absoluteFilePath(page.textureName);

        int canvasW = qRound(page.texW * scale), canvasH = qRound(page.texH * scale);
        {
            const QImage origTex(texPath);
            if (!origTex.isNull()) {
                canvasW = qMax(1, qRound(origTex.width()  * scale));
                canvasH = qMax(1, qRound(origTex.height() * scale));
            }
        }
        if (canvasW <= 0 || canvasH <= 0) {
            log << QStringLiteral("[Skip] Cannot determine canvas size for %1").arg(page.textureName);
            continue;
        }

        log << QStringLiteral("[Page] %1  canvas %2x%3").arg(page.textureName).arg(canvasW).arg(canvasH);

        QImage canvas(canvasW, canvasH, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);

        int merged = 0, missing = 0;
        for (const SpriteEntry& sp : page.sprites) {
            QString filename = sp.name;
            filename.replace('/', '_').replace('\\', '_');
            const QImage img(srcDir.absoluteFilePath(filename + QStringLiteral(".png")));

            if (img.isNull()) {
                log << QStringLiteral("  [Missing] %1").arg(sp.name);
                ++missing;
                continue;
            }

            const int natW   = sp.rotate ? sp.h : sp.w;
            const int natH   = sp.rotate ? sp.w : sp.h;
            const int sNatW  = qMax(1, qRound(natW    * scale));
            const int sNatH  = qMax(1, qRound(natH    * scale));
            const int sOrigW = qMax(1, qRound(sp.origW * scale));
            const int sOrigH = qMax(1, qRound(sp.origH * scale));

            QImage packed;
            if (img.width() == sOrigW && img.height() == sOrigH) {
                const int offY_screen = sp.origH - natH - sp.offY;
                packed = img.copy(qRound(sp.offX    * scale),
                                  qRound(offY_screen * scale),
                                  sNatW, sNatH);
            } else if (img.width() == sNatW && img.height() == sNatH) {
                packed = img;
            } else {
                log << QStringLiteral("  [Warning] %1 size mismatch, scaling").arg(sp.name);
                packed = img.scaled(sNatW, sNatH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }

            if (sp.rotate) {
                QTransform t;
                t.rotate(90.0);
                packed = packed.transformed(t, Qt::SmoothTransformation);
            }

            painter.drawImage(qRound(sp.x * scale), qRound(sp.y * scale), packed);
            log << QStringLiteral("  [OK] %1").arg(sp.name);
            ++merged;
        }
        painter.end();

        results.append({ texPath, canvas, merged, missing });
    }

    if (results.isEmpty()) {
        result.error = QStringLiteral("No usable pages.");
        return result;
    }

    const QFileInfo atlasInfo(atlasPath);
    const QString fileTag = tag.isEmpty() ? QStringLiteral("_1x") : tag;

    if (mergeIntoOne && results.size() > 1) {
        int totalW = 0, totalH = 0;
        for (const auto& r : results) { totalW += r.canvas.width(); totalH = qMax(totalH, r.canvas.height()); }

        QImage combined(totalW, totalH, QImage::Format_ARGB32);
        combined.fill(Qt::transparent);
        QList<int> xOffsets;
        {
            QPainter p(&combined);
            int xOff = 0;
            for (const auto& r : results) {
                xOffsets.append(xOff);
                p.drawImage(xOff, 0, r.canvas);
                xOff += r.canvas.width();
            }
        }

        const QString combinedPath = atlasDir.absoluteFilePath(
            atlasInfo.baseName() + QStringLiteral("_combined") + fileTag + QStringLiteral(".png"));

        const QString savePath = resolveSavePath(combinedPath, conflict, log);
        if (savePath.isEmpty()) {
            log << QStringLiteral("[Skip] Skipping merge save");
        } else {
            int tm = 0, tmiss = 0;
            for (const auto& r : results) { tm += r.merged; tmiss += r.missing; }

            if (!combined.save(savePath, "PNG")) {
                log << QStringLiteral("[Error] Cannot save: %1").arg(savePath);
            } else {
                log << QStringLiteral("[Saved] %1  %2x%3  (merged=%4, missing=%5)")
                           .arg(savePath).arg(totalW).arg(totalH).arg(tm).arg(tmiss);
                result.savedFiles << savePath;
                result.merged  += tm;
                result.missing += tmiss;
            }

            const QString atlasOut = atlasDir.absoluteFilePath(
                atlasInfo.baseName() + QStringLiteral("_combined") + fileTag + QStringLiteral(".atlas"));
            saveAtlasFile(atlasOut, pages, scale, fileTag, log, xOffsets);
        }
    } else {
        for (const auto& r : results) {
            const QFileInfo fi(r.texPath);
            const QString scaledPath = fi.absolutePath() + QDir::separator() +
                fi.baseName() + fileTag + QStringLiteral(".") + fi.suffix();

            const QString savePath = resolveSavePath(scaledPath, conflict, log);
            if (savePath.isEmpty()) {
                log << QStringLiteral("  [Skip] Skipping %1").arg(fi.fileName());
                continue;
            }
            if (!r.canvas.save(savePath, "PNG")) {
                log << QStringLiteral("[Error] Cannot save: %1").arg(savePath);
            } else {
                log << QStringLiteral("[Saved] %1  (merged=%2, missing=%3)")
                           .arg(savePath).arg(r.merged).arg(r.missing);
                result.savedFiles << savePath;
                result.merged  += r.merged;
                result.missing += r.missing;
            }
        }

        const QString atlasOut = atlasDir.absoluteFilePath(
            atlasInfo.baseName() + fileTag + QStringLiteral(".atlas"));
        saveAtlasFile(atlasOut, pages, scale, fileTag, log);
    }

    result.ok = true;
    return result;
}

} // namespace AtlasOps
