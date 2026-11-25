#include "Workflow.h"

#include <QDateTime>
#include <QDir>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

QJsonObject node(const QString& cls, const QString& title, const QJsonObject& inputs)
{
    QJsonObject n;
    n[QStringLiteral("class_type")] = cls;
    n[QStringLiteral("inputs")] = inputs;
    n[QStringLiteral("_meta")] = QJsonObject{{QStringLiteral("title"), title}};
    return n;
}

QJsonArray link(const char* nodeId, int slot)
{
    return QJsonArray{QString::fromLatin1(nodeId), slot};
}

// The input's spec in /object_info: ["INT", {min, max, ...}], [[choices], {...}] or ["COMBO", {"options": [...]}].
QJsonArray specOf(const QJsonObject& objectInfo, const QString& cls, const QString& input)
{
    const QJsonObject in = objectInfo.value(cls).toObject().value(QStringLiteral("input")).toObject();
    for (const char* part : {"required", "optional"}) {
        const QJsonValue v = in.value(QLatin1String(part)).toObject().value(input);
        if (v.isArray())
            return v.toArray();
    }
    return {};
}

QStringList choicesOf(const QJsonArray& spec)
{
    QStringList out;
    QJsonArray list;
    if (!spec.isEmpty() && spec[0].isArray())
        list = spec[0].toArray();
    else if (!spec.isEmpty() && spec[0].toString() == QStringLiteral("COMBO"))
        list = spec.size() > 1 ? spec[1].toObject().value(QStringLiteral("options")).toArray() : QJsonArray();
    for (const QJsonValue& v : list)
        out << (v.isString() ? v.toString() : QString::number(v.toDouble()));
    return out;
}

bool isLink(const QJsonValue& v)
{
    return v.isArray() && v.toArray().size() == 2 && v.toArray()[0].isString();
}

}  // namespace

Workflow Workflow::textToImage()
{
    QJsonObject g;
    g[QStringLiteral("4")] = node(QStringLiteral("CheckpointLoaderSimple"), QStringLiteral("Model"),
                                  {{QStringLiteral("ckpt_name"), QStringLiteral("Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors")}});
    g[QStringLiteral("6")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Positive prompt"),
                                  {{QStringLiteral("text"), QStringLiteral("a brave young archer in a blue tunic, fantasy RPG character, full body, "
                                                                           "game art, clean lines, soft lighting, plain background")},
                                   {QStringLiteral("clip"), link("4", 1)}});
    g[QStringLiteral("7")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Negative prompt"),
                                  {{QStringLiteral("text"), QStringLiteral("blurry, low quality, extra limbs, text, watermark")},
                                   {QStringLiteral("clip"), link("4", 1)}});
    g[QStringLiteral("5")] = node(QStringLiteral("EmptyLatentImage"), QStringLiteral("Size"),
                                  {{QStringLiteral("width"), 1024}, {QStringLiteral("height"), 1024}, {QStringLiteral("batch_size"), 1}});
    g[QStringLiteral("3")] = node(QStringLiteral("KSampler"), QStringLiteral("Sampler"),
                                  {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 20}, {QStringLiteral("cfg"), 6.0},
                                   {QStringLiteral("sampler_name"), QStringLiteral("dpmpp_2m")}, {QStringLiteral("scheduler"), QStringLiteral("karras")},
                                   {QStringLiteral("denoise"), 1.0}, {QStringLiteral("model"), link("4", 0)},
                                   {QStringLiteral("positive"), link("6", 0)}, {QStringLiteral("negative"), link("7", 0)},
                                   {QStringLiteral("latent_image"), link("5", 0)}});
    g[QStringLiteral("8")] = node(QStringLiteral("VAEDecode"), QStringLiteral("Decode"),
                                  {{QStringLiteral("samples"), link("3", 0)}, {QStringLiteral("vae"), link("4", 2)}});
    g[QStringLiteral("9")] = node(QStringLiteral("SaveImage"), QStringLiteral("Save"),
                                  {{QStringLiteral("filename_prefix"), QStringLiteral("ComfyLive")}, {QStringLiteral("images"), link("8", 0)}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to image (built-in)");
    return w;
}

Workflow Workflow::textToMusic()
{
    // The same graph as ComfyUI's "ACE-Step 1.5 checkpoint" template (audio_ace_step_1_5_checkpoint.json), saving MP3
    // (Windows plays it without extra codecs). The model: ace_step_1.5_turbo_aio.safetensors in checkpoints/ (README.md).
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("CheckpointLoaderSimple"), QStringLiteral("Model"),
                                  {{QStringLiteral("ckpt_name"), QStringLiteral("ace_step_1.5_turbo_aio.safetensors")}});
    g[QStringLiteral("2")] = node(QStringLiteral("ModelSamplingAuraFlow"), QStringLiteral("Model shift"),
                                  {{QStringLiteral("shift"), 3.0}, {QStringLiteral("model"), link("1", 0)}});
    g[QStringLiteral("3")] = node(QStringLiteral("TextEncodeAceStepAudio1.5"), QStringLiteral("Song"),
                                  {{QStringLiteral("tags"), QStringLiteral("smooth quiet storm R&B, 1970s Philly soul ballad, high smooth male tenor, "
                                                                           "soft falsetto, breathy intimate delivery, lush strings, soft electric piano, "
                                                                           "gentle bass, subtle drums, warm polished production")},
                                   {QStringLiteral("lyrics"), QStringLiteral("[Verse]\nI woke up in a tower made of stone\nNo name, no road, no way back home\n"
                                                                             "Every door I open shows a face I almost know\n\n"
                                                                             "[Chorus]\nTell me who I was before the night\nGive me back the memory, give me back the light\n"
                                                                             "Floor by floor I climb to find the man I left behind\n\n"
                                                                             "[Outro]\nFloor by floor, I am coming home")},
                                   {QStringLiteral("seed"), 1}, {QStringLiteral("bpm"), 72}, {QStringLiteral("duration"), 60.0},
                                   {QStringLiteral("timesignature"), QStringLiteral("4")}, {QStringLiteral("language"), QStringLiteral("en")},
                                   {QStringLiteral("keyscale"), QStringLiteral("D minor")}, {QStringLiteral("generate_audio_codes"), true},
                                   {QStringLiteral("cfg_scale"), 2.0}, {QStringLiteral("temperature"), 0.85}, {QStringLiteral("top_p"), 0.9},
                                   {QStringLiteral("top_k"), 0}, {QStringLiteral("min_p"), 0.0}, {QStringLiteral("clip"), link("1", 1)}});
    g[QStringLiteral("4")] = node(QStringLiteral("ConditioningZeroOut"), QStringLiteral("Negative"),
                                  {{QStringLiteral("conditioning"), link("3", 0)}});
    g[QStringLiteral("5")] = node(QStringLiteral("EmptyAceStep1.5LatentAudio"), QStringLiteral("Length"),
                                  {{QStringLiteral("seconds"), 60.0}, {QStringLiteral("batch_size"), 1}});
    g[QStringLiteral("6")] = node(QStringLiteral("KSampler"), QStringLiteral("Sampler"),
                                  {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 8}, {QStringLiteral("cfg"), 1.0},
                                   {QStringLiteral("sampler_name"), QStringLiteral("euler")}, {QStringLiteral("scheduler"), QStringLiteral("simple")},
                                   {QStringLiteral("denoise"), 1.0}, {QStringLiteral("model"), link("2", 0)},
                                   {QStringLiteral("positive"), link("3", 0)}, {QStringLiteral("negative"), link("4", 0)},
                                   {QStringLiteral("latent_image"), link("5", 0)}});
    g[QStringLiteral("7")] = node(QStringLiteral("VAEDecodeAudio"), QStringLiteral("Decode"),
                                  {{QStringLiteral("samples"), link("6", 0)}, {QStringLiteral("vae"), link("1", 2)}});
    g[QStringLiteral("8")] = node(QStringLiteral("SaveAudioMP3"), QStringLiteral("Save"),
                                  {{QStringLiteral("filename_prefix"), QStringLiteral("audio/ComfyLive")}, {QStringLiteral("quality"), QStringLiteral("V0")},
                                   {QStringLiteral("audio"), link("7", 0)}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to music, ACE-Step 1.5 (built-in)");
    return w;
}

Workflow Workflow::textToSoundEffect()
{
    // ComfyUI's Stable Audio Open 1.0 graph (its "stable audio" example), with the timing conditioning and saving MP3
    // (Windows plays it without extra codecs). The models: stable-audio-open-1.0.safetensors in checkpoints/ and
    // t5-base.safetensors in text_encoders/ (README.md). The default: a dog barking.
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("CheckpointLoaderSimple"), QStringLiteral("Model"),
                                  {{QStringLiteral("ckpt_name"), QStringLiteral("stable-audio-open-1.0.safetensors")}});
    g[QStringLiteral("2")] = node(QStringLiteral("CLIPLoader"), QStringLiteral("Text encoder"),
                                  {{QStringLiteral("clip_name"), QStringLiteral("t5-base.safetensors")},
                                   {QStringLiteral("type"), QStringLiteral("stable_audio")}});
    g[QStringLiteral("3")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Sound"),
                                  {{QStringLiteral("text"), QStringLiteral("a medium-sized dog barking twice, loud and close, outdoors, "
                                                                           "clean field recording, sound effect")},
                                   {QStringLiteral("clip"), link("2", 0)}});
    g[QStringLiteral("4")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Avoid"),
                                  {{QStringLiteral("text"), QStringLiteral("music, speech, human voice, background noise, hum, low quality, distorted")},
                                   {QStringLiteral("clip"), link("2", 0)}});
    g[QStringLiteral("5")] = node(QStringLiteral("ConditioningStableAudio"), QStringLiteral("Timing"),
                                  {{QStringLiteral("seconds_start"), 0.0}, {QStringLiteral("seconds_total"), 3.0},
                                   {QStringLiteral("positive"), link("3", 0)}, {QStringLiteral("negative"), link("4", 0)}});
    g[QStringLiteral("6")] = node(QStringLiteral("EmptyLatentAudio"), QStringLiteral("Length"),
                                  {{QStringLiteral("seconds"), 3.0}, {QStringLiteral("batch_size"), 1}});
    g[QStringLiteral("7")] = node(QStringLiteral("KSampler"), QStringLiteral("Sampler"),
                                  {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 50}, {QStringLiteral("cfg"), 5.0},
                                   {QStringLiteral("sampler_name"), QStringLiteral("dpmpp_3m_sde_gpu")},
                                   {QStringLiteral("scheduler"), QStringLiteral("exponential")}, {QStringLiteral("denoise"), 1.0},
                                   {QStringLiteral("model"), link("1", 0)}, {QStringLiteral("positive"), link("5", 0)},
                                   {QStringLiteral("negative"), link("5", 1)}, {QStringLiteral("latent_image"), link("6", 0)}});
    g[QStringLiteral("8")] = node(QStringLiteral("VAEDecodeAudio"), QStringLiteral("Decode"),
                                  {{QStringLiteral("samples"), link("7", 0)}, {QStringLiteral("vae"), link("1", 2)}});
    g[QStringLiteral("9")] = node(QStringLiteral("SaveAudioMP3"), QStringLiteral("Save"),
                                  {{QStringLiteral("filename_prefix"), QStringLiteral("audio/ComfyLive_sfx")}, {QStringLiteral("quality"), QStringLiteral("V0")},
                                   {QStringLiteral("audio"), link("8", 0)}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to sound effect, Stable Audio Open (built-in)");
    return w;
}

namespace {

// The Qwen3-TTS node's sampling inputs, the same on both speech nodes (ComfyUI-Qwen-TTS's own defaults).
QJsonObject qwenSampling(QJsonObject in)
{
    in[QStringLiteral("model_choice")] = QStringLiteral("1.7B");
    in[QStringLiteral("device")] = QStringLiteral("auto");
    in[QStringLiteral("precision")] = QStringLiteral("bf16");
    in[QStringLiteral("seed")] = 1;
    in[QStringLiteral("max_new_tokens")] = 2048;
    in[QStringLiteral("top_p")] = 1.0;
    in[QStringLiteral("top_k")] = 50;
    in[QStringLiteral("temperature")] = 0.9;
    in[QStringLiteral("repetition_penalty")] = 1.05;
    in[QStringLiteral("attention")] = QStringLiteral("auto");
    // on: the speech model leaves the GPU after each run (ComfyUI's /free cannot reach it), so the main ComfyUI's
    // picture and music models have the 12 GB again
    in[QStringLiteral("unload_model_after_generate")] = true;
    return in;
}

QJsonObject saveMp3(const char* prefix, const char* from)
{
    return node(QStringLiteral("SaveAudioMP3"), QStringLiteral("Save"),
                {{QStringLiteral("filename_prefix"), QString::fromLatin1(prefix)}, {QStringLiteral("quality"), QStringLiteral("V0")},
                 {QStringLiteral("audio"), link(from, 0)}});
}

}  // namespace

Workflow Workflow::textToSpeech()
{
    // ComfyUI-Qwen-TTS's CustomVoice node (custom_nodes/ComfyUI-Qwen-TTS; models in models/qwen-tts, README.md): one of
    // 9 preset speakers says the text in the chosen language; "instruct" adds a tone ("angry", "whispering").
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("FB_Qwen3TTSCustomVoice"), QStringLiteral("Speech"),
                                  qwenSampling({{QStringLiteral("text"), QStringLiteral("The gate of the tower is open. Climb, young hero, "
                                                                                        "and do not look back.")},
                                                {QStringLiteral("speaker"), QStringLiteral("Ryan")},
                                                {QStringLiteral("language"), QStringLiteral("English")},
                                                {QStringLiteral("instruct"), QString()}}));
    g[QStringLiteral("2")] = saveMp3("audio/ComfyLive_speech", "1");
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to speech, Qwen3-TTS preset voice (built-in)");
    return w;
}

Workflow Workflow::textToSpeechDesigned()
{
    // ComfyUI-Qwen-TTS's VoiceDesign node: the voice described in words ("instruct") instead of a preset speaker.
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("FB_Qwen3TTSVoiceDesign"), QStringLiteral("Speech"),
                                  qwenSampling({{QStringLiteral("text"), QStringLiteral("So, another climber. Many have come before you, "
                                                                                        "and the tower remembers every one of them.")},
                                                {QStringLiteral("instruct"), QStringLiteral("An old male wizard, deep and slightly raspy voice, "
                                                                                            "speaking slowly, calm and a little mysterious.")},
                                                {QStringLiteral("language"), QStringLiteral("English")}}));
    g[QStringLiteral("2")] = saveMp3("audio/ComfyLive_voice", "1");
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to speech, Qwen3-TTS voice from a description (built-in)");
    return w;
}

QString Workflow::videoPromptSuffix()
{
    return QStringLiteral("one single character alone, full body shot, the character is large and tall in the picture, "
                          "the body fills most of the height of the frame from head to feet with a little room around it, "
                          "side view, static camera, locked-off shot, plain flat light grey studio background, "
                          "even soft lighting, sharp and clear, no motion blur");
}

Workflow Workflow::textToVideo()
{
    // ComfyUI's "Wan 2.2 TI2V-5B" template (video_wan2_2_5B_ti2v), ending in SaveImage: the frames come back as PNG
    // pictures, which ComfyLive numbers and makes an MP4 of (VideoTools). The models (README.md "Video"):
    // diffusion_models/wan2.2_ti2v_5B_fp16.safetensors, text_encoders/umt5_xxl_fp8_e4m3fn_scaled.safetensors,
    // vae/wan2.2_vae.safetensors. 24 frames a second; length is 4n+1 frames (73: 3 s).
    QJsonObject g;
    g[QStringLiteral("37")] = node(QStringLiteral("UNETLoader"), QStringLiteral("Model"),
                                   {{QStringLiteral("unet_name"), QStringLiteral("wan2.2_ti2v_5B_fp16.safetensors")},
                                    {QStringLiteral("weight_dtype"), QStringLiteral("default")}});
    g[QStringLiteral("38")] = node(QStringLiteral("CLIPLoader"), QStringLiteral("Text encoder"),
                                   {{QStringLiteral("clip_name"), QStringLiteral("umt5_xxl_fp8_e4m3fn_scaled.safetensors")},
                                    {QStringLiteral("type"), QStringLiteral("wan")}, {QStringLiteral("device"), QStringLiteral("default")}});
    g[QStringLiteral("39")] = node(QStringLiteral("VAELoader"), QStringLiteral("VAE"),
                                   {{QStringLiteral("vae_name"), QStringLiteral("wan2.2_vae.safetensors")}});
    g[QStringLiteral("48")] = node(QStringLiteral("ModelSamplingSD3"), QStringLiteral("Model shift"),
                                   {{QStringLiteral("shift"), 8.0}, {QStringLiteral("model"), link("37", 0)}});
    g[QStringLiteral("6")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Video"),
                                  {{QStringLiteral("text"), QStringLiteral("a young archer in a blue tunic and a brown cape stands ready, then "
                                                                           "draws a wooden bow and releases an arrow to the right, "
                                                                           "fantasy game character, ") + videoPromptSuffix()},
                                   {QStringLiteral("clip"), link("38", 0)}});
    g[QStringLiteral("7")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Avoid"),
                                  {{QStringLiteral("text"), QStringLiteral("blurry, motion blur, camera movement, zoom, pan, cuts, several characters, "
                                                                           "crowd, cropped body, close-up, busy background, scenery, text, subtitles, "
                                                                           "watermark, extra limbs, deformed hands, low quality, still picture")},
                                   {QStringLiteral("clip"), link("38", 0)}});
    g[QStringLiteral("55")] = node(QStringLiteral("Wan22ImageToVideoLatent"), QStringLiteral("Size and length"),
                                   {{QStringLiteral("width"), 1280}, {QStringLiteral("height"), 704}, {QStringLiteral("length"), 73},
                                    {QStringLiteral("batch_size"), 1}, {QStringLiteral("vae"), link("39", 0)}});
    g[QStringLiteral("3")] = node(QStringLiteral("KSampler"), QStringLiteral("Sampler"),
                                  {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 20}, {QStringLiteral("cfg"), 5.0},
                                   {QStringLiteral("sampler_name"), QStringLiteral("uni_pc")}, {QStringLiteral("scheduler"), QStringLiteral("simple")},
                                   {QStringLiteral("denoise"), 1.0}, {QStringLiteral("model"), link("48", 0)},
                                   {QStringLiteral("positive"), link("6", 0)}, {QStringLiteral("negative"), link("7", 0)},
                                   {QStringLiteral("latent_image"), link("55", 0)}});
    g[QStringLiteral("8")] = node(QStringLiteral("VAEDecode"), QStringLiteral("Decode"),
                                  {{QStringLiteral("samples"), link("3", 0)}, {QStringLiteral("vae"), link("39", 0)}});
    g[QStringLiteral("30")] = node(QStringLiteral("InspyrenetRembg"), QStringLiteral("Remove background"),
                                   {{QStringLiteral("torchscript_jit"), QStringLiteral("default")}, {QStringLiteral("image"), link("8", 0)}});
    g[QStringLiteral("9")] = node(QStringLiteral("SaveImage"), QStringLiteral("Save"),
                                  {{QStringLiteral("filename_prefix"), QStringLiteral("video/ComfyLive_video")}, {QStringLiteral("images"), link("30", 0)}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to video, Wan 2.2 TI2V-5B (built-in)");
    return w;
}

Workflow Workflow::textToVideoLtx()
{
    // LTX-2 19B distilled (Lightricks): the picture and its sound made together, in 8 steps without CFG, at 768 x 512.
    // Unlike the other video graphs it ends in SaveVideo: ComfyUI writes the MP4 with its soundtrack, and ComfyLive
    // downloads that file (no frames, no ffmpeg here). Built from ComfyUI's own LTX nodes; tried on a 16 GB card: 4 s in
    // about 70-90 s, 15.5 GB at the peak. 25 frames a second (the model's rate: at 24 the sound came out almost
    // silent); length is 8n+1 frames (97: 3.9 s). The models: checkpoints/ltx-2-19b-distilled.safetensors (video and
    // audio VAE inside) and text_encoders/gemma_3_12B_it.safetensors.
    const QString ckpt = QStringLiteral("ltx-2-19b-distilled.safetensors");
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("CheckpointLoaderSimple"), QStringLiteral("Model"), {{QStringLiteral("ckpt_name"), ckpt}});
    g[QStringLiteral("2")] = node(QStringLiteral("LTXAVTextEncoderLoader"), QStringLiteral("Text encoder"),
                                  {{QStringLiteral("text_encoder"), QStringLiteral("gemma_3_12B_it.safetensors")},
                                   {QStringLiteral("ckpt_name"), ckpt}, {QStringLiteral("device"), QStringLiteral("default")}});
    g[QStringLiteral("3")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Video and sound"),
                                  {{QStringLiteral("text"), QStringLiteral("A small brown puppy sits on a wooden porch on a sunny afternoon, looks at the "
                                                                           "camera and barks loudly twice: \"Woof! Woof!\", its tail wagging. Clear loud "
                                                                           "barking sounds, birds chirp in the background. Static camera.")},
                                   {QStringLiteral("clip"), link("2", 0)}});
    g[QStringLiteral("4")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Avoid"),
                                  {{QStringLiteral("text"), QStringLiteral("blurry, low quality, distorted, watermark, text, subtitles")},
                                   {QStringLiteral("clip"), link("2", 0)}});
    g[QStringLiteral("5")] = node(QStringLiteral("LTXVConditioning"), QStringLiteral("Frame rate"),
                                  {{QStringLiteral("positive"), link("3", 0)}, {QStringLiteral("negative"), link("4", 0)},
                                   {QStringLiteral("frame_rate"), 25.0}});
    g[QStringLiteral("6")] = node(QStringLiteral("EmptyLTXVLatentVideo"), QStringLiteral("Size and length"),
                                  {{QStringLiteral("width"), 768}, {QStringLiteral("height"), 512}, {QStringLiteral("length"), 97},
                                   {QStringLiteral("batch_size"), 1}});
    g[QStringLiteral("7")] = node(QStringLiteral("LTXVAudioVAELoader"), QStringLiteral("Audio VAE"), {{QStringLiteral("ckpt_name"), ckpt}});
    g[QStringLiteral("8")] = node(QStringLiteral("LTXVEmptyLatentAudio"), QStringLiteral("Sound length"),
                                  {{QStringLiteral("frames_number"), 97}, {QStringLiteral("frame_rate"), 25},
                                   {QStringLiteral("batch_size"), 1}, {QStringLiteral("audio_vae"), link("7", 0)}});
    g[QStringLiteral("9")] = node(QStringLiteral("LTXVConcatAVLatent"), QStringLiteral("Picture + sound"),
                                  {{QStringLiteral("video_latent"), link("6", 0)}, {QStringLiteral("audio_latent"), link("8", 0)}});
    g[QStringLiteral("10")] = node(QStringLiteral("RandomNoise"), QStringLiteral("Seed"), {{QStringLiteral("noise_seed"), 1}});
    g[QStringLiteral("11")] = node(QStringLiteral("CFGGuider"), QStringLiteral("Guider"),
                                   {{QStringLiteral("model"), link("1", 0)}, {QStringLiteral("positive"), link("5", 0)},
                                    {QStringLiteral("negative"), link("5", 1)}, {QStringLiteral("cfg"), 1.0}});
    g[QStringLiteral("12")] = node(QStringLiteral("KSamplerSelect"), QStringLiteral("Sampler"), {{QStringLiteral("sampler_name"), QStringLiteral("euler")}});
    g[QStringLiteral("13")] = node(QStringLiteral("ManualSigmas"), QStringLiteral("Steps (distilled)"),
                                   {{QStringLiteral("sigmas"), QStringLiteral("1.0, 0.99375, 0.9875, 0.98125, 0.975, 0.909375, 0.725, 0.421875, 0.0")}});
    g[QStringLiteral("14")] = node(QStringLiteral("SamplerCustomAdvanced"), QStringLiteral("Sample"),
                                   {{QStringLiteral("noise"), link("10", 0)}, {QStringLiteral("guider"), link("11", 0)},
                                    {QStringLiteral("sampler"), link("12", 0)}, {QStringLiteral("sigmas"), link("13", 0)},
                                    {QStringLiteral("latent_image"), link("9", 0)}});
    g[QStringLiteral("15")] = node(QStringLiteral("LTXVSeparateAVLatent"), QStringLiteral("Split"), {{QStringLiteral("av_latent"), link("14", 0)}});
    g[QStringLiteral("16")] = node(QStringLiteral("VAEDecodeTiled"), QStringLiteral("Decode pictures"),
                                   {{QStringLiteral("samples"), link("15", 0)}, {QStringLiteral("vae"), link("1", 2)},
                                    {QStringLiteral("tile_size"), 512}, {QStringLiteral("overlap"), 64},
                                    {QStringLiteral("temporal_size"), 64}, {QStringLiteral("temporal_overlap"), 8}});
    g[QStringLiteral("17")] = node(QStringLiteral("LTXVAudioVAEDecode"), QStringLiteral("Decode sound"),
                                   {{QStringLiteral("samples"), link("15", 1)}, {QStringLiteral("audio_vae"), link("7", 0)}});
    g[QStringLiteral("18")] = node(QStringLiteral("CreateVideo"), QStringLiteral("Video"),
                                   {{QStringLiteral("images"), link("16", 0)}, {QStringLiteral("fps"), 25.0}, {QStringLiteral("audio"), link("17", 0)}});
    g[QStringLiteral("19")] = node(QStringLiteral("SaveVideo"), QStringLiteral("Save"),
                                   {{QStringLiteral("video"), link("18", 0)}, {QStringLiteral("filename_prefix"), QStringLiteral("video/ComfyLive_ltx")},
                                    {QStringLiteral("format"), QStringLiteral("auto")}, {QStringLiteral("format.codec"), QStringLiteral("auto")}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to video with sound, LTX-2 19B distilled (built-in)");
    return w;
}

Workflow Workflow::textToVideoHyperFlow()
{
    // ComfyUI-HyperFlow-Flash's own text-to-video graph (example_workflows/api/hyperflow_t2v_api.json), with the Q3_K_M
    // file and ending in SaveImage: the frames come back as PNG pictures, like the Wan graph's. The model also makes a
    // soundtrack; it is not decoded here (an animation's frames are what is wanted). No negative prompt: it samples
    // without CFG, in 4, 6 or 8 steps. length is 17n+5 frames at 24 a second (124: 5.2 s, the shortest it was trained on).
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("HyperFlowLoader"), QStringLiteral("Model"),
                                  {{QStringLiteral("unet_name"), QStringLiteral("minimax_h3_fl2va_hyperflow_flash_14b-Q4_K_M.gguf")},
                                   {QStringLiteral("weights_in"), QStringLiteral("auto")}});
    g[QStringLiteral("2")] = node(QStringLiteral("CLIPLoader"), QStringLiteral("Text encoder"),
                                  {{QStringLiteral("clip_name"), QStringLiteral("qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors")},
                                   {QStringLiteral("type"), QStringLiteral("minimax")}, {QStringLiteral("device"), QStringLiteral("default")}});
    g[QStringLiteral("3")] = node(QStringLiteral("VAELoader"), QStringLiteral("VAE"),
                                  {{QStringLiteral("vae_name"), QStringLiteral("minimax_h3_video_vae_fp16.safetensors")}});
    g[QStringLiteral("5")] = node(QStringLiteral("MiniMaxH3ImageToVideo"), QStringLiteral("Video"),
                                  {{QStringLiteral("prompt"), QStringLiteral("a young archer in a blue tunic and a brown cape stands ready, then "
                                                                             "draws a wooden bow and releases an arrow to the right, "
                                                                             "fantasy game character, ") + videoPromptSuffix()},
                                   {QStringLiteral("width"), 832}, {QStringLiteral("height"), 480}, {QStringLiteral("length"), 73},
                                   {QStringLiteral("clip"), link("2", 0)}, {QStringLiteral("vae"), link("3", 0)}});
    g[QStringLiteral("6")] = node(QStringLiteral("HyperFlowSigmas"), QStringLiteral("Steps"),
                                  {{QStringLiteral("steps"), 6}, {QStringLiteral("model"), link("1", 0)}});
    g[QStringLiteral("7")] = node(QStringLiteral("KSamplerSelect"), QStringLiteral("Sampler"),
                                  {{QStringLiteral("sampler_name"), QStringLiteral("euler")}});
    g[QStringLiteral("8")] = node(QStringLiteral("RandomNoise"), QStringLiteral("Seed"), {{QStringLiteral("noise_seed"), 1}});
    g[QStringLiteral("9")] = node(QStringLiteral("BasicGuider"), QStringLiteral("Guider"),
                                  {{QStringLiteral("model"), link("1", 0)}, {QStringLiteral("conditioning"), link("5", 0)}});
    g[QStringLiteral("10")] = node(QStringLiteral("SamplerCustomAdvanced"), QStringLiteral("Sample"),
                                   {{QStringLiteral("noise"), link("8", 0)}, {QStringLiteral("guider"), link("9", 0)},
                                    {QStringLiteral("sampler"), link("7", 0)}, {QStringLiteral("sigmas"), link("6", 0)},
                                    {QStringLiteral("latent_image"), link("5", 1)}});
    g[QStringLiteral("11")] = node(QStringLiteral("VAEDecode"), QStringLiteral("Decode"),
                                   {{QStringLiteral("samples"), link("10", 0)}, {QStringLiteral("vae"), link("3", 0)}});
    g[QStringLiteral("30")] = node(QStringLiteral("InspyrenetRembg"), QStringLiteral("Remove background"),
                                   {{QStringLiteral("torchscript_jit"), QStringLiteral("default")}, {QStringLiteral("image"), link("11", 0)}});
    g[QStringLiteral("12")] = node(QStringLiteral("SaveImage"), QStringLiteral("Save"),
                                   {{QStringLiteral("filename_prefix"), QStringLiteral("video/ComfyLive_h3")}, {QStringLiteral("images"), link("30", 0)}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to video, HyperFlow H3 Flash (built-in)");
    return w;
}

namespace {
// Qwen Image 2.1 Turbo, as ComfyUI's own template (image_qwen_image_2_1_turbo_t2i): the INT8 model and text encoder,
// the model's cache, and the exact eight-step schedule (nine sigmas) through SamplerCustomAdvanced at CFG 1. A plain
// KSampler with 8 steps does not give this schedule.
QJsonObject qwenTurboBase(const QString& prompt, const QJsonArray& latent)
{
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("UNETLoader"), QStringLiteral("Model"),
                                  {{QStringLiteral("unet_name"), QStringLiteral("qwen-image-2.1-turbo-int8_convrot.safetensors")},
                                   {QStringLiteral("weight_dtype"), QStringLiteral("default")}});
    g[QStringLiteral("2")] = node(QStringLiteral("CLIPLoader"), QStringLiteral("Text encoder"),
                                  {{QStringLiteral("clip_name"), QStringLiteral("qwen3vl_8b_int8_convrot.safetensors")},
                                   {QStringLiteral("type"), QStringLiteral("qwen_image")}, {QStringLiteral("device"), QStringLiteral("default")}});
    g[QStringLiteral("3")] = node(QStringLiteral("VAELoader"), QStringLiteral("VAE"),
                                  {{QStringLiteral("vae_name"), QStringLiteral("qwen_image_2.1_vae_bf16.safetensors")}});
    g[QStringLiteral("4")] = node(QStringLiteral("QwenImage21Cache"), QStringLiteral("Cache"),
                                  {{QStringLiteral("device"), QStringLiteral("auto")}, {QStringLiteral("dtype"), QStringLiteral("default")},
                                   {QStringLiteral("model"), link("1", 0)}});
    g[QStringLiteral("5")] = node(QStringLiteral("TextEncodeQwenImage21"), QStringLiteral("Picture"),
                                  {{QStringLiteral("prompt"), prompt}, {QStringLiteral("negative_prompt"), QString()},
                                   {QStringLiteral("resolution"), 1024}, {QStringLiteral("clip"), link("2", 0)}});
    g[QStringLiteral("7")] = node(QStringLiteral("ManualSigmas"), QStringLiteral("Steps"),
                                  {{QStringLiteral("sigmas"), QStringLiteral("1.0, 0.978453, 0.95418, 0.926626, 0.89508, 0.845148, 0.704534, 0.414568, 0")}});
    g[QStringLiteral("8")] = node(QStringLiteral("KSamplerSelect"), QStringLiteral("Sampler"), {{QStringLiteral("sampler_name"), QStringLiteral("euler")}});
    g[QStringLiteral("9")] = node(QStringLiteral("RandomNoise"), QStringLiteral("Seed"), {{QStringLiteral("noise_seed"), 1}});
    g[QStringLiteral("10")] = node(QStringLiteral("CFGGuider"), QStringLiteral("Guider"),
                                   {{QStringLiteral("cfg"), 1.0}, {QStringLiteral("model"), link("4", 0)},
                                    {QStringLiteral("positive"), link("5", 0)}, {QStringLiteral("negative"), link("5", 1)}});
    g[QStringLiteral("11")] = node(QStringLiteral("SamplerCustomAdvanced"), QStringLiteral("Sample"),
                                   {{QStringLiteral("noise"), link("9", 0)}, {QStringLiteral("guider"), link("10", 0)},
                                    {QStringLiteral("sampler"), link("8", 0)}, {QStringLiteral("sigmas"), link("7", 0)},
                                    {QStringLiteral("latent_image"), latent}});
    g[QStringLiteral("12")] = node(QStringLiteral("VAEDecode"), QStringLiteral("Decode"),
                                   {{QStringLiteral("samples"), link("11", 0)}, {QStringLiteral("vae"), link("3", 0)}});
    g[QStringLiteral("13")] = node(QStringLiteral("SaveImage"), QStringLiteral("Save"),
                                   {{QStringLiteral("filename_prefix"), QStringLiteral("ComfyLive_qwen")}, {QStringLiteral("images"), link("12", 0)}});
    return g;
}
const int kQwenPictures = 4;   // qwen-edit's picture boxes: nodes "21" .. "24"
}  // namespace

Workflow Workflow::qwenTextToImage()
{
    QJsonObject g = qwenTurboBase(QStringLiteral("a brave young archer with messy brown hair, a blue tunic and a brown leather cape, standing ready with a "
                                                 "wooden bow, side view, full body, fantasy game character art, clean lines, soft lighting, plain light grey "
                                                 "background"),
                                  link("6", 0));
    g[QStringLiteral("6")] = node(QStringLiteral("EmptyLatentImage"), QStringLiteral("Size"),
                                  {{QStringLiteral("width"), 1024}, {QStringLiteral("height"), 1024}, {QStringLiteral("batch_size"), 1}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to image, Qwen Image 2.1 Turbo (built-in)");
    return w;
}

Workflow Workflow::qwenEditImages()
{
    // the text encoder sees the pictures and gives the latent: the result has picture 1's shape, at about
    // resolution x resolution pixels
    QJsonObject g = qwenTurboBase(QStringLiteral("Keep the character in <image1> unchanged: the same face, hair, clothes and colours. Show the character "
                                                 "from the side, standing ready and holding a wooden bow, full body, plain light grey background"),
                                  link("5", 2));
    QJsonObject enc = g.value(QStringLiteral("5")).toObject();
    QJsonObject in = enc.value(QStringLiteral("inputs")).toObject();
    in[QStringLiteral("vae")] = link("3", 0);
    for (int i = 1; i <= kQwenPictures; ++i) {
        const QString id = QString::number(20 + i);
        g[id] = node(QStringLiteral("LoadImage"), i == 1 ? QStringLiteral("Picture 1") : QStringLiteral("Picture %1 (optional)").arg(i),
                     {{QStringLiteral("image"), QString()}});
        in[QStringLiteral("images.image_%1").arg(i)] = QJsonArray{id, 0};
    }
    enc[QStringLiteral("inputs")] = in;
    enc[QStringLiteral("_meta")] = QJsonObject{{QStringLiteral("title"), QStringLiteral("Edit")}};
    g[QStringLiteral("5")] = enc;
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Edit pictures, Qwen Image 2.1 Turbo (built-in)");
    return w;
}

namespace {
// a Turbo graph turned into the full model's: the GGUF loader, and one KSampler in place of the fixed eight steps
Workflow qwenFull(Workflow w, const QString& name)
{
    QJsonObject g = w.graph();
    const QJsonValue latent = g.value(QStringLiteral("11")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("latent_image"));
    for (const char* id : {"7", "8", "9", "10", "11"})
        g.remove(QLatin1String(id));
    g[QStringLiteral("1")] = node(QStringLiteral("UnetLoaderGGUF"), QStringLiteral("Model"),
                                  {{QStringLiteral("unet_name"), QStringLiteral("qwen-image-2.1-Q4_K_M.gguf")}});
    g[QStringLiteral("11")] = node(QStringLiteral("KSampler"), QStringLiteral("Sampler"),
                                   {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 25}, {QStringLiteral("cfg"), 1.0},
                                    {QStringLiteral("sampler_name"), QStringLiteral("euler")}, {QStringLiteral("scheduler"), QStringLiteral("simple")},
                                    {QStringLiteral("denoise"), 1.0}, {QStringLiteral("model"), link("4", 0)},
                                    {QStringLiteral("positive"), link("5", 0)}, {QStringLiteral("negative"), link("5", 1)},
                                    {QStringLiteral("latent_image"), latent}});
    QJsonObject save = g.value(QStringLiteral("13")).toObject();
    QJsonObject in = save.value(QStringLiteral("inputs")).toObject();
    in[QStringLiteral("filename_prefix")] = QStringLiteral("ComfyLive_qwen_full");
    save[QStringLiteral("inputs")] = in;
    g[QStringLiteral("13")] = save;
    w.fromJson(g, nullptr);
    w.setName(name);
    return w;
}
}  // namespace

namespace {
Workflow qwenTurboGguf(Workflow w, const QString& name)
{
    QJsonObject g = w.graph();
    g[QStringLiteral("1")] = node(QStringLiteral("UnetLoaderGGUF"), QStringLiteral("Model"),
                                  {{QStringLiteral("unet_name"), QStringLiteral("qwen-image-2.1-turbo-Q6_K.gguf")}});
    w.fromJson(g, nullptr);
    w.setName(name);
    return w;
}
}  // namespace

Workflow Workflow::qwenGgufTextToImage()
{
    return qwenTurboGguf(qwenTextToImage(), QStringLiteral("Text to image, Qwen Image 2.1 Turbo Q6_K (built-in)"));
}

Workflow Workflow::qwenGgufEditImages()
{
    return qwenTurboGguf(qwenEditImages(), QStringLiteral("Edit pictures, Qwen Image 2.1 Turbo Q6_K (built-in)"));
}

Workflow Workflow::qwenFullTextToImage()
{
    return qwenFull(qwenTextToImage(), QStringLiteral("Text to image, Qwen Image 2.1 Q4_K_M (built-in)"));
}

Workflow Workflow::qwenFullEditImages()
{
    return qwenFull(qwenEditImages(), QStringLiteral("Edit pictures, Qwen Image 2.1 Q4_K_M (built-in)"));
}

bool Workflow::isLocalPicture(const QString& nodeId) const
{
    const QJsonObject n = graph_.value(nodeId).toObject();
    if (n.value(QStringLiteral("class_type")).toString() != QStringLiteral("LoadImage"))
        return false;
    const QString title = n.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("title")).toString();
    return title.startsWith(QStringLiteral("Reference picture")) || title.startsWith(QStringLiteral("Picture "));
}

QStringList Workflow::localPictureNodes() const
{
    QStringList ids;
    for (auto it = graph_.begin(); it != graph_.end(); ++it)
        if (isLocalPicture(it.key()))
            ids << it.key();
    std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b) { return a.toInt() < b.toInt(); });
    return ids;
}

bool Workflow::makesAudio() const
{
    for (auto it = graph_.begin(); it != graph_.end(); ++it)
        if (it.value().toObject().value(QStringLiteral("class_type")).toString().contains(QStringLiteral("SaveAudio")))
            return true;
    return false;
}

QJsonObject Workflow::graphForRun() const
{
    QJsonObject g = graph_;
    for (auto it = graph_.begin(); it != graph_.end(); ++it) {
        QJsonObject n = it.value().toObject();
        QJsonObject inputs = n.value(QStringLiteral("inputs")).toObject();
        // this node's numbered pictures ("images.image_3" or "ref_images.ref_image_0": [node, 0]), in order
        QList<QPair<int, QJsonValue>> pictures;
        QString prefix;
        for (auto in = inputs.begin(); in != inputs.end(); ++in) {
            const qsizetype cut = in.key().lastIndexOf(QStringLiteral("image_"));
            if (cut < 0 || !in.key().contains(QLatin1Char('.')) || !isLink(in.value()))
                continue;
            bool isNumber = false;
            const int n = in.key().mid(cut + 6).toInt(&isNumber);
            if (!isNumber)
                continue;
            prefix = in.key().left(cut + 6);
            pictures.append({n, in.value()});
        }
        if (pictures.isEmpty())
            continue;
        std::sort(pictures.begin(), pictures.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        bool dropped = false;
        int next = pictures.first().first;   // numbered from 1, or from 0
        for (const auto& p : pictures) {
            inputs.remove(prefix + QString::number(p.first));
            const QString from = p.second.toArray()[0].toString();
            if (isLocalPicture(from) && value(from, QStringLiteral("image")).toString().trimmed().isEmpty() && p.first != pictures.first().first) {
                g.remove(from);   // an optional picture left empty
                dropped = true;
            } else {
                inputs[prefix + QString::number(next++)] = p.second;
            }
        }
        if (!dropped)
            continue;
        n[QStringLiteral("inputs")] = inputs;
        g[it.key()] = n;
    }
    return g;
}

QString Workflow::videoPromptSuffixWithImage()
{
    return QStringLiteral("the same single character as in the first frame, the same clothes and colours, the whole body stays "
                          "in the frame, static camera, locked-off shot, the background does not change, sharp and clear, "
                          "no motion blur");
}

namespace {
const QString kStartImageNode = QStringLiteral("20");

// the text graph with a LoadImage node as its first frame, and the prompt's ending for a picture
Workflow withStartImage(Workflow w, const QString& latentNode, const QString& imageInput, const QString& promptInput, const QString& name)
{
    QJsonObject g = w.graph();
    g[kStartImageNode] = node(QStringLiteral("LoadImage"), QStringLiteral("Reference picture"), {{QStringLiteral("image"), QString()}});
    QJsonObject n = g.value(latentNode).toObject();
    QJsonObject inputs = n.value(QStringLiteral("inputs")).toObject();
    inputs[imageInput] = QJsonArray{kStartImageNode, 0};
    n[QStringLiteral("inputs")] = inputs;
    g[latentNode] = n;
    w.fromJson(g, nullptr);
    const QString promptNode = promptInput.section(QLatin1Char('/'), 0, 0), input = promptInput.section(QLatin1Char('/'), 1);
    w.setValue(promptNode, input, QStringLiteral("the archer stands ready, then raises the bow, draws the string back to the cheek and "
                                                 "releases the arrow, ") + Workflow::videoPromptSuffixWithImage());
    w.setName(name);
    return w;
}
}  // namespace

Workflow Workflow::imageToVideo()
{
    return withStartImage(textToVideo(), QStringLiteral("55"), QStringLiteral("start_image"), QStringLiteral("6/text"),
                          QStringLiteral("First frame to video, Wan 2.2 TI2V-5B (built-in)"));
}

Workflow Workflow::imageToVideoHyperFlow()
{
    return withStartImage(textToVideoHyperFlow(), QStringLiteral("5"), QStringLiteral("first_frame"), QStringLiteral("5/prompt"),
                          QStringLiteral("First frame to video, HyperFlow H3 Flash (built-in)"));
}

namespace {
const QString kRemoveBackgroundNode = QStringLiteral("30");

// the Save node that takes the background-removal node's pictures, or what that node itself takes (the decoded frames)
QString saveNodeAfter(const QJsonObject& graph, const QJsonValue& source)
{
    for (auto it = graph.begin(); it != graph.end(); ++it) {
        const QJsonObject n = it.value().toObject();
        if (n.value(QStringLiteral("class_type")).toString() == QStringLiteral("SaveImage") &&
            n.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")) == source)
            return it.key();
    }
    return {};
}
}  // namespace

bool Workflow::hasRemoveBackground() const
{
    const QJsonObject n = graph_.value(kRemoveBackgroundNode).toObject();
    if (!n.value(QStringLiteral("class_type")).toString().startsWith(QStringLiteral("InspyrenetRembg")) || !makesVideo())
        return false;
    const QJsonValue decoded = n.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("image"));
    return !saveNodeAfter(graph_, QJsonArray{kRemoveBackgroundNode, 0}).isEmpty() || !saveNodeAfter(graph_, decoded).isEmpty();
}

bool Workflow::removeBackground() const
{
    return hasRemoveBackground() && !saveNodeAfter(graph_, QJsonArray{kRemoveBackgroundNode, 0}).isEmpty();
}

void Workflow::setRemoveBackground(bool on)
{
    if (!hasRemoveBackground() || on == removeBackground())
        return;
    const QJsonValue removed = QJsonArray{kRemoveBackgroundNode, 0};
    const QJsonValue decoded = graph_.value(kRemoveBackgroundNode).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("image"));
    const QString save = saveNodeAfter(graph_, on ? decoded : removed);
    QJsonObject n = graph_.value(save).toObject();
    QJsonObject inputs = n.value(QStringLiteral("inputs")).toObject();
    inputs[QStringLiteral("images")] = on ? removed : decoded;
    n[QStringLiteral("inputs")] = inputs;
    graph_[save] = n;
}

Workflow Workflow::referenceToVideoHyperFlow()
{
    // ComfyUI-HyperFlow-Flash's own graph (example_workflows/api/hyperflow_ref2v_api.json), ending in SaveImage like the
    // other video graphs (the sound is not decoded). The reference pictures are nodes "41" .. "43".
    QJsonObject g = textToVideoHyperFlow().graph();
    g[QStringLiteral("1")] = node(QStringLiteral("HyperFlowLoader"), QStringLiteral("Model"),
                                  {{QStringLiteral("unet_name"), QStringLiteral("minimax_h3_ref2va_hyperflow_flash_14b-Q4_K_S.gguf")},
                                   {QStringLiteral("weights_in"), QStringLiteral("auto")}});
    g[QStringLiteral("4")] = node(QStringLiteral("VAELoader"), QStringLiteral("Audio VAE"),
                                  {{QStringLiteral("vae_name"), QStringLiteral("minimax_h3_audio_vae_fp32.safetensors")}});
    QJsonObject in{{QStringLiteral("prompt"), QStringLiteral("<Picture 1> stands facing right, raises a wooden bow, draws the string back to the cheek and "
                                                             "releases the arrow, then lowers the bow, ") + videoPromptSuffix()},
                   {QStringLiteral("width"), 832}, {QStringLiteral("height"), 480}, {QStringLiteral("length"), 73},
                   {QStringLiteral("ref_image_size"), QStringLiteral("match")},
                   {QStringLiteral("clip"), link("2", 0)}, {QStringLiteral("vae"), link("3", 0)}, {QStringLiteral("audio_vae"), link("4", 0)}};
    for (int i = 0; i < 3; ++i) {
        const QString id = QString::number(41 + i);
        g[id] = node(QStringLiteral("LoadImage"), i == 0 ? QStringLiteral("Picture 1") : QStringLiteral("Picture %1 (optional)").arg(i + 1),
                     {{QStringLiteral("image"), QString()}});
        in[QStringLiteral("ref_images.ref_image_%1").arg(i)] = QJsonArray{id, 0};
    }
    g[QStringLiteral("5")] = node(QStringLiteral("MiniMaxH3ReferenceToVideo"), QStringLiteral("Video"), in);
    QJsonObject save = g.value(QStringLiteral("12")).toObject();
    QJsonObject sin = save.value(QStringLiteral("inputs")).toObject();
    sin[QStringLiteral("filename_prefix")] = QStringLiteral("video/ComfyLive_h3ref");
    save[QStringLiteral("inputs")] = sin;
    g[QStringLiteral("12")] = save;
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Reference pictures to video, HyperFlow H3 Flash (built-in)");
    return w;
}

bool Workflow::hasStartImage() const
{
    return graph_.value(kStartImageNode).toObject().value(QStringLiteral("class_type")).toString() == QStringLiteral("LoadImage") && makesVideo();
}

QString Workflow::startImage() const
{
    return hasStartImage() ? value(kStartImageNode, QStringLiteral("image")).toString() : QString();
}

void Workflow::setStartImage(const QString& path)
{
    if (!hasStartImage())
        return;
    setValue(kStartImageNode, QStringLiteral("image"), path);
    const QSize size = QImageReader(path).size();
    if (!size.isValid() || size.isEmpty())
        return;
    // the node that has the video's size: the same number of pixels, the picture's shape, on the 32-pixel grid
    for (auto it = graph_.begin(); it != graph_.end(); ++it) {
        const QJsonObject inputs = it.value().toObject().value(QStringLiteral("inputs")).toObject();
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (!(cls.contains(QStringLiteral("ToVideo")) || cls.contains(QStringLiteral("LatentVideo"))) ||
            !inputs.value(QStringLiteral("width")).isDouble() || !inputs.value(QStringLiteral("height")).isDouble())
            continue;
        const double area = inputs.value(QStringLiteral("width")).toDouble() * inputs.value(QStringLiteral("height")).toDouble();
        const double aspect = double(size.width()) / size.height();
        const int w = qMax(32, int(std::lround(std::sqrt(area * aspect) / 32.0)) * 32);
        const int h = qMax(32, int(std::lround(w / aspect / 32.0)) * 32);
        const QString id = it.key();
        setValue(id, QStringLiteral("width"), w);
        setValue(id, QStringLiteral("height"), h);
        break;
    }
}

QStringList Workflow::builtInIds()
{
    return {QStringLiteral("image"), QStringLiteral("music"), QStringLiteral("sfx"), QStringLiteral("speech"), QStringLiteral("voice"),
            QStringLiteral("video"), QStringLiteral("video-h3"), QStringLiteral("video-image"), QStringLiteral("video-h3-image"), QStringLiteral("qwen"), QStringLiteral("qwen-edit"),
            QStringLiteral("qwen-gguf"), QStringLiteral("qwen-gguf-edit"), QStringLiteral("qwen-full"), QStringLiteral("qwen-full-edit"),
            QStringLiteral("video-h3-ref"), QStringLiteral("video-ltx")};
}

bool Workflow::builtIn(const QString& id, Workflow* out)
{
    if (id == QStringLiteral("image"))
        *out = textToImage();
    else if (id == QStringLiteral("music"))
        *out = textToMusic();
    else if (id == QStringLiteral("sfx"))
        *out = textToSoundEffect();
    else if (id == QStringLiteral("speech"))
        *out = textToSpeech();
    else if (id == QStringLiteral("voice"))
        *out = textToSpeechDesigned();
    else if (id == QStringLiteral("video"))
        *out = textToVideo();
    else if (id == QStringLiteral("video-h3"))
        *out = textToVideoHyperFlow();
    else if (id == QStringLiteral("video-image"))
        *out = imageToVideo();
    else if (id == QStringLiteral("video-h3-image"))
        *out = imageToVideoHyperFlow();
    else if (id == QStringLiteral("qwen"))
        *out = qwenTextToImage();
    else if (id == QStringLiteral("qwen-edit"))
        *out = qwenEditImages();
    else if (id == QStringLiteral("video-h3-ref"))
        *out = referenceToVideoHyperFlow();
    else if (id == QStringLiteral("video-ltx"))
        *out = textToVideoLtx();
    else if (id == QStringLiteral("qwen-gguf"))
        *out = qwenGgufTextToImage();
    else if (id == QStringLiteral("qwen-gguf-edit"))
        *out = qwenGgufEditImages();
    else if (id == QStringLiteral("qwen-full"))
        *out = qwenFullTextToImage();
    else if (id == QStringLiteral("qwen-full-edit"))
        *out = qwenFullEditImages();
    else
        return false;
    return true;
}

bool Workflow::fromRecipe(const QJsonObject& o, QString* why)
{
    QString id = o.value(QStringLiteral("graph")).toString(QStringLiteral("music"));   // an old song file has none
    // a video recipe with a reference picture: the same model's graph with the picture as its first frame
    const QString imagePath = o.value(QStringLiteral("image")).toString();
    const bool withImage = id.endsWith(QStringLiteral("-image")) || ((id == QStringLiteral("video") || id == QStringLiteral("video-h3")) && !imagePath.isEmpty());
    if (id.endsWith(QStringLiteral("-image")))
        id.chop(6);
    Workflow w;
    if (!builtIn(withImage ? id + QStringLiteral("-image") : id, &w)) {
        if (why)
            *why = QStringLiteral("\"graph\": \"%1\" is not a built-in graph (%2)").arg(id, builtInIds().join(QStringLiteral(", ")));
        return false;
    }
    // friendly names for inputs whose plain name is on two nodes (the sound graph's two prompts)
    QHash<QString, QString> alias;
    if (id == QStringLiteral("sfx")) {
        alias.insert(QStringLiteral("prompt"), QStringLiteral("3/text"));
        alias.insert(QStringLiteral("avoid"), QStringLiteral("4/text"));
    } else if (id == QStringLiteral("image")) {
        alias.insert(QStringLiteral("prompt"), QStringLiteral("6/text"));
        alias.insert(QStringLiteral("avoid"), QStringLiteral("7/text"));
    } else if (id == QStringLiteral("voice")) {
        alias.insert(QStringLiteral("voice"), QStringLiteral("1/instruct"));
    } else if (id == QStringLiteral("speech")) {
        alias.insert(QStringLiteral("style"), QStringLiteral("1/instruct"));
    } else if (id == QStringLiteral("video")) {
        alias.insert(QStringLiteral("prompt"), QStringLiteral("6/text"));
        alias.insert(QStringLiteral("text"), QStringLiteral("6/text"));   // plain "text" is on both prompts
        alias.insert(QStringLiteral("avoid"), QStringLiteral("7/text"));
        alias.insert(QStringLiteral("seconds"), QStringLiteral("55/length"));
    } else if (id.startsWith(QStringLiteral("qwen"))) {
        alias.insert(QStringLiteral("text"), QStringLiteral("5/prompt"));
        alias.insert(QStringLiteral("avoid"), QStringLiteral("5/negative_prompt"));
        if (!id.startsWith(QStringLiteral("qwen-full")))   // the full model's KSampler has a plain "seed"
            alias.insert(QStringLiteral("seed"), QStringLiteral("9/noise_seed"));
    } else if (id == QStringLiteral("video-h3") || id == QStringLiteral("video-h3-ref")) {   // no "avoid": this model has no negative prompt
        alias.insert(QStringLiteral("text"), QStringLiteral("5/prompt"));
        alias.insert(QStringLiteral("seconds"), QStringLiteral("5/length"));
        alias.insert(QStringLiteral("seed"), QStringLiteral("8/noise_seed"));
    } else if (id == QStringLiteral("video-ltx")) {   // "sound" is joined to the prompt below; "seconds" sets both lengths
        alias.insert(QStringLiteral("prompt"), QStringLiteral("3/text"));
        alias.insert(QStringLiteral("text"), QStringLiteral("3/text"));
        alias.insert(QStringLiteral("avoid"), QStringLiteral("4/text"));
        alias.insert(QStringLiteral("seconds"), QStringLiteral("6/length"));
        alias.insert(QStringLiteral("seed"), QStringLiteral("10/noise_seed"));
    }
    const bool ltx = id == QStringLiteral("video-ltx");
    const bool videoRecipe = id == QStringLiteral("video") || id == QStringLiteral("video-h3") || id == QStringLiteral("video-h3-ref");
    // qwen-edit: "images": ["a.png", "b.png"] (or "image": "a.png") are pictures 1, 2 ...
    const bool qwenEdit = (id.startsWith(QStringLiteral("qwen")) && id.endsWith(QStringLiteral("-edit"))) || id == QStringLiteral("video-h3-ref");
    if (qwenEdit) {
        QJsonArray list = o.value(QStringLiteral("images")).toArray();
        if (list.isEmpty() && !imagePath.isEmpty())
            list = QJsonArray{imagePath};
        const QStringList nodes = w.localPictureNodes();
        if (list.size() > nodes.size()) {
            if (why)
                *why = QStringLiteral("\"images\" has %1 pictures; this graph takes up to %2").arg(list.size()).arg(nodes.size());
            return false;
        }
        for (qsizetype i = 0; i < list.size(); ++i)
            w.setValue(nodes[i], QStringLiteral("image"), list[i].toString());
    }
    if (videoRecipe)   // on unless the recipe says false
        w.setRemoveBackground(o.value(QStringLiteral("remove-background")).toBool(true));
    if (withImage && !imagePath.isEmpty())
        w.setStartImage(imagePath);   // first: "width" / "height" in the recipe then win over the picture's shape
    const QString videoPromptKey = id == QStringLiteral("video") ? QStringLiteral("6/text") : QStringLiteral("5/prompt");
    const bool rawPrompt = o.value(QStringLiteral("raw-prompt")).toBool();
    QJsonObject values;
    static const QStringList reserved{QStringLiteral("out"), QStringLiteral("random-seed"), QStringLiteral("params"),
                                      QStringLiteral("name"), QStringLiteral("graph"), QStringLiteral("comfylive"),
                                      QStringLiteral("raw-prompt"), QStringLiteral("remove-background")};
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (reserved.contains(it.key()) || it.key().startsWith(QLatin1Char('_')))   // "_comment": notes for people
            continue;
        if (videoRecipe && it.key() == QStringLiteral("image"))   // set above
            continue;
        if (qwenEdit && (it.key() == QStringLiteral("image") || it.key() == QStringLiteral("images")))
            continue;
        values[it.key()] = it.value();
    }
    const QJsonObject more = o.value(QStringLiteral("params")).toObject();
    for (auto it = more.begin(); it != more.end(); ++it)
        values[it.key()] = it.value();
    if (ltx && values.contains(QStringLiteral("sound"))) {
        // LTX-2 has one prompt for the picture and the sound: what is heard goes after what is seen, said plainly
        // (a vague or missing sound description gives an almost silent soundtrack)
        const QString sound = values.take(QStringLiteral("sound")).toString().trimmed();
        const QString key = values.contains(QStringLiteral("prompt")) ? QStringLiteral("prompt") : QStringLiteral("text");
        QString seen = values.value(key).toString().trimmed();
        if (!values.contains(key))
            seen = w.value(QStringLiteral("3"), QStringLiteral("text")).toString();
        if (!sound.isEmpty())
            values[key] = seen + (seen.endsWith(QLatin1Char('.')) || seen.isEmpty() ? QStringLiteral(" ") : QStringLiteral(". ")) +
                          QStringLiteral("Sound: ") + sound;
    }
    bool hasSeed = false;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QString key = alias.value(it.key(), it.key());
        QJsonValue v = it.value();
        if (v.isArray()) {   // lines: tags with ", ", the sentences of a speech with a space, else one per line
            QStringList parts;
            for (const QJsonValue& line : v.toArray())
                parts << line.toString();
            const QString input = key.section(QLatin1Char('/'), -1);
            const bool speechText = (id == QStringLiteral("speech") || id == QStringLiteral("voice")) && input == QStringLiteral("text");
            v = parts.join(input == QStringLiteral("tags") ? QStringLiteral(", ") : speechText ? QStringLiteral(" ") : QStringLiteral("\n"));
        }
        if (ltx && it.key() == QStringLiteral("seconds")) {
            // 8n+1 frames at the graph's rate (25: 4 s -> 97); the sound's length follows (setValue's tie)
            v = qMax(9, int(std::lround((v.toDouble() * w.videoFps() - 1.0) / 8.0)) * 8 + 1);
        } else if (videoRecipe && it.key() == QStringLiteral("seconds")) {
            // seconds -> frames at the model's rate, on the grid the model takes: 4n+1 (Wan; 3 s: 73) or 17n+5
            // (MiniMax H3; 3 s: 73, 5 s: 124)
            const double frames = v.toDouble() * w.videoFps();
            v = id == QStringLiteral("video") ? qMax(5, int(std::lround(frames / 4.0)) * 4 + 1)   // else MiniMax H3
                                              : qMax(22, int(std::ceil((frames - 5.0) / 17.0)) * 17 + 5);
        } else if (videoRecipe && (key == videoPromptKey || (key == QStringLiteral("prompt") && id.startsWith(QStringLiteral("video-h3")))) &&
                   !rawPrompt) {
            v = v.toString().trimmed() + QStringLiteral(", ") + (withImage ? videoPromptSuffixWithImage() : videoPromptSuffix());
        }
        bool found = false;
        for (auto n = w.graph_.begin(); n != w.graph_.end(); ++n) {
            const QString nodeId = n.key();
            const QJsonObject inputs = n.value().toObject().value(QStringLiteral("inputs")).toObject();
            const QString input = key.contains(QLatin1Char('/')) ? key.section(QLatin1Char('/'), 1) : key;
            if ((key.contains(QLatin1Char('/')) && key.section(QLatin1Char('/'), 0, 0) != nodeId) || !inputs.contains(input) ||
                isLink(inputs.value(input)))
                continue;
            w.setValue(nodeId, input, v);
            found = true;
            hasSeed = hasSeed || input == QStringLiteral("seed") || input == QStringLiteral("noise_seed");
        }
        if (!found) {
            if (why)
                *why = QStringLiteral("no input \"%1\" in the built-in \"%2\" graph").arg(it.key(), id);
            return false;
        }
    }
    *this = w;
    fromRecipe_ = true;
    recipeRandomSeed_ = o.value(QStringLiteral("random-seed")).toBool(!hasSeed);
    recipeOut_ = o.value(QStringLiteral("out")).toString();
    if (o.contains(QStringLiteral("name")))
        name_ = o.value(QStringLiteral("name")).toString();
    return true;
}

bool Workflow::isRecipe(const QJsonObject& o)
{
    if (o.value(QStringLiteral("graph")).isString() || o.value(QStringLiteral("comfylive")).toString() == QStringLiteral("recipe"))
        return true;
    if (o.value(QStringLiteral("comfylive")).toString() == QStringLiteral("settings"))
        return false;
    for (auto it = o.begin(); it != o.end(); ++it)   // an API graph: every entry a node
        if (!it.key().startsWith(QLatin1Char('_')) && !it.value().toObject().contains(QStringLiteral("class_type")))
            return !o.contains(QStringLiteral("nodes"));   // the editor's format is not a recipe either
    return false;
}

namespace {
// the settings file's key for the result it belongs to: "video" for an MP4, else "audio"
QString resultKey(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QStringLiteral("mp4") ? QStringLiteral("video")
           : (s == QStringLiteral("png") || s == QStringLiteral("jpg") || s == QStringLiteral("jpeg")) ? QStringLiteral("image") : QStringLiteral("audio");
}
}  // namespace

QString Workflow::settingsPathFor(const QString& audioPath)
{
    const QFileInfo f(audioPath);
    return f.dir().filePath(f.completeBaseName() + QStringLiteral(".json"));
}

bool Workflow::writeSettings(const QString& audioPath, const QJsonObject& graph, const QString& name, const QJsonObject& extra,
                             QString* why)
{
    QJsonObject o = extra;
    o[QStringLiteral("comfylive")] = QStringLiteral("settings");
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("name")] = name;
    o[resultKey(audioPath)] = QFileInfo(audioPath).fileName();
    o[QStringLiteral("created")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    o[QStringLiteral("how")] = QStringLiteral("ComfyLive: Open graph... with this file, then Generate: the same song or sound "
                                              "again (New seed each run is turned off for it)");
    o[QStringLiteral("graph")] = graph;
    QFile f(settingsPathFor(audioPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (why)
            *why = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool Workflow::copySettings(const QString& from, const QString& to, const QJsonObject& extra)
{
    QFile in(settingsPathFor(from));
    if (!in.open(QIODevice::ReadOnly))
        return false;
    QJsonObject o = QJsonDocument::fromJson(in.readAll()).object();
    if (o.value(QStringLiteral("comfylive")).toString() != QStringLiteral("settings"))
        return false;
    for (auto it = extra.begin(); it != extra.end(); ++it)
        o[it.key()] = it.value();
    o.remove(resultKey(from));
    o[resultKey(to)] = QFileInfo(to).fileName();
    QFile out(settingsPathFor(to));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool Workflow::fromJson(const QJsonObject& graphIn, QString* why)
{
    // a song's settings file: the graph inside it
    fromSettings_ = graphIn.value(QStringLiteral("comfylive")).toString() == QStringLiteral("settings") &&
                    graphIn.value(QStringLiteral("graph")).isObject();
    fromRecipe_ = false;
    const QJsonObject graph = fromSettings_ ? graphIn.value(QStringLiteral("graph")).toObject() : graphIn;
    if (graph.contains(QStringLiteral("nodes")) && graph.value(QStringLiteral("nodes")).isArray()) {
        if (why)
            *why = QStringLiteral("This is a ComfyUI workflow in the editor's format. In ComfyUI use Workflow > Export (API), then open that file.");
        return false;
    }
    for (auto it = graph.begin(); it != graph.end(); ++it) {
        if (!it.value().toObject().contains(QStringLiteral("class_type"))) {
            if (why)
                *why = QStringLiteral("Node \"%1\" has no class_type: not an API-format ComfyUI graph.").arg(it.key());
            return false;
        }
    }
    if (graph.isEmpty()) {
        if (why)
            *why = QStringLiteral("The graph is empty.");
        return false;
    }
    graph_ = graph;
    return true;
}

bool Workflow::load(const QString& path, QString* why)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (why)
            *why = f.errorString();
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject()) {
        if (why)
            *why = err.errorString();
        return false;
    }
    if (isRecipe(doc.object())) {
        if (!fromRecipe(doc.object(), why))
            return false;
        for (const QString& id : localPictureNodes()) {   // a picture named beside the recipe
            const QString p = value(id, QStringLiteral("image")).toString();
            if (id != kStartImageNode && !p.isEmpty() && QFileInfo(p).isRelative())
                setValue(id, QStringLiteral("image"), QDir::cleanPath(QFileInfo(path).absoluteDir().filePath(p)));
        }
        if (hasStartImage() && !startImage().isEmpty() && QFileInfo(startImage()).isRelative()) {
            QJsonObject n = graph_.value(kStartImageNode).toObject();   // beside the recipe; the size was set from it?
            const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteDir().filePath(startImage()));
            const bool sized = doc.object().contains(QStringLiteral("width")) || doc.object().contains(QStringLiteral("height"));
            if (sized)
                setValue(kStartImageNode, QStringLiteral("image"), absolute);
            else
                setStartImage(absolute);
        }
        if (!doc.object().contains(QStringLiteral("name")))
            name_ = QFileInfo(path).completeBaseName();
        return true;
    }
    if (!fromJson(doc.object(), why))
        return false;
    name_ = QFileInfo(path).completeBaseName();
    return true;
}

bool Workflow::save(const QString& path, QString* why) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (why)
            *why = f.errorString();
        return false;
    }
    f.write(QJsonDocument(graph_).toJson(QJsonDocument::Indented));
    return true;
}

QList<ParamDef> Workflow::params(const QJsonObject& objectInfo) const
{
    QStringList ids = graph_.keys();
    std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b) {
        bool okA = false, okB = false;
        const double na = a.toDouble(&okA), nb = b.toDouble(&okB);
        if (okA && okB)
            return na < nb;
        return a < b;
    });
    QList<ParamDef> out;
    for (const QString& id : ids) {
        const QJsonObject n = graph_.value(id).toObject();
        const QString cls = n.value(QStringLiteral("class_type")).toString();
        const QString title = n.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("title")).toString(cls);
        const QJsonObject inputs = n.value(QStringLiteral("inputs")).toObject();
        // the server's order for this class when it knows it (required, then optional); else the file's
        QStringList order;
        const QJsonObject classInfo = objectInfo.value(cls).toObject();
        for (const QJsonValue& v : classInfo.value(QStringLiteral("input_order")).toObject().value(QStringLiteral("required")).toArray())
            order << v.toString();
        for (const QJsonValue& v : classInfo.value(QStringLiteral("input_order")).toObject().value(QStringLiteral("optional")).toArray())
            order << v.toString();
        for (const QString& k : inputs.keys())
            if (!order.contains(k))
                order << k;
        for (const QString& input : order) {
            if (!inputs.contains(input))
                continue;
            const QJsonValue v = inputs.value(input);
            if (isLink(v))
                continue;
            ParamDef p;
            p.nodeId = id;
            p.input = input;
            p.group = title == cls ? cls : title + QStringLiteral("  (") + cls + QStringLiteral(")");
            p.classType = cls;
            p.value = v;
            const QJsonArray spec = specOf(objectInfo, cls, input);
            const QString type = spec.isEmpty() || spec[0].isArray() ? QString() : spec[0].toString();
            const QJsonObject opts = spec.size() > 1 ? spec[1].toObject() : QJsonObject();
            p.tooltip = opts.value(QStringLiteral("tooltip")).toString();
            const QStringList choices = choicesOf(spec);
            if (input == QStringLiteral("image") && isLocalPicture(id)) {
                p.kind = ParamDef::Kind::Line;   // a file on this PC (ComfyClient sends it), not one of the server's list
            } else if (!choices.isEmpty()) {
                p.kind = ParamDef::Kind::Combo;
                p.choices = choices;
            } else if (type == QStringLiteral("INT")) {
                p.kind = ParamDef::Kind::Int;
                p.minimum = std::max(-1e15, opts.value(QStringLiteral("min")).toDouble(-1e15));
                p.maximum = std::min(1e15, opts.value(QStringLiteral("max")).toDouble(1e15));
                p.step = std::max(1.0, opts.value(QStringLiteral("step")).toDouble(1.0));
            } else if (type == QStringLiteral("FLOAT")) {
                p.kind = ParamDef::Kind::Float;
                p.minimum = opts.value(QStringLiteral("min")).toDouble(-1e9);
                p.maximum = opts.value(QStringLiteral("max")).toDouble(1e9);
                p.step = opts.value(QStringLiteral("step")).toDouble(0.01);
                p.decimals = std::clamp(int(std::ceil(-std::log10(std::max(1e-6, p.step)))), 1, 6);
            } else if (type == QStringLiteral("BOOLEAN")) {
                p.kind = ParamDef::Kind::Bool;
            } else if (type == QStringLiteral("STRING")) {
                p.kind = opts.value(QStringLiteral("multiline")).toBool() ? ParamDef::Kind::Text : ParamDef::Kind::Line;
            } else if (v.isBool()) {   // the server does not know this node: guess from the value
                p.kind = ParamDef::Kind::Bool;
            } else if (v.isDouble()) {
                const double d = v.toDouble();
                p.kind = d == std::floor(d) && std::fabs(d) < 1e15 ? ParamDef::Kind::Int : ParamDef::Kind::Float;
                p.decimals = 3;
                p.step = p.kind == ParamDef::Kind::Int ? 1.0 : 0.05;
            } else if (v.isString()) {
                p.kind = v.toString().size() > 60 || input == QStringLiteral("text") ? ParamDef::Kind::Text : ParamDef::Kind::Line;
            } else {
                continue;   // an object or a list: not editable here
            }
            out << p;
        }
    }
    return out;
}

namespace {

void setInput(QJsonObject& graph, const QString& nodeId, const QString& input, const QJsonValue& v)
{
    QJsonObject n = graph.value(nodeId).toObject();
    QJsonObject inputs = n.value(QStringLiteral("inputs")).toObject();
    inputs[input] = v;
    n[QStringLiteral("inputs")] = inputs;
    graph[nodeId] = n;
}

}  // namespace

void Workflow::setValue(const QString& nodeId, const QString& input, const QJsonValue& v)
{
    setInput(graph_, nodeId, input, v);
    // One length, two inputs: an ACE-Step encoder's duration <-> the empty ACE-Step latent's seconds, and Stable Audio's
    // timing seconds_total <-> the empty audio latent's seconds. Only a plain number follows; an input that is a link is
    // the user's own wiring and stays.
    struct Tie { const char* classA; const char* inputA; const char* classB; const char* inputB; };
    static const Tie ties[] = {
        {"TextEncodeAceStepAudio", "duration", "EmptyAceStep", "seconds"},
        {"ConditioningStableAudio", "seconds_total", "EmptyLatentAudio", "seconds"},
        {"EmptyLTXVLatentVideo", "length", "LTXVEmptyLatentAudio", "frames_number"},   // LTX-2: the picture's and the sound's frames
    };
    const QString cls = graph_.value(nodeId).toObject().value(QStringLiteral("class_type")).toString();
    QString other, otherClass;
    for (const Tie& t : ties) {
        if (cls.startsWith(QLatin1String(t.classA)) && input == QLatin1String(t.inputA)) {
            other = QLatin1String(t.inputB);
            otherClass = QLatin1String(t.classB);
        } else if (cls.startsWith(QLatin1String(t.classB)) && input == QLatin1String(t.inputB)) {
            other = QLatin1String(t.inputA);
            otherClass = QLatin1String(t.classA);
        }
    }
    if (other.isEmpty())
        return;
    QStringList ids;
    for (auto it = graph_.begin(); it != graph_.end(); ++it) {
        const QJsonObject n = it.value().toObject();
        if (it.key() != nodeId && n.value(QStringLiteral("class_type")).toString().startsWith(otherClass) &&
            n.value(QStringLiteral("inputs")).toObject().value(other).isDouble())
            ids << it.key();
    }
    for (const QString& id : ids)
        setInput(graph_, id, other, v.toDouble());
}

bool Workflow::needsSpeechServer() const
{
    for (auto it = graph_.begin(); it != graph_.end(); ++it)
        if (it.value().toObject().value(QStringLiteral("class_type")).toString().startsWith(QStringLiteral("FB_Qwen3TTS")))
            return true;
    return false;
}

bool Workflow::makesVideo() const
{
    for (auto it = graph_.begin(); it != graph_.end(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls.contains(QStringLiteral("ToVideo")) || cls.contains(QStringLiteral("LatentVideo")))
            return true;   // ...ImageToVideo, ...ReferenceToVideo, Empty...LatentVideo
    }
    return false;
}

double Workflow::videoFps() const
{
    for (auto it = graph_.begin(); it != graph_.end(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls == QStringLiteral("Wan22ImageToVideoLatent") || cls.startsWith(QStringLiteral("MiniMaxH3")))
            return 24.0;
        if (cls == QStringLiteral("LTXVConditioning"))   // LTX: the rate the graph is set to (25 in the built-in one)
            return it.value().toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("frame_rate")).toDouble(25.0);
    }
    return 16.0;
}

QJsonValue Workflow::value(const QString& nodeId, const QString& input) const
{
    return graph_.value(nodeId).toObject().value(QStringLiteral("inputs")).toObject().value(input);
}

QStringList Workflow::snapToSteps(const QJsonObject& objectInfo)
{
    QStringList changed;
    auto onGrid = [](const ParamDef& p, double v) { return std::fmod(v - p.minimum, p.step) == 0.0 && v >= p.minimum && v <= p.maximum; };
    auto gridded = [](const ParamDef& p) { return p.kind == ParamDef::Kind::Int && p.step > 1.0 && p.value.isDouble() && p.minimum > -1e9; };
    // a node's width and height together: the same shape, about the same area
    QHash<QString, QPair<ParamDef, ParamDef>> sizes;
    for (const ParamDef& p : params(objectInfo)) {
        if (!gridded(p))
            continue;
        if (p.input == QStringLiteral("width"))
            sizes[p.nodeId].first = p;
        else if (p.input == QStringLiteral("height"))
            sizes[p.nodeId].second = p;
    }
    QStringList paired;
    for (auto it = sizes.begin(); it != sizes.end(); ++it) {
        const ParamDef& pw = it->first;
        const ParamDef& ph = it->second;
        if (pw.nodeId.isEmpty() || ph.nodeId.isEmpty())
            continue;
        paired << it.key();
        const double w = pw.value.toDouble(), h = ph.value.toDouble();
        if ((onGrid(pw, w) && onGrid(ph, h)) || w <= 0 || h <= 0)
            continue;
        const double ratio = w / h, area = w * h;
        double best = 1e30, bw = w, bh = h;
        const double w0 = pw.minimum + std::round((w - pw.minimum) / pw.step) * pw.step;
        for (int k = -2; k <= 2; ++k) {   // widths near the given one; for each, the height on the grid nearest the ratio
            const double cw = std::clamp(w0 + k * pw.step, pw.minimum, pw.maximum);
            const double ch = std::clamp(ph.minimum + std::round((cw / ratio - ph.minimum) / ph.step) * ph.step, ph.minimum, ph.maximum);
            const double score = std::fabs(cw / ch - ratio) / ratio + 0.1 * std::fabs(cw * ch - area) / area;
            if (score < best) {
                best = score;
                bw = cw;
                bh = ch;
            }
        }
        setValue(pw.nodeId, pw.input, qint64(bw));
        setValue(ph.nodeId, ph.input, qint64(bh));
        changed << QStringLiteral("%1: %2 x %3 is not a size this model takes (steps of %4), using %5 x %6 (the same shape)")
                       .arg(pw.group).arg(qint64(w)).arg(qint64(h)).arg(qint64(pw.step)).arg(qint64(bw)).arg(qint64(bh));
    }
    for (const ParamDef& p : params(objectInfo)) {
        if (!gridded(p))
            continue;
        if (paired.contains(p.nodeId) && (p.input == QStringLiteral("width") || p.input == QStringLiteral("height")))
            continue;
        const double v = p.value.toDouble();
        double snapped = p.minimum + std::round((v - p.minimum) / p.step) * p.step;
        snapped = std::clamp(snapped, p.minimum, p.maximum);
        if (snapped == v)
            continue;
        setValue(p.nodeId, p.input, qint64(snapped));
        changed << QStringLiteral("%1.%2: %3 is not a value this input takes (steps of %4 from %5), using %6")
                       .arg(p.group, p.input).arg(qint64(v)).arg(qint64(p.step)).arg(qint64(p.minimum)).arg(qint64(snapped));
    }
    return changed;
}

QStringList Workflow::fixMissingChoices(const QJsonObject& objectInfo)
{
    QStringList changed;
    for (const ParamDef& p : params(objectInfo)) {
        if (p.kind != ParamDef::Kind::Combo || p.choices.isEmpty() || p.choices.contains(p.value.toString()))
            continue;
        if (p.classType.startsWith(QStringLiteral("FB_Qwen3TTS")) || p.classType == QStringLiteral("LoadImage"))
            continue;   // the speech node's lists are its own (speakers, languages, sizes), never a model file
        // the same family as the missing one: an ACE-Step model for an ACE-Step graph, a Stable Audio model or a T5
        // encoder for a sound graph, else an SDXL picture model. A sound model with nothing of its family on the server
        // stays as it is (ComfyUI then names the missing file): another family's model would only fail later.
        const QString was = p.value.toString();
        auto has = [](const QString& s, const char* word) { return s.contains(QLatin1String(word), Qt::CaseInsensitive); };
        auto isStableAudio = [&](const QString& s) { return has(s, "stable") && has(s, "audio"); };
        QStringList family;
        bool keepWhenNone = true;
        if (has(was, "ace"))
            family = p.choices.filter(QStringLiteral("ace"), Qt::CaseInsensitive);
        else if (isStableAudio(was))
            std::copy_if(p.choices.begin(), p.choices.end(), std::back_inserter(family), isStableAudio);
        else if (has(was, "minimax") || has(was, "hyperflow"))   // MiniMax H3 / HyperFlow: only another file of the same part
            family = p.choices.filter(QStringLiteral("minimax"), Qt::CaseInsensitive)
                         .filter(has(was, "audio_vae") ? QStringLiteral("audio_vae") : has(was, "video_vae") ? QStringLiteral("video_vae")
                                 : has(was, "qwen3vl") ? QStringLiteral("qwen3vl") : QStringLiteral("hyperflow"), Qt::CaseInsensitive);
        else if (has(was, "qwen-image") || has(was, "qwen_image") || has(was, "qwen3vl_8b"))   // Qwen Image 2.1: the same part only
            family = p.choices.filter(has(was, "vae") ? QStringLiteral("qwen_image_2.1_vae") : has(was, "qwen3vl") ? QStringLiteral("qwen3vl_8b")
                                                                                                                  : QStringLiteral("qwen-image-2.1"),
                                      Qt::CaseInsensitive);
        else if (has(was, "umt5"))
            family = p.choices.filter(QStringLiteral("umt5"), Qt::CaseInsensitive);
        else if (has(was, "wan"))   // a Wan video model, or its VAE: only another of the same kind
            family = p.choices.filter(QStringLiteral("wan"), Qt::CaseInsensitive)
                         .filter(has(was, "vae") ? QStringLiteral("vae") : QStringLiteral("ti2v"), Qt::CaseInsensitive);
        else if (has(was, "t5"))
            family = p.choices.filter(QStringLiteral("t5"), Qt::CaseInsensitive);
        else {
            keepWhenNone = false;
            for (const QString& c : p.choices)
                if (has(c, "Juggernaut") || has(c, "sd_xl"))
                    family << c;
        }
        if (family.isEmpty() && keepWhenNone) {
            changed << QStringLiteral("%1.%2: \"%3\" is not on the server: download it (README.md)").arg(p.group, p.input, was);
            continue;
        }
        const QString pick = family.isEmpty() ? p.choices.first() : family.first();
        setValue(p.nodeId, p.input, pick);
        changed << QStringLiteral("%1.%2: \"%3\" is not on the server, using \"%4\"").arg(p.group, p.input, p.value.toString(), pick);
    }
    return changed;
}
