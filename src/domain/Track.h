#pragma once
#include <QString>
#include <QVariantMap>

struct Track
{
    QString key;
    QString hash;
    QString albumAudioId;
    QString title;
    QString artist;
    QVariantList artists;
    QString albumId;
    QString album;
    QString coverUrl;
    QString fileId;
    qint64 durationMs = -1;

    QString durationText() const;
    QVariantMap toMap() const;
    static Track fromMap(const QVariantMap &map);
};
Q_DECLARE_METATYPE(Track)
