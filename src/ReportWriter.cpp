#include "ReportWriter.h"

#include <QFile>
#include <QTextStream>
#include <QSaveFile>
#include <QHash>
#include <QtMath>

namespace {

QString padRight(const QString& s, int width)
{
    return s.length() >= width ? s : s + QString(width - s.length(), QLatin1Char(' '));
}

QString mb(qint64 bytes)
{
    return QString::number(bytes / 1048576.0, 'f', 1);
}

// Ordered folder groups, first-seen order preserved (matches Group-Object).
struct FolderGroup { QString rel; QList<FileResult> items; };

QList<FolderGroup> groupByFolder(const QList<FileResult>& results)
{
    QList<FolderGroup> groups;
    QHash<QString, int> index;
    for (const FileResult& r : results) {
        auto it = index.find(r.folder);
        if (it == index.end()) {
            index.insert(r.folder, groups.size());
            groups.append({ r.folder, { r } });
        } else {
            groups[it.value()].items.append(r);
        }
    }
    return groups;
}

QString mmss(double seconds)
{
    const qint64 total = static_cast<qint64>(seconds);
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// Escape a field for CSV (RFC 4180 style, matching Export-Csv behaviour).
QString csvField(const QString& v)
{
    if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')) ||
        v.contains(QLatin1Char('\n')) || v.contains(QLatin1Char('\r'))) {
        QString e = v;
        e.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + e + QLatin1Char('"');
    }
    return v;
}

} // namespace

namespace ReportWriter {

bool writeTxt(const QString& path, const QList<FileResult>& results,
              const ScanMeta& meta, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    const QString bar(80, QLatin1Char('='));

    out << "Audio Bitrate Scan - " << meta.timestamp << '\n';
    out << "Working directory: " << meta.rootPath << '\n';
    out << "Total: " << meta.totalFiles << " file(s) in "
        << meta.folderCount << " folder(s)\n\n";

    const QList<FolderGroup> groups = groupByFolder(results);

    for (const FolderGroup& g : groups) {
        const int majority = FfprobeAnalyzer::majorityBitrate(g.items);

        out << bar << '\n';
        out << ' ' << g.rel << " (" << g.items.size() << " file(s))\n";
        out << bar << '\n';

        out << ' ' << padRight("Calculated", 11) << "  " << padRight("Header", 11)
            << "  " << padRight("Codec", 8) << "  " << padRight("Ch", 4)
            << "  Filename\n";
        out << ' ' << QString(11, '-') << "  " << QString(11, '-') << "  "
            << QString(8, '-') << "  " << QString(4, '-') << "  "
            << QString(50, '-') << '\n';

        int outliers = 0, errors = 0;
        for (const FileResult& r : g.items) {
            const QString calc = r.calcKbps  > 0 ? QStringLiteral("%1 kbps").arg(r.calcKbps)  : QStringLiteral("---");
            const QString hdr  = r.headerKbps > 0 ? QStringLiteral("%1 kbps").arg(r.headerKbps) : QStringLiteral("---");
            const QString cod  = r.codec.isEmpty()    ? QStringLiteral("---") : r.codec;
            const QString ch   = r.channels.isEmpty() ? QStringLiteral("---") : r.channels;
            QString name = r.fileName;
            if (name.length() > 55) name = name.left(52) + QStringLiteral("...");

            QString line = QLatin1Char(' ') + padRight(calc, 11) + "  " + padRight(hdr, 11)
                + "  " + padRight(cod, 8) + "  " + padRight(ch, 4) + "  " + name;

            if (!r.error.isEmpty()) {
                ++errors;
                line += QStringLiteral("  !! %1 (%2 MB)").arg(r.error, mb(r.fileSize));
            } else if (FfprobeAnalyzer::isOutlier(r.calcKbps, majority, meta.tolerance)) {
                ++outliers;
                const int diff = r.calcKbps - majority;
                line += QStringLiteral("  << DIFFERENT (%1%2 kbps)")
                            .arg(diff > 0 ? "+" : "").arg(diff);
            }
            out << line << '\n';
        }

        QStringList summary{ QStringLiteral("%1 file(s)").arg(g.items.size()) };
        if (majority > 0) summary << QStringLiteral("typical: %1 kbps").arg(majority);
        if (outliers > 0) summary << QStringLiteral("%1 DIFFERENT").arg(outliers);
        if (errors   > 0) summary << QStringLiteral("%1 error(s)").arg(errors);
        out << " [" << summary.join(QStringLiteral(" | ")) << "]\n\n";
    }

    out << bar << '\n';
    out << QStringLiteral("Scan complete. %1 files in %2 (avg %3s per file)\n")
            .arg(meta.totalFiles)
            .arg(mmss(meta.elapsedSeconds))
            .arg(QString::number(meta.avgPerFile, 'f', 1));

    // --- DEFECTIVE FILES summary (grouped by error type, count desc). ---
    QList<FileResult> defective;
    for (const FileResult& r : results)
        if (!r.error.isEmpty()) defective.append(r);

    if (!defective.isEmpty()) {
        out << '\n' << bar << '\n';
        out << " DEFECTIVE FILES: " << defective.size() << " file(s) with errors\n";
        out << bar << "\n\n";

        QList<QString> errOrder;
        QHash<QString, QList<FileResult>> byErr;
        for (const FileResult& r : defective) {
            if (!byErr.contains(r.error)) errOrder.append(r.error);
            byErr[r.error].append(r);
        }
        std::sort(errOrder.begin(), errOrder.end(),
                  [&](const QString& a, const QString& b) {
                      return byErr[a].size() > byErr[b].size();
                  });

        for (const QString& e : errOrder) {
            const QList<FileResult>& items = byErr[e];
            out << "  " << e << " (" << items.size() << "):\n";
            for (const FileResult& d : items) {
                out << "    " << mb(d.fileSize) << " MB  "
                    << FfprobeAnalyzer::formatDuration(d.duration) << "  "
                    << d.folder << QLatin1Char('/') << d.fileName << '\n';
            }
            out << '\n';
        }

        out << " Error type reference:\n";
        out << "   MOOV MISSING   - File index missing (incomplete download)\n";
        out << "   TRUNCATED      - File cut short unexpectedly\n";
        out << "   CORRUPT DATA   - Invalid data in stream\n";
        out << "   FOURCC MISSING - Unknown codec identifiers in container\n";
        out << "   BAD HEADER     - Container header damaged\n";
        out << "   DTS ERROR      - Broken timestamps (non-monotonous DTS)\n";
        out << "   UNKNOWN CODEC  - Codec not recognized\n";
        out << "   NO AUDIO       - No audio stream found\n";
        out << "   MID-FILE: xxx  - Header OK but error during full scan\n";
        out << "   PROBE ERROR    - Other error\n";
    } else {
        out << "\nNo defective files found.\n";
    }

    // --- BITRATE OUTLIERS summary. ---
    QList<QString> outlierLines;
    for (const FolderGroup& g : groups) {
        QList<FileResult> valid;
        for (const FileResult& r : g.items)
            if (r.error.isEmpty() && r.calcKbps > 0) valid.append(r);
        if (valid.size() <= 1) continue;
        const int majority = FfprobeAnalyzer::majorityBitrate(valid);
        for (const FileResult& r : valid) {
            if (FfprobeAnalyzer::isOutlier(r.calcKbps, majority, meta.tolerance)) {
                const int diff = r.calcKbps - majority;
                outlierLines << QStringLiteral("  %1 kbps (expected %2, %3%4)  %5/%6")
                        .arg(r.calcKbps).arg(majority)
                        .arg(diff > 0 ? "+" : "").arg(diff)
                        .arg(r.folder, r.fileName);
            }
        }
    }

    if (!outlierLines.isEmpty()) {
        out << '\n' << bar << '\n';
        out << " BITRATE OUTLIERS: " << outlierLines.size()
            << " file(s) differ from their folder\n";
        out << bar << "\n\n";
        for (const QString& l : outlierLines) out << l << '\n';
    }

    out.flush();
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool writeCsv(const QString& path, const QList<FileResult>& results, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "Folder,FileName,CalcKbps,HeaderKbps,Codec,Channels,Duration,SizeMB,Error,ScanTime\r\n";
    for (const FileResult& r : results) {
        out << csvField(r.folder)   << ','
            << csvField(r.fileName) << ','
            << r.calcKbps           << ','
            << r.headerKbps         << ','
            << csvField(r.codec)    << ','
            << csvField(r.channels) << ','
            << QString::number(r.duration, 'f', 1) << ','
            << mb(r.fileSize)       << ','
            << csvField(r.error)    << ','
            << QString::number(r.scanTime, 'f', 1) << "\r\n";
    }

    out.flush();
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace ReportWriter
