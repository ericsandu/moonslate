#include "ModelDownloader.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>
#include <QFileInfo>

ModelDownloader::ModelDownloader(QObject *parent) : QObject(parent) {
    manager = new QNetworkAccessManager(this);
}

void ModelDownloader::downloadModel(const QString& repoId, const QString& destDir) {
    currentRepoId = repoId;
    currentDestDir = destDir;

    QDir dir;
    if (!dir.exists(currentDestDir)) {
        dir.mkpath(currentDestDir);
    }

    // Default files needed for a CTranslate2 Opus-MT model
    filesToDownload = {
        "config.json",
        "model.bin",
        "shared_vocabulary.json",
        "source.spm",
        "target.spm"
    };

    startNextDownload();
}

void ModelDownloader::startNextDownload() {
    if (filesToDownload.isEmpty()) {
        emit downloadFinished();
        return;
    }

    QString filename = filesToDownload.takeFirst();
    QString urlStr = QString("https://huggingface.co/%1/resolve/main/%2").arg(currentRepoId).arg(filename);
    QUrl url(urlStr);

    QNetworkRequest request(url);
    // Follow redirects because HuggingFace uses them for LFS files
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = manager->get(request);

    QString filePath = QDir(currentDestDir).filePath(filename);
    QFile* file = new QFile(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        emit errorOccurred("Could not open file for writing: " + filePath);
        delete file;
        reply->deleteLater();
        return;
    }

    activeDownloads[reply] = file;

    connect(reply, &QNetworkReply::readyRead, this, &ModelDownloader::onReadyRead);
    connect(reply, &QNetworkReply::downloadProgress, this, &ModelDownloader::onDownloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

void ModelDownloader::onReadyRead() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply && activeDownloads.contains(reply)) {
        QFile* file = activeDownloads[reply];
        file->write(reply->readAll());
    }
}

void ModelDownloader::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply && activeDownloads.contains(reply)) {
        QFile* file = activeDownloads[reply];
        QFileInfo fileInfo(file->fileName());
        emit downloadProgress(fileInfo.fileName(), bytesReceived, bytesTotal);
    }
}

void ModelDownloader::onReplyFinished(QNetworkReply* reply) {
    if (!activeDownloads.contains(reply)) return;

    QFile* file = activeDownloads.take(reply);
    file->close();

    if (reply->error() != QNetworkReply::NoError) {
        // Fallback for missing files (e.g. shared_vocabulary.txt instead of .json)
        QFileInfo fileInfo(file->fileName());
        if (fileInfo.fileName() == "shared_vocabulary.json") {
            qDebug() << "Failed to download shared_vocabulary.json, trying shared_vocabulary.txt...";
            filesToDownload.prepend("shared_vocabulary.txt");
            file->remove(); // delete the empty/failed file
        } else {
            emit errorOccurred("Download failed: " + reply->errorString());
        }
    } else {
        QFileInfo fileInfo(file->fileName());
        emit fileDownloaded(fileInfo.fileName());
    }

    delete file;
    reply->deleteLater();

    startNextDownload();
}
