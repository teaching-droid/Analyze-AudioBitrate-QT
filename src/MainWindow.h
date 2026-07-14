// ============================================================================
// MainWindow - the GUI shell for the audio bitrate scanner.
// ============================================================================
#pragma once

#include <QMainWindow>
#include <QList>
#include <QHash>
#include "FfprobeAnalyzer.h"

class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QGroupBox;
class QMenu;
class QAction;
class QActionGroup;
class QThread;
class QTranslator;
class ScanWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent* event) override;   // catches QEvent::LanguageChange

private slots:
    void onBrowseFolder();
    void onBrowseFfprobe();
    void onScan();
    void onCancel();

    void onScanStarted(int totalFiles, int folderCount);
    void onProgress(int done, int total, double avgPerFile,
                    double etaSeconds, const QString& currentFile);
    void onFileScanned(const FileResult& result);
    void onFolderCompleted(const QString& folder, int majorityKbps);
    void onScanFinished(int totalFiles, int defectiveCount, double elapsedSeconds);

    void onExportTxt();
    void onExportCsv();
    void onAbout();

private:
    void buildUi();
    void createLanguageMenu();
    void retranslateUi();                        // (re)applies every visible string
    void switchLanguage(const QString& code);    // "en" | "de" | "ja" | "it"
    QString defaultFfprobePath() const;
    void setScanning(bool scanning);
    void addResultRow(const FileResult& r);
    void styleErrorRow(int row, const QString& error);
    void teardownThread();

    // --- Inputs ---
    QLabel*      m_dirLabel     = nullptr;
    QLabel*      m_ffprobeLabel = nullptr;
    QLineEdit*   m_dirEdit      = nullptr;
    QLineEdit*   m_ffprobeEdit  = nullptr;
    QPushButton* m_browseDir    = nullptr;
    QPushButton* m_browseFf     = nullptr;
    QPushButton* m_scanBtn      = nullptr;
    QPushButton* m_cancelBtn    = nullptr;
    QPushButton* m_exportTxtBtn = nullptr;
    QPushButton* m_exportCsvBtn = nullptr;

    // --- Progress / results ---
    QProgressBar*   m_progress   = nullptr;
    QLabel*         m_status     = nullptr;
    QTableWidget*   m_table      = nullptr;
    QGroupBox*      m_summaryBox = nullptr;
    QPlainTextEdit* m_summary    = nullptr;

    // --- Menus / i18n ---
    QMenu*        m_fileMenu = nullptr;
    QMenu*        m_helpMenu = nullptr;
    QMenu*        m_langMenu = nullptr;
    QAction*      m_exitAct  = nullptr;
    QAction*      m_aboutAct = nullptr;
    QActionGroup* m_langGroup = nullptr;
    QTranslator*  m_appTranslator = nullptr;
    QTranslator*  m_qtTranslator  = nullptr;
    QString       m_language = QStringLiteral("en");

    // --- Scan thread ---
    QThread*    m_thread = nullptr;
    ScanWorker* m_worker = nullptr;

    // --- Collected data (row index == index into m_results) ---
    QList<FileResult>          m_results;
    QHash<QString, QList<int>> m_folderRows;   // folder -> table rows
    QHash<QString, int>        m_folderMajority;

    // --- Scan meta, captured for the report header. ---
    QString m_scanRoot;
    QString m_scanTimestamp;
    int     m_totalFiles = 0;
    int     m_folderCount = 0;
    double  m_elapsedSeconds = 0.0;
    double  m_avgPerFile = 0.0;

    // Status-label content is re-derived on language change from these.
    bool    m_scanDone = false;
    int     m_lastDefective = 0;

    static constexpr double kTolerance = 0.10;
};
