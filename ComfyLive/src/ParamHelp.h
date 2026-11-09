#pragma once
#include <QString>

// What a graph parameter does to the result, in the window's language (Language::current()): shown when the mouse
// rests on a parameter, and in the Help list (the "?" button by the graph). A parameter this table does not know gets
// ComfyUI's own tooltip (English), when the server has one.
namespace ParamHelp {

// classType: the node's class ("KSampler", "TextEncodeAceStepAudio1.5" ...); input: the input's name ("top_p").
// comfyTooltip: the server's tooltip for it, or "". Returns "" when nothing is known.
QString describe(const QString& classType, const QString& input, const QString& comfyTooltip);

}  // namespace ParamHelp
