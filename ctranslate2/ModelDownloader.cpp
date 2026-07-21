#include "ModelDownloader.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>
#include <QFileInfo>

ModelDownloader::ModelDownloader(QObject *parent) : QObject(parent) {
    manager = new QNetworkAccessManager(this);
}

void ModelDownloader::downloadModel(const QString& repoId, const QString& destDir) {
    QDir dir;
    if (!dir.exists(destDir)) {
        dir.mkpath(destDir);
    }

    QStringList files = {
        "config.json",
        "model.bin",
        "shared_vocabulary.json",
        "source.spm",
        "target.spm"
    };

    for (const QString& file : files) {
        QString url = QString("https://huggingface.co/%1/resolve/main/%2").arg(repoId).arg(file);
        QString path = QDir(destDir).filePath(file);
        downloadQueue.append(qMakePair(url, path));
    }

    startNextDownload();
}

void ModelDownloader::downloadMoonshineModel(const QString& modelName, const QString& destDir) {
    QDir dir;
    if (!dir.exists(destDir)) {
        dir.mkpath(destDir);
    }

    QString urlBase = "https://download.moonshine.ai/model/" + modelName + "/quantized/";
    QStringList files = {
        "adapter.ort", "cross_kv.ort", "decoder_kv.ort", "decoder_kv_with_attention.ort",
        "encoder.ort", "frontend.ort", "streaming_config.json", "tokenizer.bin"
    };

    for (const QString& file : files) {
        downloadQueue.append(qMakePair(urlBase + file, QDir(destDir).filePath(file)));
    }

    startNextDownload();
}

void ModelDownloader::startNextDownload() {
    if (downloadQueue.isEmpty()) {
        emit downloadFinished();
        return;
    }

    auto pair = downloadQueue.takeFirst();
    QUrl url(pair.first);
    QString filePath = pair.second;

    QNetworkRequest request(url);
    // Follow redirects because HuggingFace uses them for LFS files
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = manager->get(request);

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

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || statusCode >= 400) {
        // Fallback for missing files (e.g. shared_vocabulary.txt instead of .json)
        QFileInfo fileInfo(file->fileName());
        if (fileInfo.fileName() == "shared_vocabulary.json") {
            qDebug() << "Failed to download shared_vocabulary.json, trying shared_vocabulary.txt...";
            QString urlStr = reply->request().url().toString().replace("shared_vocabulary.json", "shared_vocabulary.txt");
            QString path = file->fileName().replace("shared_vocabulary.json", "shared_vocabulary.txt");
            downloadQueue.prepend(qMakePair(urlStr, path));
            file->remove(); // delete the empty/failed file
        } else {
            file->remove(); // remove the empty/error file
            emit errorOccurred("Download failed for " + fileInfo.fileName() + ": " + reply->errorString());
            downloadQueue.clear(); // abort the queue
        }
    } else {
        QFileInfo fileInfo(file->fileName());
        emit fileDownloaded(fileInfo.fileName());
    }

    delete file;
    reply->deleteLater();

    if (downloadQueue.isEmpty() && activeDownloads.isEmpty() && (reply->error() != QNetworkReply::NoError || statusCode >= 400)) {
        // Queue aborted due to error, do not start next download or emit finished.
        return;
    }

    startNextDownload();
}
