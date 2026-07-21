#include "MainWindow.h"
#include "ModelDownloader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QActionGroup>
#include <QAction>
#include <QTime>
#include <QScrollBar>
#include <QDir>
#include <QApplication>

MainWindow::MainWindow(QString mPath) : moonPath(mPath) {
    supportedLanguages = {
        {"French", "michaelfeil/ct2fast-opus-mt-en-fr", "piper_fr_FR-upmc-medium", "fr"},
        {"German", "michaelfeil/ct2fast-opus-mt-en-de", "piper_de_DE-thorsten-medium", "de"},
        {"Spanish", "michaelfeil/ct2fast-opus-mt-en-es", "piper_es_ES-davefx-medium", "es"}
    };

    setWindowTitle("Moonslate Live Translator");
    resize(1000, 600);
    
    player = new AudioPlayer();

    QWidget* central = new QWidget();
    setCentralWidget(central);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* title = new QLabel("Moonslate - Live Translator");
    title->setStyleSheet("font-size: 28px; font-weight: bold; color: #E0E0E0;");
    headerLayout->addWidget(title);
    
    headerLayout->addStretch();
    
    settingsBtn = new QToolButton();
    settingsBtn->setText("⚙️");
    settingsBtn->setStyleSheet("QToolButton { background: transparent; border: none; font-size: 24px; color: white; padding: 5px; }");
    settingsBtn->setPopupMode(QToolButton::InstantPopup);
    settingsMenu = new QMenu(settingsBtn);
    settingsMenu->setStyleSheet("QMenu { background-color: #2E2E2E; color: white; font-size: 14px; } QMenu::item:selected { background-color: #4CAF50; }");
    
    QActionGroup* langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);
    for (const auto& lang : supportedLanguages) {
        QAction* act = new QAction(lang.name, this);
        act->setCheckable(true);
        if (lang.name == "German") act->setChecked(true);
        langGroup->addAction(act);
        settingsMenu->addAction(act);
        connect(act, &QAction::triggered, [this, lang]() {
            switchLanguage(lang);
        });
    }
    settingsBtn->setMenu(settingsMenu);
    headerLayout->addWidget(settingsBtn);
    
    toggleBtn = new QPushButton("Loading...");
    toggleBtn->setEnabled(false);
    toggleBtn->setCheckable(true);
    toggleBtn->setChecked(true);
    toggleBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #555555; color: white;"
        "  font-weight: bold; font-size: 16px;"
        "  border-radius: 8px; padding: 10px 20px;"
        "}"
    );
    headerLayout->addWidget(toggleBtn);
    mainLayout->addLayout(headerLayout);

    QHBoxLayout* boxesLayout = new QHBoxLayout();
    
    QVBoxLayout* enLayout = new QVBoxLayout();
    QLabel* enLabel = new QLabel("English (Original)");
    enLabel->setStyleSheet("font-size: 16px; color: #9E9E9E; margin-bottom: 5px;");
    enLayout->addWidget(enLabel);
    englishLog = new QTextEdit();
    englishLog->setReadOnly(true);
    englishLog->setStyleSheet(
        "QTextEdit {"
        "  background-color: #1E1E1E; color: #F5F5F5;"
        "  font-size: 18px; font-family: 'Inter', 'Segoe UI', sans-serif;"
        "  border: 1px solid #333333; border-radius: 8px; padding: 10px;"
        "}"
    );
    enLayout->addWidget(englishLog);
    boxesLayout->addLayout(enLayout);

    QVBoxLayout* deLayout = new QVBoxLayout();
    deLabel = new QLabel("Translation");
    deLabel->setStyleSheet("font-size: 16px; color: #4CAF50; margin-bottom: 5px; font-weight: bold;");
    deLayout->addWidget(deLabel);
    germanLog = new QTextEdit();
    germanLog->setReadOnly(true);
    germanLog->setStyleSheet(
        "QTextEdit {"
        "  background-color: #1A2E1A; color: #F5F5F5;"
        "  font-size: 18px; font-family: 'Inter', 'Segoe UI', sans-serif;"
        "  border: 1px solid #2E5A2E; border-radius: 8px; padding: 10px;"
        "}"
    );
    deLayout->addWidget(germanLog);
    boxesLayout->addLayout(deLayout);

    mainLayout->addLayout(boxesLayout);
    setStyleSheet("QMainWindow { background-color: #121212; }");

    connect(toggleBtn, &QPushButton::toggled, this, &MainWindow::onToggle);
}

void MainWindow::appendTranscript(const QString& original, const QString& translated, const QString& execTime) {
    QString timeStr = QTime::currentTime().toString("hh:mm:ss");
    
    QString enHtml = QString(
        "<table width='100%' cellpadding='0' cellspacing='0'>"
        "<tr>"
        "<td align='left' valign='middle' style='padding-bottom: 5px;'>%2</td>"
        "<td align='right' valign='middle' style='color: #666666; font-size: 11px; padding-left: 15px; padding-bottom: 5px;'>%1</td>"
        "</tr>"
        "<tr>"
        "<td colspan='2'>"
        "<table width='100%' cellpadding='0' cellspacing='0'><tr>"
        "<td width='2%'></td><td width='96%' style='border-top: 1px solid #2A2A2A; font-size: 1px;'></td><td width='2%'></td>"
        "</tr></table>"
        "</td>"
        "</tr>"
        "</table>"
    ).arg(timeStr, original.toHtmlEscaped());
    
    QString deHtml = QString(
        "<table width='100%' cellpadding='0' cellspacing='0'>"
        "<tr>"
        "<td align='left' valign='middle' style='padding-bottom: 5px;'>%2</td>"
        "<td align='right' valign='middle' style='color: #4CAF50; font-size: 11px; padding-left: 15px; padding-bottom: 5px;'>%1</td>"
        "</tr>"
        "<tr>"
        "<td colspan='2'>"
        "<table width='100%' cellpadding='0' cellspacing='0'><tr>"
        "<td width='2%'></td><td width='96%' style='border-top: 1px solid #233A23; font-size: 1px;'></td><td width='2%'></td>"
        "</tr></table>"
        "</td>"
        "</tr>"
        "</table>"
    ).arg(execTime, translated.toHtmlEscaped());

    englishLog->append(enHtml);
    germanLog->append(deHtml);
    
    englishLog->verticalScrollBar()->setValue(englishLog->verticalScrollBar()->maximum());
    germanLog->verticalScrollBar()->setValue(germanLog->verticalScrollBar()->maximum());
}

void MainWindow::switchLanguage(const LangConfig& lang) {
    toggleBtn->setEnabled(false);
    toggleBtn->setText("Loading...");
    toggleBtn->setStyleSheet("QPushButton { background-color: #555555; color: white; font-weight: bold; font-size: 16px; border-radius: 8px; padding: 10px 20px; }");
    
    if (worker) {
        worker->quit();
        worker->wait();
        worker->deleteLater();
        worker = nullptr;
    }

    englishLog->clear();
    germanLog->clear();

    deLabel->setText(lang.name + " (Translation)");

    QString modelDir = "../models/opus-mt-en-" + lang.langCode + "-ct2";
    if (!QDir(modelDir).exists()) {
        toggleBtn->setText("Downloading " + lang.name + "...");
        ModelDownloader* downloader = new ModelDownloader(this);
        connect(downloader, &ModelDownloader::downloadFinished, [this, downloader, lang, modelDir]() {
            downloader->deleteLater();
            startPipeline(lang, modelDir);
        });
        downloader->downloadModel(lang.opusRepo, modelDir);
    } else {
        startPipeline(lang, modelDir);
    }
}

void MainWindow::startPipeline(const LangConfig& lang, const QString& modelDir) {
    worker = new LivePipelineWorker(moonPath, modelDir, lang.piperVoice, lang.langCode);
    connect(this, &MainWindow::recordingToggled, worker, &LivePipelineWorker::setRecording);
    connect(worker, &LivePipelineWorker::transcriptUpdated, this, &MainWindow::appendTranscript);
    connect(worker, &LivePipelineWorker::chunkReady, player, &AudioPlayer::onChunkReady, Qt::QueuedConnection);
    connect(worker, &LivePipelineWorker::pipelineReady, this, [this]() {
        toggleBtn->setEnabled(true);
        if (toggleBtn->isChecked()) {
            toggleBtn->setText("Recording: ON");
            toggleBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; font-size: 16px; border-radius: 8px; padding: 10px 20px; }");
        } else {
            toggleBtn->setText("Recording: OFF");
            toggleBtn->setStyleSheet("QPushButton { background-color: #F44336; color: white; font-weight: bold; font-size: 16px; border-radius: 8px; padding: 10px 20px; }");
        }
        if (worker) worker->setRecording(toggleBtn->isChecked());
    });
    worker->start();
}

void MainWindow::onToggle(bool checked) {
    if (!toggleBtn->isEnabled()) return;
    if (checked) {
        toggleBtn->setText("Recording: ON");
        toggleBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: #4CAF50; color: white;"
            "  font-weight: bold; font-size: 16px;"
            "  border-radius: 8px; padding: 10px 20px;"
            "}"
        );
    } else {
        toggleBtn->setText("Recording: OFF");
        toggleBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: #F44336; color: white;"
            "  font-weight: bold; font-size: 16px;"
            "  border-radius: 8px; padding: 10px 20px;"
            "}"
        );
    }
    emit recordingToggled(checked);
}
