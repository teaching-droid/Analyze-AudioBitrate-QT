#include "FfprobeAnalyzer.h"

#include <QProcess>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QtMath>

FfprobeAnalyzer::FfprobeAnalyzer(QString ffprobePath)
    : m_ffprobe(std::move(ffprobePath)) {}

// ----------------------------------------------------------------------------
// Run ffprobe, blocking, while still honouring the cancel flag. QProcess handles
// argument quoting, so we pass args as a list (no manual escaping like the PS
// Invoke-Process helper needed).
// ----------------------------------------------------------------------------
FfprobeAnalyzer::ProcResult
FfprobeAnalyzer::run(const QStringList& args, const std::atomic<bool>* cancel) const
{
    QProcess p;
    p.setProgram(m_ffprobe);
    p.setArguments(args);
    p.start();

    if (!p.waitForStarted(5000))
        return { QString(), QStringLiteral("ffprobe failed to start"), -1 };

    // Poll in short slices so a huge packet scan can be cancelled mid-flight.
    while (!p.waitForFinished(200)) {
        if (cancel && cancel->load()) {
            p.kill();
            p.waitForFinished(2000);
            return { QString(), QStringLiteral("cancelled"), -1 };
        }
        if (p.state() == QProcess::NotRunning)
            break; // crashed / finished between polls
    }

    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const QString err = QString::fromUtf8(p.readAllStandardError());
    return { out, err, p.exitCode() };
}

double FfprobeAnalyzer::getFormatDuration(const QString& path,
                                          const std::atomic<bool>* cancel) const
{
    const ProcResult r = run({
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        path
    }, cancel);

    const QString v = r.out.trimmed();
    if (!v.isEmpty() && v != QLatin1String("N/A")) {
        bool ok = false;
        const double d = v.toDouble(&ok);
        if (ok) return d;
    }
    return 0.0;
}

QString FfprobeAnalyzer::channelsLabel(int channels)
{
    switch (channels) {
        case 1: return QStringLiteral("1.0");
        case 2: return QStringLiteral("2.0");
        case 6: return QStringLiteral("5.1");
        case 8: return QStringLiteral("7.1");
        default: return channels > 0 ? QStringLiteral("%1ch").arg(channels) : QString();
    }
}

// Port of Get-ErrorType - order matters, first match wins.
QString FfprobeAnalyzer::classifyError(const QString& stderrText, bool hasStreams)
{
    const QString e = stderrText.toLower();
    if (e.contains("moov atom not found"))                     return "MOOV MISSING";
    if (e.contains("moof atom not found"))                     return "MOOF MISSING";
    if (e.contains("invalid data found"))                      return "CORRUPT DATA";
    if (e.contains("end of file"))                             return "TRUNCATED";
    if (e.contains("could not find codec"))                    return "UNKNOWN CODEC";
    if (e.contains("fourcc not found"))                        return "FOURCC MISSING";
    if (e.contains("header damaged") || e.contains("corrupt")) return "BAD HEADER";
    if (e.contains("non monotonous dts"))                      return "DTS ERROR";
    if (e.contains("decode_slice_header"))                     return "SLICE ERROR";
    if (e.contains("no frame!"))                               return "NO FRAMES";
    if (e.contains("permission denied"))                       return "ACCESS DENIED";
    if (e.contains("no such file"))                            return "NOT FOUND";
    if (e.contains("invalid argument"))                        return "BAD FORMAT";
    if (e.contains("protocol not found"))                      return "BAD PATH";
    if (e.contains("error") || e.contains("fail"))             return "PROBE ERROR";
    if (!hasStreams)                                           return "NO AUDIO";
    return QString();
}

int FfprobeAnalyzer::majorityBitrate(const QList<FileResult>& folderItems)
{
    QHash<int, int> counts;
    int best = 0, bestCount = 0;
    for (const FileResult& r : folderItems) {
        if (r.calcKbps <= 0) continue;
        const int c = ++counts[r.calcKbps];
        if (c > bestCount) { bestCount = c; best = r.calcKbps; } // first-seen wins ties
    }
    return best;
}

bool FfprobeAnalyzer::isOutlier(int calc, int majority, double tolerance)
{
    if (majority <= 0 || calc <= 0) return false;
    return std::abs(calc - majority) > (majority * tolerance);
}

QString FfprobeAnalyzer::formatDuration(double seconds)
{
    if (seconds <= 0) return QStringLiteral("---");
    const qint64 total = static_cast<qint64>(seconds);
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    if (h >= 1)
        return QStringLiteral("%1:%2:%3")
            .arg(h, 2, 10, QLatin1Char('0'))
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2")
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

// ----------------------------------------------------------------------------
// analyze() - direct port of the per-file body of the PowerShell scan pass.
// ----------------------------------------------------------------------------
FileResult FfprobeAnalyzer::analyze(const QString& fullPath,
                                    const QString& relFolder,
                                    const QString& fileName,
                                    qint64 fileSize,
                                    const std::atomic<bool>* cancel) const
{
    FileResult r;
    r.folder   = relFolder;
    r.fullPath = fullPath;
    r.fileName = fileName;
    r.fileSize = fileSize;

    QElapsedTimer timer;
    timer.start();

    // --- Pass 1: stream metadata (codec / channels / duration / header bitrate)
    const ProcResult meta = run({
        "-v", "error", "-select_streams", "a:0",
        "-show_entries", "stream=codec_name,channels,sample_rate,duration,bit_rate",
        "-of", "json", fullPath
    }, cancel);

    QString stderrMsg = meta.err.trimmed();
    bool hasStreams = false;

    QJsonParseError je{};
    const QJsonDocument doc = QJsonDocument::fromJson(meta.out.toUtf8(), &je);
    if (je.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonArray streams = doc.object().value("streams").toArray();
        if (!streams.isEmpty()) {
            hasStreams = true;
            const QJsonObject s = streams.first().toObject();
            r.codec    = s.value("codec_name").toString();
            r.channels = channelsLabel(s.value("channels").toInt());

            const QJsonValue dur = s.value("duration");
            const QString durStr = dur.isString() ? dur.toString() : QString();
            if (!durStr.isEmpty() && durStr != QLatin1String("N/A"))
                r.duration = durStr.toDouble();

            const QJsonValue br = s.value("bit_rate");
            const QString brStr = br.isString() ? br.toString() : QString();
            if (!brStr.isEmpty() && brStr != QLatin1String("N/A"))
                r.headerKbps = qRound(brStr.toDouble() / 1000.0);
        }
    }

    // Classify any header-level error.
    r.error = classifyError(meta.err, hasStreams);

    // Fallback: pull duration from the container format section.
    if (r.duration <= 0 && r.error.isEmpty())
        r.duration = getFormatDuration(fullPath, cancel);

    // --- Pass 3: full packet-size scan (true bitrate + mid-file corruption)
    if (r.duration > 0 && r.error.isEmpty()) {
        const ProcResult pkt = run({
            "-v", "error", "-select_streams", "a:0",
            "-show_entries", "packet=size",
            "-of", "default=noprint_wrappers=1:nokey=1", fullPath
        }, cancel);

        const QString pktErr = pkt.err.trimmed();
        if (!pktErr.isEmpty()) {
            const QString e = classifyError(pkt.err, true);
            if (!e.isEmpty()) {
                r.error   = QStringLiteral("MID-FILE: %1").arg(e);
                stderrMsg = pktErr;
            }
        }

        if (!pkt.out.isEmpty()) {
            qint64 totalBytes = 0;
            const QList<QStringView> lines =
                QStringView(pkt.out).split(u'\n', Qt::SkipEmptyParts);
            for (const QStringView& line : lines) {
                bool ok = false;
                const qint64 v = line.trimmed().toLongLong(&ok);
                if (ok) totalBytes += v;
            }
            if (totalBytes > 0)
                r.calcKbps = qRound((totalBytes * 8.0 / 1000.0) / r.duration);
        } else if (r.error.isEmpty()) {
            r.error = QStringLiteral("NO PACKETS");
        }
    } else if (r.error.isEmpty()) {
        r.error = QStringLiteral("NO DURATION");
    }

    r.errorDetail = stderrMsg;
    r.scanTime    = timer.elapsed() / 1000.0;
    return r;
}
