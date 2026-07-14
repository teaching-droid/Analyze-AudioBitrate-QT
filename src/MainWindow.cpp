#include "MainWindow.h"
#include "ScanWorker.h"
#include "ReportWriter.h"

#include <QApplication>
#include <QThread>
#include <QSettings>
#include <QDateTime>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QEvent>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QStatusBar>
#include <QFont>

namespace {

// The video containers the scanner recognises (suffix, lower-case, no dot).
const QStringList kExtensions = { "mp4", "mkv", "avi", "mov", "ts", "m4v" };

// Table columns.
enum Col { ColFolder, ColFile, ColCalc, ColHeader, ColCodec,
           ColCh, ColDuration, ColSize, ColStatus, ColCount };

// A table item that sorts by a stored numeric key rather than its text.
class NumItem : public QTableWidgetItem {
public:
    NumItem(const QString& text, double key)
        : QTableWidgetItem(text), m_key(key) {}
    bool operator<(const QTableWidgetItem& other) const override {
        if (const auto* n = dynamic_cast<const NumItem*>(&other))
            return m_key < n->m_key;
        return QTableWidgetItem::operator<(other);
    }
private:
    double m_key;
};

// Languages offered in the menu: code + native name (endonym, never translated).
struct Lang { const char* code; const char* name; };
const Lang kLanguages[] = {
    { "en", "English"  },
    { "de", "Deutsch"  },
    { "ja", u8"日本語" },
    { "it", "Italiano" },
};

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    qRegisterMetaType<FileResult>("FileResult");
    buildUi();

    QSettings s;
    m_dirEdit->setText(s.value("lastDir", QDir::homePath()).toString());
    const QString savedFf = s.value("ffprobePath").toString();
    m_ffprobeEdit->setText(savedFf.isEmpty() ? defaultFfprobePath() : savedFf);

    // Pick the startup language: saved choice, else system locale, else English.
    QString lang = s.value("language").toString();
    if (lang.isEmpty()) {
        const QString sys = QLocale::system().name().left(2);
        lang = (sys == "de" || sys == "ja" || sys == "it") ? sys : QStringLiteral("en");
    }
    switchLanguage(lang);
}

MainWindow::~MainWindow()
{
    if (m_worker) m_worker->requestCancel();
    teardownThread();
}

// ----------------------------------------------------------------------------
// UI construction - widgets are created here; all their text is set later in
// retranslateUi() so a language switch can re-apply everything at runtime.
// ----------------------------------------------------------------------------
void MainWindow::buildUi()
{
    resize(1100, 720);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // --- Inputs -------------------------------------------------------------
    auto* inputs = new QGridLayout;
    m_dirLabel = new QLabel;
    inputs->addWidget(m_dirLabel, 0, 0);
    m_dirEdit = new QLineEdit;
    inputs->addWidget(m_dirEdit, 0, 1);
    m_browseDir = new QPushButton;
    inputs->addWidget(m_browseDir, 0, 2);

    m_ffprobeLabel = new QLabel;
    inputs->addWidget(m_ffprobeLabel, 1, 0);
    m_ffprobeEdit = new QLineEdit;
    inputs->addWidget(m_ffprobeEdit, 1, 1);
    m_browseFf = new QPushButton;
    inputs->addWidget(m_browseFf, 1, 2);
    inputs->setColumnStretch(1, 1);
    root->addLayout(inputs);

    // --- Action buttons + progress -----------------------------------------
    auto* actions = new QHBoxLayout;
    m_scanBtn   = new QPushButton;
    m_cancelBtn = new QPushButton;
    m_cancelBtn->setEnabled(false);
    actions->addWidget(m_scanBtn);
    actions->addWidget(m_cancelBtn);
    actions->addStretch(1);
    m_exportTxtBtn = new QPushButton;
    m_exportCsvBtn = new QPushButton;
    m_exportTxtBtn->setEnabled(false);
    m_exportCsvBtn->setEnabled(false);
    actions->addWidget(m_exportTxtBtn);
    actions->addWidget(m_exportCsvBtn);
    root->addLayout(actions);

    m_progress = new QProgressBar;
    m_progress->setTextVisible(true);
    root->addWidget(m_progress);
    m_status = new QLabel;
    root->addWidget(m_status);

    // --- Results table ------------------------------------------------------
    m_table = new QTableWidget(0, ColCount);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(false);
    root->addWidget(m_table, 3);

    // --- Summary panel ------------------------------------------------------
    m_summaryBox = new QGroupBox;
    auto* summaryLayout = new QVBoxLayout(m_summaryBox);
    m_summary = new QPlainTextEdit;
    m_summary->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::TypeWriter);
    m_summary->setFont(mono);
    summaryLayout->addWidget(m_summary);
    root->addWidget(m_summaryBox, 1);

    setCentralWidget(central);

    // --- Menus --------------------------------------------------------------
    m_fileMenu = menuBar()->addMenu(QString());
    m_exitAct  = m_fileMenu->addAction(QString(), this, &QWidget::close);
    createLanguageMenu();
    m_helpMenu = menuBar()->addMenu(QString());
    m_aboutAct = m_helpMenu->addAction(QString(), this, &MainWindow::onAbout);

    // --- Wiring -------------------------------------------------------------
    connect(m_browseDir,    &QPushButton::clicked, this, &MainWindow::onBrowseFolder);
    connect(m_browseFf,     &QPushButton::clicked, this, &MainWindow::onBrowseFfprobe);
    connect(m_scanBtn,      &QPushButton::clicked, this, &MainWindow::onScan);
    connect(m_cancelBtn,    &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(m_exportTxtBtn, &QPushButton::clicked, this, &MainWindow::onExportTxt);
    connect(m_exportCsvBtn, &QPushButton::clicked, this, &MainWindow::onExportCsv);

    retranslateUi();
}

void MainWindow::createLanguageMenu()
{
    m_langMenu  = menuBar()->addMenu(QString());
    m_langGroup = new QActionGroup(this);
    m_langGroup->setExclusive(true);
    for (const Lang& l : kLanguages) {
        QAction* a = m_langMenu->addAction(QString::fromUtf8(l.name));
        a->setCheckable(true);
        a->setData(QString::fromLatin1(l.code));
        m_langGroup->addAction(a);
    }
    connect(m_langGroup, &QActionGroup::triggered, this, [this](QAction* a) {
        switchLanguage(a->data().toString());
    });
}

// ----------------------------------------------------------------------------
// i18n
// ----------------------------------------------------------------------------
void MainWindow::switchLanguage(const QString& code)
{
    if (m_appTranslator) {
        qApp->removeTranslator(m_appTranslator);
        delete m_appTranslator; m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        qApp->removeTranslator(m_qtTranslator);
        delete m_qtTranslator; m_qtTranslator = nullptr;
    }

    m_language = code;

    if (code != QLatin1String("en")) {
        m_appTranslator = new QTranslator(this);
        if (m_appTranslator->load(QStringLiteral("abr_%1").arg(code), QStringLiteral(":/i18n")))
            qApp->installTranslator(m_appTranslator);
        else { delete m_appTranslator; m_appTranslator = nullptr; }

        // Qt's own strings (standard dialog buttons) if available in the install.
        m_qtTranslator = new QTranslator(this);
        const QString qtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        if (m_qtTranslator->load(QStringLiteral("qtbase_%1").arg(code), qtDir))
            qApp->installTranslator(m_qtTranslator);
        else { delete m_qtTranslator; m_qtTranslator = nullptr; }
    }

    // Installing/removing a translator posts LanguageChange (→ changeEvent →
    // retranslateUi); call directly too so the "en" path also refreshes.
    retranslateUi();

    // Keep the menu check in sync and persist the choice.
    if (m_langGroup) {
        for (QAction* a : m_langGroup->actions())
            if (a->data().toString() == code) { a->setChecked(true); break; }
    }
    QSettings().setValue("language", code);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Analyze Audio Bitrate  v%1").arg(QApplication::applicationVersion()));

    m_dirLabel->setText(tr("Folder to scan:"));
    m_dirEdit->setPlaceholderText(tr("Root folder - scanned recursively"));
    m_browseDir->setText(tr("Browse..."));
    m_ffprobeLabel->setText(tr("ffprobe.exe:"));
    m_ffprobeEdit->setPlaceholderText(tr("Path to ffprobe (part of FFmpeg)"));
    m_browseFf->setText(tr("Browse..."));

    m_scanBtn->setText(tr("Scan"));
    m_cancelBtn->setText(tr("Cancel"));
    m_exportTxtBtn->setText(tr("Export TXT..."));
    m_exportCsvBtn->setText(tr("Export CSV..."));

    m_table->setHorizontalHeaderLabels({
        tr("Folder"), tr("File"), tr("Audio Rate (kbps)"), tr("Header (kbps)"),
        tr("Codec"), tr("Ch"), tr("Duration"), tr("Size (MB)"), tr("Status")
    });
    // Make the two bitrate columns unambiguous.
    if (auto* h = m_table->horizontalHeaderItem(ColCalc))
        h->setToolTip(tr("True audio data rate, measured from a full packet scan"));
    if (auto* h = m_table->horizontalHeaderItem(ColHeader))
        h->setToolTip(tr("Audio bitrate as declared by the container header"));

    m_summaryBox->setTitle(tr("Summary"));
    m_summary->setPlaceholderText(tr("Defective files and bitrate outliers appear here after a scan."));

    m_fileMenu->setTitle(tr("&File"));
    m_exitAct->setText(tr("E&xit"));
    m_langMenu->setTitle(tr("&Language"));
    m_helpMenu->setTitle(tr("&Help"));
    m_aboutAct->setText(tr("&About"));

    // Idle / finished status line (progress updates handle the scanning state).
    if (!m_thread) {
        if (m_scanDone) {
            m_status->setText(tr("Done. %1 file(s), %2 defective, in %3.")
                                  .arg(m_totalFiles).arg(m_lastDefective)
                                  .arg(FfprobeAnalyzer::formatDuration(m_elapsedSeconds)));
        } else {
            m_status->setText(tr("Ready."));
        }
    }
}

QString MainWindow::defaultFfprobePath() const
{
    const QByteArray env = qgetenv("FFPROBE_PATH");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!onPath.isEmpty()) return onPath;
    return QStringLiteral("D:/ffmpeg/ffprobe.exe");
}

// ----------------------------------------------------------------------------
// Input browsing
// ----------------------------------------------------------------------------
void MainWindow::onBrowseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose folder to scan"),
        m_dirEdit->text().isEmpty() ? QDir::homePath() : m_dirEdit->text());
    if (!dir.isEmpty()) m_dirEdit->setText(QDir::toNativeSeparators(dir));
}

void MainWindow::onBrowseFfprobe()
{
#ifdef Q_OS_WIN
    const QString filter = tr("ffprobe (ffprobe.exe);;All files (*)");
#else
    const QString filter = tr("ffprobe (ffprobe);;All files (*)");
#endif
    const QString f = QFileDialog::getOpenFileName(
        this, tr("Locate ffprobe"), m_ffprobeEdit->text(), filter);
    if (!f.isEmpty()) m_ffprobeEdit->setText(QDir::toNativeSeparators(f));
}

// ----------------------------------------------------------------------------
// Scan lifecycle
// ----------------------------------------------------------------------------
void MainWindow::onScan()
{
    const QString ffprobe = m_ffprobeEdit->text().trimmed();
    if (!QFileInfo(ffprobe).isFile()) {
        QMessageBox::warning(this, tr("ffprobe not found"),
            tr("ffprobe was not found at:\n%1\n\nInstall FFmpeg and point to its "
               "ffprobe executable, or set the FFPROBE_PATH environment variable.")
               .arg(ffprobe.isEmpty() ? tr("(empty)") : ffprobe));
        return;
    }

    const QString dir = m_dirEdit->text().trimmed();
    if (!QFileInfo(dir).isDir()) {
        QMessageBox::warning(this, tr("Folder not found"),
            tr("The folder to scan does not exist:\n%1").arg(dir));
        return;
    }

    QSettings s;
    s.setValue("lastDir", dir);
    s.setValue("ffprobePath", ffprobe);

    // Reset state.
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    m_results.clear();
    m_folderRows.clear();
    m_folderMajority.clear();
    m_summary->clear();
    m_progress->setValue(0);
    m_scanDone = false;

    m_scanRoot      = dir;
    m_scanTimestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    setScanning(true);

    m_thread = new QThread(this);
    m_worker = new ScanWorker;
    m_worker->configure(dir, ffprobe, kExtensions, kTolerance);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,        m_worker, &ScanWorker::doScan);
    connect(m_worker, &ScanWorker::started,     this, &MainWindow::onScanStarted);
    connect(m_worker, &ScanWorker::progress,    this, &MainWindow::onProgress);
    connect(m_worker, &ScanWorker::fileScanned, this, &MainWindow::onFileScanned);
    connect(m_worker, &ScanWorker::folderCompleted, this, &MainWindow::onFolderCompleted);
    connect(m_worker, &ScanWorker::finished,    this, &MainWindow::onScanFinished);
    connect(m_worker, &ScanWorker::failed, this, [this](const QString& msg) {
        QMessageBox::critical(this, tr("Scan error"), msg);
    });

    m_thread->start();
}

void MainWindow::onCancel()
{
    if (m_worker) m_worker->requestCancel();
    m_status->setText(tr("Cancelling..."));
    m_cancelBtn->setEnabled(false);
}

void MainWindow::setScanning(bool scanning)
{
    m_scanBtn->setEnabled(!scanning);
    m_cancelBtn->setEnabled(scanning);
    m_dirEdit->setEnabled(!scanning);
    m_ffprobeEdit->setEnabled(!scanning);
    m_browseDir->setEnabled(!scanning);
    m_browseFf->setEnabled(!scanning);
    if (scanning) {
        m_exportTxtBtn->setEnabled(false);
        m_exportCsvBtn->setEnabled(false);
    }
}

void MainWindow::teardownThread()
{
    if (!m_thread) return;
    m_thread->quit();
    m_thread->wait();
    delete m_worker; m_worker = nullptr;
    delete m_thread; m_thread = nullptr;
}

// ----------------------------------------------------------------------------
// Worker signals
// ----------------------------------------------------------------------------
void MainWindow::onScanStarted(int totalFiles, int folderCount)
{
    m_totalFiles  = totalFiles;
    m_folderCount = folderCount;
    m_progress->setMaximum(qMax(1, totalFiles));
    m_progress->setValue(0);
    m_status->setText(tr("Found %1 file(s) in %2 folder(s)...")
                          .arg(totalFiles).arg(folderCount));
    statusBar()->showMessage(tr("Scanning %1...").arg(m_scanRoot));
}

void MainWindow::onProgress(int done, int total, double avgPerFile,
                            double etaSeconds, const QString& currentFile)
{
    m_progress->setMaximum(qMax(1, total));
    m_progress->setValue(done);

    QString s = tr("Scanning %1 / %2").arg(done).arg(total);
    if (avgPerFile > 0)
        s += tr("  |  %1 s/file").arg(QString::number(avgPerFile, 'f', 1));
    if (etaSeconds > 0)
        s += tr("  |  ETA %1").arg(FfprobeAnalyzer::formatDuration(etaSeconds));
    if (!currentFile.isEmpty())
        s += QStringLiteral("  |  %1").arg(currentFile);
    m_status->setText(s);
}

void MainWindow::addResultRow(const FileResult& r)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    m_table->setItem(row, ColFolder, new QTableWidgetItem(r.folder));
    m_table->setItem(row, ColFile,   new QTableWidgetItem(r.fileName));
    m_table->setItem(row, ColCalc,   new NumItem(
        r.calcKbps  > 0 ? QString::number(r.calcKbps)  : QStringLiteral("---"), r.calcKbps));
    m_table->setItem(row, ColHeader, new NumItem(
        r.headerKbps > 0 ? QString::number(r.headerKbps) : QStringLiteral("---"), r.headerKbps));
    m_table->setItem(row, ColCodec,  new QTableWidgetItem(r.codec.isEmpty()    ? QStringLiteral("---") : r.codec));
    m_table->setItem(row, ColCh,     new QTableWidgetItem(r.channels.isEmpty() ? QStringLiteral("---") : r.channels));
    m_table->setItem(row, ColDuration, new NumItem(FfprobeAnalyzer::formatDuration(r.duration), r.duration));
    m_table->setItem(row, ColSize,   new NumItem(QString::number(r.fileSize / 1048576.0, 'f', 1),
                                                 static_cast<double>(r.fileSize)));
    m_table->setItem(row, ColStatus, new QTableWidgetItem(
        r.error.isEmpty() ? QStringLiteral("...") : r.error));
}

void MainWindow::styleErrorRow(int row, const QString& error)
{
    const QColor bg(139, 0, 0);
    const QColor fg(255, 232, 150);
    for (int c = 0; c < m_table->columnCount(); ++c) {
        if (auto* it = m_table->item(row, c)) {
            it->setBackground(bg);
            it->setForeground(fg);
        }
    }
    if (auto* it = m_table->item(row, ColStatus)) it->setText(error);
}

void MainWindow::onFileScanned(const FileResult& r)
{
    const int row = m_results.size();
    m_results.append(r);
    m_folderRows[r.folder].append(row);
    addResultRow(r);
    if (!r.error.isEmpty()) styleErrorRow(row, r.error);
    m_table->scrollToBottom();
}

void MainWindow::onFolderCompleted(const QString& folder, int majorityKbps)
{
    m_folderMajority.insert(folder, majorityKbps);
    for (int row : m_folderRows.value(folder)) {
        const FileResult& r = m_results.at(row);
        if (!r.error.isEmpty()) continue; // error rows already styled
        auto* status = m_table->item(row, ColStatus);
        if (!status) continue;

        if (FfprobeAnalyzer::isOutlier(r.calcKbps, majorityKbps, kTolerance)) {
            const int diff = r.calcKbps - majorityKbps;
            status->setText(tr("DIFFERENT (%1%2 kbps)").arg(diff > 0 ? "+" : "").arg(diff));
            status->setForeground(QColor(211, 47, 47));
            QFont f = status->font(); f.setBold(true); status->setFont(f);
        } else {
            status->setText(majorityKbps > 0 ? tr("OK") : QStringLiteral("-"));
            status->setForeground(QColor(46, 160, 70));
        }
    }
}

void MainWindow::onScanFinished(int totalFiles, int defectiveCount, double elapsedSeconds)
{
    teardownThread();

    m_totalFiles     = totalFiles;
    m_lastDefective  = defectiveCount;
    m_elapsedSeconds = elapsedSeconds;
    m_scanDone       = true;
    double sum = 0.0;
    for (const FileResult& r : m_results) sum += r.scanTime;
    m_avgPerFile = m_results.isEmpty() ? 0.0 : sum / m_results.size();

    setScanning(false);
    const bool haveData = !m_results.isEmpty();
    m_exportTxtBtn->setEnabled(haveData);
    m_exportCsvBtn->setEnabled(haveData);
    m_table->setSortingEnabled(true);

    // Build the summary panel.
    QString text = tr("Scan complete: %1 file(s) in %2  (avg %3 s/file)\n")
                       .arg(totalFiles)
                       .arg(FfprobeAnalyzer::formatDuration(elapsedSeconds))
                       .arg(QString::number(m_avgPerFile, 'f', 1));

    // Defective files grouped by error type.
    QList<QString> errOrder;
    QHash<QString, QList<const FileResult*>> byErr;
    for (const FileResult& r : m_results) {
        if (r.error.isEmpty()) continue;
        if (!byErr.contains(r.error)) errOrder.append(r.error);
        byErr[r.error].append(&r);
    }
    std::sort(errOrder.begin(), errOrder.end(),
              [&](const QString& a, const QString& b) { return byErr[a].size() > byErr[b].size(); });

    if (!byErr.isEmpty()) {
        text += tr("\nDEFECTIVE FILES: %1\n").arg(defectiveCount);
        for (const QString& e : errOrder) {
            text += QStringLiteral("  %1 (%2)\n").arg(e).arg(byErr[e].size());
            for (const FileResult* d : byErr[e])
                text += QStringLiteral("    %1  %2/%3\n")
                            .arg(FfprobeAnalyzer::formatDuration(d->duration), d->folder, d->fileName);
        }
    } else {
        text += tr("\nNo defective files found.\n");
    }

    // Bitrate outliers (per folder, recomputed).
    QStringList outliers;
    for (auto it = m_folderRows.constBegin(); it != m_folderRows.constEnd(); ++it) {
        QList<FileResult> valid;
        for (int row : it.value()) {
            const FileResult& r = m_results.at(row);
            if (r.error.isEmpty() && r.calcKbps > 0) valid.append(r);
        }
        if (valid.size() <= 1) continue;
        const int majority = FfprobeAnalyzer::majorityBitrate(valid);
        for (const FileResult& r : valid) {
            if (FfprobeAnalyzer::isOutlier(r.calcKbps, majority, kTolerance)) {
                const int diff = r.calcKbps - majority;
                outliers << QStringLiteral("  %1 kbps (expected %2, %3%4)  %5/%6")
                        .arg(r.calcKbps).arg(majority)
                        .arg(diff > 0 ? "+" : "").arg(diff)
                        .arg(r.folder, r.fileName);
            }
        }
    }
    if (!outliers.isEmpty()) {
        text += tr("\nBITRATE OUTLIERS: %1\n").arg(outliers.size());
        text += outliers.join(QLatin1Char('\n'));
        text += QLatin1Char('\n');
    }

    m_summary->setPlainText(text);

    m_status->setText(tr("Done. %1 file(s), %2 defective, in %3.")
                          .arg(totalFiles).arg(defectiveCount)
                          .arg(FfprobeAnalyzer::formatDuration(elapsedSeconds)));
    statusBar()->showMessage(tr("Scan finished."));
}

// ----------------------------------------------------------------------------
// Export
// ----------------------------------------------------------------------------
void MainWindow::onExportTxt()
{
    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    const QString suggested = QDir(m_scanRoot).filePath(
        QStringLiteral("AudioBitrate_Report_%1.txt").arg(stamp));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save TXT report"), suggested, tr("Text files (*.txt)"));
    if (path.isEmpty()) return;

    ReportWriter::ScanMeta meta;
    meta.rootPath       = m_scanRoot;
    meta.timestamp      = m_scanTimestamp;
    meta.totalFiles     = m_totalFiles;
    meta.folderCount    = m_folderCount;
    meta.elapsedSeconds = m_elapsedSeconds;
    meta.avgPerFile     = m_avgPerFile;
    meta.tolerance      = kTolerance;

    QString err;
    if (ReportWriter::writeTxt(path, m_results, meta, &err))
        statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    else
        QMessageBox::critical(this, tr("Export failed"), err);
}

void MainWindow::onExportCsv()
{
    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    const QString suggested = QDir(m_scanRoot).filePath(
        QStringLiteral("AudioBitrate_Report_%1.csv").arg(stamp));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save CSV report"), suggested, tr("CSV files (*.csv)"));
    if (path.isEmpty()) return;

    QString err;
    if (ReportWriter::writeCsv(path, m_results, &err))
        statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    else
        QMessageBox::critical(this, tr("Export failed"), err);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About Analyze Audio Bitrate"),
        tr("<h3>Analyze Audio Bitrate</h3>"
           "<p><b>Version %1</b></p>"
           "<p>Recursively scans video files, computes the true audio bitrate "
           "from a full ffprobe packet scan, and flags corrupt files and "
           "per-folder bitrate outliers.</p>"
           "<p>Qt6 GUI port of the <b>Analyze-AudioBitrate.ps1</b> PowerShell tool.</p>"
           "<p>Requires FFmpeg's ffprobe.</p>")
        .arg(QApplication::applicationVersion()));
}
