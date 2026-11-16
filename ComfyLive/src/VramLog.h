#pragma once
#include <QList>
#include <QString>

class Workflow;

// What each run cost on this PC (README.md "Size and memory"): the model, the size, the frames, the highest GPU memory
// in use while it ran (ComfyUI's /system_stats, read every second or so) and the time. Kept in ComfyLive's own folder
// (vram_records.json), written by the window and by headless runs alike. The window shows it as a short note when a
// size is changed, so the limit of the card is known before Generate, from runs that were really made.
namespace VramLog {

struct Record {
    QString model;        // the diffusion model's file name
    int width = 0;
    int height = 0;
    int frames = 1;       // 1: a picture
    double peakGB = 0;    // highest GPU memory in use during the run
    double totalGB = 0;   // the card
    double seconds = 0;
    bool failed = false;  // ComfyUI ended it with an error (out of memory, most often)
    QString when;
};

QString filePath();
QList<Record> load();
void add(const Record& r);   // replaces an older record of the same model, size and frames

// The size a graph would run at. False: the graph has no width and height of its own.
bool sizeOf(const Workflow& w, Record* out);

// The note for the size the graph has now: the size, the frames and seconds, what this size (or the nearest one that
// was run) cost, and the card's memory. usedNowGB < 0: not known.
QString describe(const Workflow& w, double totalGB, double usedNowGB);

}  // namespace VramLog
