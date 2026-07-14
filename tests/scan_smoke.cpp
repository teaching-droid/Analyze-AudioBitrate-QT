// Headless driver for the scan backend - exercises ScanWorker/FfprobeAnalyzer
// against a real folder using a real ffprobe. Not shipped; built only when
// -DBUILD_SCAN_SMOKE=ON. Usage: scan_smoke <folder> <ffprobe>
#include <QCoreApplication>
#include <QTextStream>
#include "ScanWorker.h"
#include "FfprobeAnalyzer.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 3) { out << "usage: scan_smoke <folder> <ffprobe>\n"; return 2; }

    ScanWorker w;
    w.configure(argv[1], argv[2], { "mp4", "mkv", "avi", "mov", "ts", "m4v" }, 0.10);

    QObject::connect(&w, &ScanWorker::started, [&](int t, int f) {
        out << "START total=" << t << " folders=" << f << "\n"; });
    QObject::connect(&w, &ScanWorker::fileScanned, [&](const FileResult& r) {
        out << QStringLiteral("  FILE %1 | calc=%2 hdr=%3 codec=%4 ch=%5 dur=%6 err=[%7]\n")
                   .arg(r.fileName).arg(r.calcKbps).arg(r.headerKbps)
                   .arg(r.codec, r.channels)
                   .arg(QString::number(r.duration, 'f', 1), r.error); });
    QObject::connect(&w, &ScanWorker::folderCompleted, [&](const QString& f, int m) {
        out << "  FOLDER " << f << " majority=" << m << " kbps\n"; });
    QObject::connect(&w, &ScanWorker::finished, [&](int t, int d, double e) {
        out << QStringLiteral("DONE files=%1 defective=%2 elapsed=%3s\n")
                   .arg(t).arg(d).arg(QString::number(e, 'f', 1)); });

    w.doScan();      // runs synchronously on this thread
    out.flush();
    return 0;
}
