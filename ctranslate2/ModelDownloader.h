#ifndef MODELDOWNLOADER_H
#define MODELDOWNLOADER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QFile>
#include <QDir>
#include <QMap>

class ModelDownloader : public QObject {
    Q_OBJECT
public:
    explicit ModelDownloader(QObject *parent = nullptr);
    
    // e.g., repoId = "michaelfeil/ct2fast-opus-mt-en-de"
    // destDir = "../models/opus-mt-en-de-ct2"
    Q_INVOKABLE void downloadModel(const QString& repoId, const QString& destDir);

signals:
    void downloadProgress(const QString& filename, qint64 bytesReceived, qint64 bytesTotal);
    void fileDownloaded(const QString& filename);
    void downloadFinished();
    void errorOccurred(const QString& errorString);

private slots:
    void onReplyFinished(QNetworkReply* reply);
    void onReadyRead();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    QNetworkAccessManager* manager;
    QString currentDestDir;
    QMap<QNetworkReply*, QFile*> activeDownloads;
    QList<QString> filesToDownload;
    QString currentRepoId;

    void startNextDownload();
};

#endif // MODELDOWNLOADER_H
