#include "Track.h"

QString Track::durationText() const
{
    if (durationMs < 0)
        return {};
    const qint64 seconds = durationMs / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
QVariantMap Track::toMap() const
{
    return {{"key", key},
            {"hash", hash},
            {"albumAudioId", albumAudioId},
            {"title", title},
            {"artist", artist},
            {"artists", artists},
            {"albumId", albumId},
            {"album", album},
            {"coverUrl", coverUrl},
            {"fileId", fileId},
            {"durationMs", durationMs},
            {"durationText", durationText()}};
}
Track Track::fromMap(const QVariantMap &map)
{
    Track track;
    track.key = map.value("key").toString();
    track.hash = map.value("hash").toString();
    track.albumAudioId = map.value("albumAudioId").toString();
    track.title = map.value("title").toString();
    track.artist = map.value("artist").toString();
    track.artists = map.value("artists").toList();
    track.albumId = map.value("albumId").toString();
    track.album = map.value("album").toString();
    track.coverUrl = map.value("coverUrl").toString();
    track.fileId = map.value("fileId").toString();
    track.durationMs = map.value("durationMs", -1).toLongLong();
    return track;
}
