// ============================================================================
// ReportWriter - writes the TXT and CSV reports from a completed scan.
// ============================================================================
// Mirrors the report sections of Analyze-AudioBitrate.ps1: per-folder tables,
// the DEFECTIVE FILES summary, the BITRATE OUTLIERS summary, and the CSV export.
// ============================================================================
#pragma once

#include <QString>
#include <QList>
#include "FfprobeAnalyzer.h"

namespace ReportWriter {

// Metadata shown in the TXT header / footer.
struct ScanMeta {
    QString rootPath;
    QString timestamp;      // "yyyy-MM-dd HH:mm:ss"
    int     totalFiles = 0;
    int     folderCount = 0;
    double  elapsedSeconds = 0.0;
    double  avgPerFile = 0.0;
    double  tolerance = 0.10;
};

// Returns false and fills `error` if the file could not be written.
bool writeTxt(const QString& path, const QList<FileResult>& results,
              const ScanMeta& meta, QString* error = nullptr);

bool writeCsv(const QString& path, const QList<FileResult>& results,
              QString* error = nullptr);

} // namespace ReportWriter
