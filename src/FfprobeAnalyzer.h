// ============================================================================
// FfprobeAnalyzer - C++ port of the per-file analysis in Analyze-AudioBitrate.ps1
// ============================================================================
// Runs ffprobe (via QProcess) to extract audio metadata, calculate the true
// bitrate from a full packet-size scan, and classify container/stream errors.
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>
#include <atomic>

// One analyzed file. Mirrors the PSCustomObject built in the PowerShell scan pass.
struct FileResult {
    QString folder;       // path relative to the scan root
    QString fullPath;
    QString fileName;
    int     headerKbps  = 0;   // bit_rate reported by the container header
    int     calcKbps    = 0;   // bitrate computed from summed packet sizes
    double  duration    = 0.0; // seconds
    QString codec;
    QString channels;          // "2.0", "5.1", "8ch", ...
    QString error;             // "" when the file is healthy
    QString errorDetail;       // raw ffprobe stderr, for diagnostics
    qint64  fileSize    = 0;    // bytes
    double  scanTime    = 0.0;  // seconds spent probing this file
};
Q_DECLARE_METATYPE(FileResult)

class FfprobeAnalyzer {
public:
    explicit FfprobeAnalyzer(QString ffprobePath);

    // Fully analyze one file (metadata pass, optional duration fallback, packet
    // scan). `cancel`, if set, aborts long-running ffprobe calls promptly.
    FileResult analyze(const QString& fullPath,
                       const QString& relFolder,
                       const QString& fileName,
                       qint64 fileSize,
                       const std::atomic<bool>* cancel = nullptr) const;

    // Port of Get-ErrorType: map ffprobe stderr to a short error category.
    static QString classifyError(const QString& stderrText, bool hasStreams);

    // Most common CalcKbps among files in a folder (0 if none valid).
    static int majorityBitrate(const QList<FileResult>& folderItems);

    // True when `calc` sits outside `tolerance` of the folder majority.
    static bool isOutlier(int calc, int majority, double tolerance);

    // Port of Format-Duration: "---", "mm:ss", or "hh:mm:ss".
    static QString formatDuration(double seconds);

    // Port of the channels switch (1->"1.0", 2->"2.0", 6->"5.1", 8->"7.1").
    static QString channelsLabel(int channels);

private:
    struct ProcResult { QString out; QString err; int exitCode; };
    ProcResult run(const QStringList& args, const std::atomic<bool>* cancel) const;
    double getFormatDuration(const QString& path, const std::atomic<bool>* cancel) const;

    QString m_ffprobe;
};
