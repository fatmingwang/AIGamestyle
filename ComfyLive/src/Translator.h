#pragma once
#include <QString>
#include <QStringList>

// The window's language: "en" (the source text) or "zh_TW" (Traditional Chinese, a table built into the exe, so there
// are no .qm files to ship). Every tr() in the window goes through it; a text missing from the table stays English.
//
// The headless protocol, the log lines from ComfyUI and the graph's own input names (seed, steps, ...) stay as they
// are: they are what scripts and ComfyUI use.
namespace Language {

QStringList codes();                          // {"en", "zh_TW"}
QString displayName(const QString& code);     // "English", "繁體中文"
QString systemDefault();                      // zh_TW on a Chinese Windows, else en
QString current();
void apply(const QString& code);              // installs (or removes) the translator; rebuild the window after

}  // namespace Language
