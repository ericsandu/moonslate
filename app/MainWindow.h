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
    
    QString currentMoonshineModelName;
    LangConfig currentLang;
    
    LivePipelineWorker* worker = nullptr;
    AudioPlayer* player = nullptr;
    
    QList<LangConfig> supportedLanguages;
    QList<QString> supportedMoonshineModels;

    explicit MainWindow();

signals:
    void recordingToggled(bool isRecording);

public slots:
    void appendTranscript(const QString& original, const QString& translated, const QString& execTime);
    void switchLanguage(const LangConfig& lang);
    void switchMoonshineModel(const QString& modelName);
    void checkAndStartPipeline();
    void onToggle(bool checked);
};
