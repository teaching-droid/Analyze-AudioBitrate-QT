// ============================================================================
// ScanWorker - runs the recursive scan on a background thread.
// ============================================================================
// Owns no UI. Emits signals the MainWindow consumes; all heavy lifting (ffprobe
// calls) happens here so the GUI thread stays responsive. Mirrors the SCAN PASS
// loop of Analyze-AudioBitrate.ps1 (group by folder, per-file probe, per-folder
// majority bitrate).
// ============================================================================
#pragma once

#include <QObject>
#include <QStringList>
#include <atomic>
#include "FfprobeAnalyzer.h"

class ScanWorker : public QObject {
    Q_OBJECT
public:
    explicit ScanWorker(QObject* parent = nullptr) : QObject(parent) {}

    void configure(const QString& root, const QString& ffprobePath,
                   const QStringList& extensions, double tolerance);

    void requestCancel() { m_cancel.store(true); }

public slots:
    void doScan();   // invoked once the worker is moved onto its thread

signals:
    void started(int totalFiles, int folderCount);
    void progress(int done, int total, double avgPerFile,
                  double etaSeconds, const QString& currentFile);
    void fileScanned(const FileResult& result);
    void folderCompleted(const QString& folder, int majorityKbps);
    void finished(int totalFiles, int defectiveCount, double elapsedSeconds);
    void failed(const QString& message);

private:
    QString     m_root;
    QString     m_ffprobe;
    QStringList m_exts;      // lower-case, without the leading dot
    double      m_tolerance = 0.10;
    std::atomic<bool> m_cancel{false};
};
