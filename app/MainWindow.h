#pragma once

#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QLabel>
#include <QString>
#include <QList>

#include "LangConfig.h"
#include "LivePipelineWorker.h"
#include "AudioPlayer.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    QTextEdit* englishLog;
    QTextEdit* germanLog;
    QPushButton* toggleBtn;
    QToolButton* settingsBtn;
    QMenu* settingsMenu;
    QLabel* deLabel;
    
    QString moonPath;
    LivePipelineWorker* worker = nullptr;
    AudioPlayer* player = nullptr;
    
    QList<LangConfig> supportedLanguages;

    explicit MainWindow(QString mPath);

signals:
    void recordingToggled(bool isRecording);

public slots:
    void appendTranscript(const QString& original, const QString& translated, const QString& execTime);
    void switchLanguage(const LangConfig& lang);
    void startPipeline(const LangConfig& lang, const QString& modelDir);
    void onToggle(bool checked);
};
