#include "AppConfig.h"
static QString expandTilde(const QString& path) {
    if (path.startsWith("~")) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

static void resolvePathAgainstBase(QString &p, const QString &base)
{
    if (p.isEmpty()) return;
    QString tmp = expandTilde(p);
    QFileInfo fi(tmp);
    if (!fi.isAbsolute()) {
        tmp = QDir(base).absoluteFilePath(tmp);
    } else {
        tmp = fi.absoluteFilePath();
    }
    tmp = QDir::cleanPath(tmp);
    p = QDir::fromNativeSeparators(tmp);
}

AppConfig::AppConfig()
{
    // Resolve defaults at construction time
    resolveRelativePaths();
}

void AppConfig::resolveRelativePaths()
{
    QString base = QCoreApplication::applicationDirPath();
    resolvePathAgainstBase(projectRoot, base);
    resolvePathAgainstBase(outputFolder, base);
    resolvePathAgainstBase(assetRoot, base);
}

void AppConfig::load(QSettings& s)
{
    projectRoot     = s.value("projectRoot",     projectRoot).toString();
    outputFolder    = s.value("outputFolder",    outputFolder).toString();
    assetRoot       = s.value("assetRoot",       assetRoot).toString();
    heroReference   = s.value("heroReference",   heroReference).toString();
    positivePrompt  = s.value("positivePrompt",  positivePrompt).toString();
    negativePrompt  = s.value("negativePrompt",  negativePrompt).toString();
    styleProfileId  = s.value("styleProfileId",  styleProfileId).toString();
    batchId         = s.value("batchId",         batchId).toString();
    comfyuiServer   = s.value("comfyuiServer",   comfyuiServer).toString();
    category        = s.value("category",        category).toString();
    pythonExe       = s.value("pythonExe",       pythonExe).toString();
    ipAdapterWeight = s.value("ipAdapterWeight", ipAdapterWeight).toDouble();
    denoise         = s.value("denoise",         denoise).toDouble();
    steps           = s.value("steps",           steps).toInt();
    fullPipeline    = s.value("fullPipeline",    fullPipeline).toBool();
    includeSpine    = s.value("includeSpine",    includeSpine).toBool();
    // Normalize any loaded paths
    resolveRelativePaths();
}

void AppConfig::save(QSettings& s) const
{
    s.setValue("projectRoot",     projectRoot);
    s.setValue("outputFolder",    outputFolder);
    s.setValue("assetRoot",       assetRoot);
    s.setValue("heroReference",   heroReference);
    s.setValue("positivePrompt",  positivePrompt);
    s.setValue("negativePrompt",  negativePrompt);
    s.setValue("styleProfileId",  styleProfileId);
    s.setValue("batchId",         batchId);
    s.setValue("comfyuiServer",   comfyuiServer);
    s.setValue("category",        category);
    s.setValue("pythonExe",       pythonExe);
    s.setValue("ipAdapterWeight", ipAdapterWeight);
    s.setValue("denoise",         denoise);
    s.setValue("steps",           steps);
    s.setValue("fullPipeline",    fullPipeline);
    s.setValue("includeSpine",    includeSpine);
}
