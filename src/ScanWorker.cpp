#include "ScanWorker.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QHash>

void ScanWorker::configure(const QString& root, const QString& ffprobePath,
                           const QStringList& extensions, double tolerance)
{
    m_root      = root;
    m_ffprobe   = ffprobePath;
    m_exts      = extensions;
    m_tolerance = tolerance;
    m_cancel.store(false);
}

void ScanWorker::doScan()
{
    QElapsedTimer wall;
    wall.start();

    // --- Collect matching files recursively (Get-ChildItem -File -Recurse). ---
    QList<QFileInfo> files;
    QDirIterator it(m_root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (m_exts.contains(fi.suffix().toLower()))
            files.append(fi);
    }

    // --- Group by directory, preserving first-seen order (Group-Object). ---
    QList<QString> folderOrder;
    QHash<QString, QList<QFileInfo>> groups;
    for (const QFileInfo& fi : files) {
        const QString dir = fi.absolutePath();
        if (!groups.contains(dir)) folderOrder.append(dir);
        groups[dir].append(fi);
    }

    const int total = files.size();
    emit started(total, folderOrder.size());
    if (total == 0) {
        emit finished(0, 0, wall.elapsed() / 1000.0);
        return;
    }

    const FfprobeAnalyzer analyzer(m_ffprobe);
    const QDir rootDir(m_root);

    int    done      = 0;
    int    defective = 0;
    double sumTimes  = 0.0;   // running total of per-file scan seconds (for avg/ETA)

    for (const QString& dir : folderOrder) {
        if (m_cancel.load()) break;

        QString rel = rootDir.relativeFilePath(dir);
        if (rel.isEmpty()) rel = QStringLiteral(".");

        QList<FileResult> folderResults;
        for (const QFileInfo& fi : groups.value(dir)) {
            if (m_cancel.load()) break;

            const double avg = done > 0 ? sumTimes / done : 0.0;
            const double eta = avg * (total - done);
            emit progress(done, total, avg, eta, fi.fileName());

            FileResult r = analyzer.analyze(fi.absoluteFilePath(), rel,
                                            fi.fileName(), fi.size(), &m_cancel);
            ++done;
            sumTimes += r.scanTime;
            if (!r.error.isEmpty()) ++defective;

            folderResults.append(r);
            emit fileScanned(r);
        }

        emit folderCompleted(rel, FfprobeAnalyzer::majorityBitrate(folderResults));
    }

    // Final progress tick so the bar lands on 100% / clears the "current file".
    const double avg = done > 0 ? sumTimes / done : 0.0;
    emit progress(done, total, avg, 0.0, QString());
    emit finished(total, defective, wall.elapsed() / 1000.0);
}
