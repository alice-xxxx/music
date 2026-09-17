#include "KuGouApi.h"
#include "platform/CredentialCookieJar.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace
{
QString stringField(const QJsonObject &o, std::initializer_list<const char *> names);
struct EndpointError
{
    QString code;
    QString message;
};
EndpointError endpointError(const ApiClient::Response &response, bool searchEndpoint = false)
{
    if (!response.errorCode.isEmpty())
        return {response.errorCode, response.errorMessage};
    const QJsonObject root = response.json.object();
    const int businessCode = root.value(QStringLiteral("error_code")).toInt();
    const bool failed = businessCode != 0 || root.value(QStringLiteral("status")).toInt(1) == 0;
    if (failed)
    {
        QString message = stringField(root, {"message", "error", "msg"});
        if (message.isEmpty())
            message = QStringLiteral("服务业务错误 %1").arg(businessCode);
        return {searchEndpoint && businessCode == 152 ? QStringLiteral("AuthRequired")
                                                      : QStringLiteral("BusinessError"),
                message};
    }
    return {response.errorCode, response.errorMessage};
}
QString stringField(const QJsonObject &o, std::initializer_list<const char *> names)
{
    for (const char *name : names)
    {
        const auto value = o.value(QLatin1String(name));
        if (value.isString())
            return value.toString();
        if (value.isDouble())
            return QString::number(value.toInteger());
    }
    return {};
}
QString normalizedCover(QString cover)
{
    QUrl url(cover);
    if (url.scheme() == QStringLiteral("http") &&
        (url.host().endsWith(QStringLiteral(".kugou.com")) ||
         url.host().endsWith(QStringLiteral(".kgimg.com"))))
        url.setScheme(QStringLiteral("https"));
    return url.isValid() && !url.host().isEmpty() && url.scheme() == QStringLiteral("https") &&
                   url.userInfo().isEmpty()
               ? url.toString().replace(QStringLiteral("%7Bsize%7D"), QStringLiteral("{size}"))
               : QString{};
}
QList<Track> parseTracks(const QJsonObject &root)
{
    QJsonArray rows;
    const auto data = root.value(QStringLiteral("data"));
    const QJsonObject container = data.isObject() ? data.toObject() : root;
    for (const auto &key : {"items", "lists", "info", "song_list", "songs", "list"})
    {
        rows = container.value(QLatin1String(key)).toArray();
        if (!rows.isEmpty())
            break;
    }
    if (rows.isEmpty() && data.isArray())
        rows = data.toArray();
    QList<Track> tracks;
    for (const auto &value : rows)
    {
        const auto row = value.toObject();
        if (row.value(QStringLiteral("base")).isObject() &&
            row.value(QStringLiteral("audio_info")).isObject())
        {
            const auto base = row.value(QStringLiteral("base")).toObject();
            const auto audio = row.value(QStringLiteral("audio_info")).toObject();
            const auto album = row.value(QStringLiteral("album_info")).toObject();
            Track track;
            track.hash = stringField(audio, {"hash_128", "hash"});
            track.albumAudioId = stringField(base, {"album_audio_id"});
            track.title = stringField(base, {"audio_name"});
            track.artist = stringField(base, {"author_name"});
            track.albumId = stringField(base, {"album_id"});
            track.album = stringField(album, {"album_name"});
            track.durationMs = audio.value(QStringLiteral("duration")).toInteger(-1);
            track.coverUrl = normalizedCover(album.value(QStringLiteral("cover")).toString());
            track.key =
                QStringLiteral("kugou:") + track.hash + QLatin1Char(':') + track.albumAudioId;
            if (!track.hash.isEmpty() && !track.title.isEmpty())
                tracks.append(track);
            continue;
        }
        // Search payloads differ across KuGouMusicApi/upstream versions. Prefer
        // the ordinary 128-kbit hash, then fall back to another playable hash.
        const QString hash = stringField(row, {"FileHash", "filehash", "hash", "audio_hash",
                                               "HQFileHash", "hqfilehash", "SQFileHash",
                                               "sqfilehash", "ResFileHash", "resfilehash"});
        const QString title =
            stringField(row, {"SongName", "songname", "song_name", "name", "OriSongName",
                              "audio_name", "FileName", "filename"});
        if (hash.isEmpty() || title.isEmpty())
            continue;
        const QString albumAudioId =
            stringField(row, {"MixSongID", "mixsongid", "mixsong_id", "album_audio_id",
                              "AlbumAudioId", "audio_id"});
        Track track;
        track.key = QStringLiteral("kugou:") + hash + QLatin1Char(':') + albumAudioId;
        track.hash = hash;
        track.albumAudioId = albumAudioId;
        track.title = title;
        track.fileId = stringField(row, {"fileid"});
        track.artist = stringField(row, {"SingerName", "singername", "author_name", "singer_name"});
        if (row.value("singerinfo").isArray())
        {
            QStringList names;
            for (const auto &singerValue : row.value("singerinfo").toArray())
            {
                const auto singer = singerValue.toObject();
                const auto name = stringField(singer, {"name"});
                const auto id = stringField(singer, {"id"});
                if (!name.isEmpty())
                    names.append(name);
                if (!id.isEmpty() && !name.isEmpty())
                    track.artists.append(QVariantMap{{"id", id}, {"name", name}});
            }
            track.artist = names.join(QStringLiteral("、"));
            const auto prefix = track.artist + QStringLiteral(" - ");
            if (!track.artist.isEmpty() && track.title.startsWith(prefix))
                track.title.remove(0, prefix.size());
            track.albumId = stringField(row, {"album_id"});
            track.album = row.value("albuminfo").toObject().value("name").toString();
            track.coverUrl = normalizedCover(row.value("cover").toString());
        }
        const QString duration = stringField(row, {"Duration", "duration", "timelen"});
        track.durationMs =
            duration.isEmpty() ? -1 : duration.toLongLong() * (row.contains("timelen") ? 1 : 1000);
        tracks.append(track);
    }
    return tracks;
}
QString findMediaUrl(const QJsonValue &value, int depth = 0)
{
    if (depth > 4)
        return {};
    if (value.isString())
    {
        const QString text = value.toString();
        if (text.startsWith(QStringLiteral("http://")) ||
            text.startsWith(QStringLiteral("https://")))
            return text;
    }
    if (value.isArray())
    {
        for (const auto &item : value.toArray())
            if (const QString url = findMediaUrl(item, depth + 1); !url.isEmpty())
                return url;
    }
    if (value.isObject())
    {
        const auto object = value.toObject();
        for (const auto &key : {"url", "play_url", "playUrl", "backup_url"})
            if (const QString url = findMediaUrl(object.value(QLatin1String(key)), depth + 1);
                !url.isEmpty())
                return url;
        for (const auto &key : {"data", "urls", "backup"})
            if (const QString url = findMediaUrl(object.value(QLatin1String(key)), depth + 1);
                !url.isEmpty())
                return url;
    }
    return {};
}

} // namespace

KuGouApi::KuGouApi(QUrl serviceBase, QObject *parent)
    : QObject(parent), m_client(serviceBase, this), m_serviceBase(std::move(serviceBase))
{
    auto *jar = m_client.cookieJar();
    m_registrationState = jar->hasDeviceRegistration() ? RegistrationState::Registered
                                                       : RegistrationState::Unregistered;
}

void KuGouApi::setServiceBase(QUrl serviceBase)
{
    if (m_serviceBase == serviceBase)
        return;
    cancelLogin();
    m_client.setServiceBase(serviceBase);
    m_serviceBase = std::move(serviceBase);
    m_registrationState = m_client.cookieJar()->hasDeviceRegistration()
                              ? RegistrationState::Registered
                              : RegistrationState::Unregistered;
    m_authenticated = m_client.cookieJar()->hasLoginSession();
    m_pendingResolves.clear();
    emit sessionInvalidated();
}

QString KuGouApi::storageError() const
{
    return m_client.cookieJar()->storageError();
}

void KuGouApi::search(const QString &keywords, SearchCallback callback, int page)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keywords"), keywords);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("song"));
    m_client.get(QStringLiteral("/search"), query,
                 [this, callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response, true); !error.code.isEmpty())
                     {
                         callback({}, error.code, error.message);
                         return;
                     }
                     const auto document = response.json;
                     if (!document.isObject())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("服务返回的 JSON 无法解析"));
                         return;
                     }
                     const QJsonObject root = document.object();
                     const auto tracks = parseTracks(root);
                     const QJsonObject data = root.value(QStringLiteral("data")).toObject();
                     if (!data.value(QStringLiteral("lists")).isArray() &&
                         !data.value(QStringLiteral("info")).isArray())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("搜索响应缺少歌曲列表"));
                         return;
                     }
                     const bool hadRows =
                         !data.value(QStringLiteral("lists")).toArray().isEmpty() ||
                         !data.value(QStringLiteral("info")).toArray().isEmpty();
                     if (hadRows && tracks.isEmpty())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("搜索结果缺少必要的歌曲 ID 或标题"));
                         return;
                     }
                     callback(std::move(tracks), {}, {});
                 });
}

void KuGouApi::trackMetadata(Track track, std::function<void(Track, QString)> callback)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), track.hash);
    query.addQueryItem(QStringLiteral("album_audio_id"), track.albumAudioId);
    query.addQueryItem(QStringLiteral("album_id"), track.albumId);
    // The service returns one data entry per requested hash without echoing that hash.
    // A single-track request keeps the association explicit rather than matching UI row numbers.
    m_client.get(QStringLiteral("/images"), query,
                 [track = std::move(track),
                  callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.message);
                         return;
                     }
                     const auto data =
                         response.json.object().value(QStringLiteral("data")).toArray();
                     if (data.size() != 1 || !data.first().isObject())
                     {
                         callback({}, QStringLiteral("图片响应结构不匹配"));
                         return;
                     }
                     const auto item = data.first().toObject();
                     const auto albums = item.value(QStringLiteral("album")).toArray();
                     if (!albums.isEmpty())
                     {
                         const auto album = albums.first().toObject();
                         track.albumId = stringField(album, {"album_id"});
                         track.album = stringField(album, {"album_name"});
                         track.coverUrl = normalizedCover(
                             album.value(QStringLiteral("sizable_cover")).toString());
                     }
                     track.artists.clear();
                     for (const auto &authorValue : item.value(QStringLiteral("author")).toArray())
                     {
                         const auto author = authorValue.toObject();
                         const auto id = stringField(author, {"author_id"});
                         const auto name = stringField(author, {"author_name"});
                         if (!id.isEmpty() && !name.isEmpty())
                             track.artists.append(QVariantMap{{"id", id}, {"name", name}});
                     }
                     if (track.artist.isEmpty())
                     {
                         QStringList artists;
                         for (const auto &author : item.value(QStringLiteral("author")).toArray())
                             artists.append(
                                 author.toObject().value(QStringLiteral("author_name")).toString());
                         track.artist = artists.join(QStringLiteral(" / "));
                     }
                     callback(std::move(track), {});
                 });
}

void KuGouApi::resolveSong(const QString &hash, const QString &albumAudioId,
                           std::function<void(QUrl, QString)> callback, const QString &quality)
{
    const auto requestedQuality =
        quality == "320" || quality == "flac" ? quality : QStringLiteral("128");
    if (m_registrationState != RegistrationState::Registered)
    {
        m_pendingResolves.push_back({hash, albumAudioId, std::move(callback), requestedQuality});
        if (m_registrationState == RegistrationState::Registering)
            return;
        m_registrationState = RegistrationState::Registering;
        m_client.get(
            QStringLiteral("/register/dev"), {},
            [this](ApiClient::Response response) mutable
            {
                const auto registrationError = endpointError(response);
                const bool registered = registrationError.code.isEmpty() &&
                                        m_client.cookieJar()->hasDeviceRegistration();
                m_registrationState =
                    registered ? RegistrationState::Registered : RegistrationState::Failed;
                auto pending = std::move(m_pendingResolves);
                m_pendingResolves.clear();
                for (auto &request : pending)
                {
                    if (registered)
                        resolveSongAfterRegistration(request.hash, request.albumAudioId,
                                                     std::move(request.callback), request.quality);
                    else
                        request.callback({}, registrationError.message.isEmpty()
                                                 ? QStringLiteral("设备注册失败，未返回有效 dfid")
                                                 : registrationError.message);
                }
            });
        return;
    }
    resolveSongAfterRegistration(hash, albumAudioId, std::move(callback), requestedQuality);
}

void KuGouApi::resolveSongAfterRegistration(const QString &hash, const QString &albumAudioId,
                                            std::function<void(QUrl, QString)> callback,
                                            const QString &quality)
{
    // The deployed API version does not expose /user/verify. The documented
    // /song/url endpoint consumes the already-restored login cookies directly.
    // Do not probe or refresh login state here: playback must never cause a
    // repeated login request.
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), hash);
    query.addQueryItem(QStringLiteral("quality"), quality);
    if (!albumAudioId.isEmpty())
    {
        query.addQueryItem(QStringLiteral("album_audio_id"), albumAudioId);
    }
    m_client.get(
        QStringLiteral("/song/url"), query,
        [callback = std::move(callback)](ApiClient::Response response) mutable
        {
            if (const auto error = endpointError(response); !error.code.isEmpty())
            {
                callback({}, error.message);
                return;
            }
            const auto root = response.json.object();
            const auto data = root.value(QStringLiteral("data"));
            const QString value = findMediaUrl(data.isUndefined() ? QJsonValue(root) : data);
            const QUrl media(value);
            if (!media.isValid() || media.host().isEmpty() || !media.userInfo().isEmpty() ||
                (media.scheme() != QStringLiteral("https") &&
                 media.scheme() != QStringLiteral("http")))
            {
                callback({},
                         QStringLiteral(
                             "服务没有返回播放地址；设备注册可能无效，或歌曲需要登录/会员权限"));
                return;
            }
            callback(media, {});
        });
}

void KuGouApi::searchEntries(const QString &keywords, const QString &kind,
                             std::function<void(QVariantList, QString, QString)> callback, int page)
{
    const QString type = kind == QStringLiteral("artist")     ? QStringLiteral("author")
                         : kind == QStringLiteral("playlist") ? QStringLiteral("special")
                                                              : QStringLiteral("album");
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keywords"), keywords);
    query.addQueryItem(QStringLiteral("type"), type);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    m_client.get(
        QStringLiteral("/search"), query,
        [kind, callback = std::move(callback)](ApiClient::Response response)
        {
            if (const auto error = endpointError(response, true); !error.code.isEmpty())
            {
                callback({}, error.code, error.message);
                return;
            }
            const auto rows = response.json.object().value("data").toObject().value("lists");
            if (!rows.isArray())
            {
                callback({}, QStringLiteral("SchemaMismatch"), QStringLiteral("搜索响应缺少列表"));
                return;
            }
            QVariantList entries;
            for (const auto &value : rows.toArray())
            {
                const auto row = value.toObject();
                QString id, title, cover, subtitle;
                if (kind == QStringLiteral("album"))
                {
                    id = stringField(row, {"albumid"});
                    title = stringField(row, {"albumname"});
                    cover = stringField(row, {"img"});
                    subtitle = stringField(row, {"singer"});
                }
                else if (kind == QStringLiteral("artist"))
                {
                    id = stringField(row, {"AuthorId"});
                    title = stringField(row, {"AuthorName"});
                    cover = stringField(row, {"Avatar"});
                    subtitle = QStringLiteral("%1 首歌曲").arg(stringField(row, {"AudioCount"}));
                }
                else
                {
                    id = stringField(row, {"gid"});
                    title = stringField(row, {"specialname"});
                    cover = stringField(row, {"img"});
                    subtitle = QStringLiteral("%1 首歌曲").arg(stringField(row, {"song_count"}));
                }
                if (id.isEmpty() || title.isEmpty())
                {
                    callback({}, QStringLiteral("SchemaMismatch"),
                             QStringLiteral("搜索结果缺少有效身份或标题"));
                    return;
                }
                entries.append(
                    QVariantMap{{"kind", kind},
                                {"id", id},
                                {"title", title},
                                {"name", title},
                                {"subtitle", subtitle},
                                {"coverUrl", normalizedCover(cover)},
                                {"albumId", kind == QStringLiteral("album") ? id : QString{}},
                                {"album", kind == QStringLiteral("album") ? title : QString{}}});
            }
            callback(entries, {}, {});
        });
}
void KuGouApi::artistDetail(const QString &artistId,
                            std::function<void(QVariantMap, QString)> callback)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), artistId);
    m_client.get(QStringLiteral("/artist/detail"), query,
                 [artistId, callback = std::move(callback)](ApiClient::Response response)
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.message);
                         return;
                     }
                     const auto data = response.json.object().value("data").toObject();
                     if (stringField(data, {"author_id"}) != artistId ||
                         stringField(data, {"author_name"}).isEmpty())
                     {
                         callback({}, QStringLiteral("歌手详情缺少有效身份信息"));
                         return;
                     }
                     callback({{"id", artistId},
                               {"title", stringField(data, {"author_name"})},
                               {"cover", normalizedCover(stringField(data, {"sizable_avatar"}))},
                               {"description", stringField(data, {"intro"})}},
                              {});
                 });
}
void KuGouApi::artistTracks(const QString &artistId, SearchCallback callback, int page)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), artistId);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    m_client.get(QStringLiteral("/artist/audios"), query,
                 [callback = std::move(callback)](ApiClient::Response response)
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.code, error.message);
                         return;
                     }
                     const auto data = response.json.object().value("data");
                     if (!data.isArray())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("歌手歌曲响应缺少列表"));
                         return;
                     }
                     QList<Track> tracks;
                     for (const auto &value : data.toArray())
                     {
                         const auto row = value.toObject();
                         Track track;
                         track.hash = stringField(row, {"hash_128", "hash"});
                         track.albumAudioId = stringField(row, {"album_audio_id"});
                         track.key =
                             QStringLiteral("kugou:") + track.hash + ':' + track.albumAudioId;
                         track.title = stringField(row, {"audio_name"});
                         track.artist = stringField(row, {"author_name"});
                         track.albumId = stringField(row, {"album_id"});
                         track.album = stringField(row, {"album_name"});
                         track.durationMs = row.value("timelength").toInteger(-1);
                         track.coverUrl = normalizedCover(
                             row.value("trans_param").toObject().value("union_cover").toString());
                         if (track.hash.isEmpty() || track.title.isEmpty())
                         {
                             callback({}, QStringLiteral("SchemaMismatch"),
                                      QStringLiteral("歌手歌曲缺少有效身份"));
                             return;
                         }
                         tracks.append(track);
                     }
                     callback(tracks, {}, {});
                 });
}
void KuGouApi::lyrics(const QString &hash, std::function<void(QString, QString)> callback)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), hash);
    query.addQueryItem(QStringLiteral("man"), QStringLiteral("no"));
    m_client.get(
        QStringLiteral("/search/lyric"), query,
        [this, callback = std::move(callback)](ApiClient::Response response) mutable
        {
            if (const auto error = endpointError(response); !error.code.isEmpty())
            {
                callback({}, error.message);
                return;
            }
            const QJsonObject root = response.json.object();
            QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
            if (candidates.isEmpty())
                candidates = root.value(QStringLiteral("data"))
                                 .toObject()
                                 .value(QStringLiteral("candidates"))
                                 .toArray();
            if (candidates.isEmpty())
            {
                callback({}, QStringLiteral("暂无歌词"));
                return;
            }
            const QJsonObject candidate = candidates.first().toObject();
            const QString id = stringField(candidate, {"id"});
            const QString accessKey = stringField(candidate, {"accesskey"});
            if (id.isEmpty() || accessKey.isEmpty())
            {
                callback({}, QStringLiteral("歌词搜索响应缺少 data.candidates[0].id/accesskey"));
                return;
            }
            QUrlQuery detail;
            detail.addQueryItem(QStringLiteral("id"), id);
            detail.addQueryItem(QStringLiteral("accesskey"), accessKey);
            detail.addQueryItem(QStringLiteral("fmt"), QStringLiteral("lrc"));
            detail.addQueryItem(QStringLiteral("decode"), QStringLiteral("true"));
            m_client.get(
                QStringLiteral("/lyric"), detail,
                [callback = std::move(callback)](ApiClient::Response lyricResponse) mutable
                {
                    if (const auto error = endpointError(lyricResponse); !error.code.isEmpty())
                    {
                        callback({}, error.message);
                        return;
                    }
                    const QJsonObject lyricRoot = lyricResponse.json.object();
                    const QJsonObject data = lyricRoot.value(QStringLiteral("data")).toObject();
                    QString text = stringField(data, {"decodeContent", "content"});
                    if (text.isEmpty())
                        text = stringField(lyricRoot, {"decodeContent", "content"});
                    callback(text, text.isEmpty() ? QStringLiteral("歌词内容响应缺少 content")
                                                  : QString{});
                });
        });
}

void KuGouApi::userPlaylists(std::function<void(QList<Playlist>, QString, QString)> callback,
                             int page)
{
    const QString userId = m_client.cookieJar()->userId();
    if (userId.isEmpty())
    {
        callback({}, QStringLiteral("AuthRequired"), QStringLiteral("登录后可查看歌单"));
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userid"), userId);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    m_client.get(
        QStringLiteral("/user/playlist"), query,
        [callback = std::move(callback)](ApiClient::Response response) mutable
        {
            if (const auto error = endpointError(response); !error.code.isEmpty())
            {
                callback({}, error.code, error.message);
                return;
            }
            const QJsonObject root = response.json.object();
            const QJsonValue data = root.value(QStringLiteral("data"));
            if (!data.isArray() && !data.toObject().value(QStringLiteral("info")).isArray() &&
                !data.toObject().value(QStringLiteral("lists")).isArray())
            {
                callback({}, QStringLiteral("SchemaMismatch"),
                         QStringLiteral("账号响应缺少歌单列表"));
                return;
            }
            QJsonArray rows = data.isArray()
                                  ? data.toArray()
                                  : data.toObject().value(QStringLiteral("info")).toArray();
            if (rows.isEmpty() && data.isObject())
                rows = data.toObject().value(QStringLiteral("lists")).toArray();
            QList<Playlist> result;
            for (const QJsonValue &value : rows)
            {
                const QJsonObject row = value.toObject();
                const QString globalId = stringField(row, {"global_collection_id"});
                const QString listId = stringField(row, {"listid", "list_id", "specialid"});
                const QString title = stringField(row, {"specialname", "name", "title"});
                if ((!globalId.isEmpty() || !listId.isEmpty()) && !title.isEmpty())
                    result.push_back({globalId, listId, title,
                                      normalizedCover(stringField(row, {"img", "pic", "cover"})),
                                      stringField(row, {"song_count", "count", "total"}).toInt()});
            }
            if (result.size() != rows.size())
            {
                callback({}, QStringLiteral("SchemaMismatch"),
                         QStringLiteral("歌单响应缺少必要 ID 或标题"));
                return;
            }
            callback(std::move(result), {}, {});
        });
}

void KuGouApi::playlistTracks(const QString &globalCollectionId, const QString &listId,
                              SearchCallback callback, int page)
{
    const bool userCloudPlaylist = !listId.isEmpty();
    QUrlQuery query;
    query.addQueryItem(userCloudPlaylist ? QStringLiteral("listid") : QStringLiteral("id"),
                       userCloudPlaylist ? listId : globalCollectionId);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    auto handleResponse = [callback = std::move(callback)](ApiClient::Response response) mutable
    {
        if (const auto error = endpointError(response); !error.code.isEmpty())
        {
            callback({}, error.code, error.message);
            return;
        }
        const QJsonObject root = response.json.object();
        const QJsonValue dataValue = root.value(QStringLiteral("data"));
        const QJsonObject data = dataValue.toObject();
        const bool hasList = dataValue.isArray() || data.value(QStringLiteral("items")).isArray() ||
                             data.value(QStringLiteral("songs")).isArray() ||
                             data.value(QStringLiteral("lists")).isArray() ||
                             data.value(QStringLiteral("info")).isArray();
        if (!hasList)
        {
            callback({}, QStringLiteral("SchemaMismatch"), QStringLiteral("歌单响应缺少歌曲列表"));
            return;
        }
        auto tracks = parseTracks(root);
        if ((!dataValue.toArray().isEmpty() ||
             !data.value(QStringLiteral("songs")).toArray().isEmpty() ||
             !data.value(QStringLiteral("items")).toArray().isEmpty() ||
             !data.value(QStringLiteral("lists")).toArray().isEmpty() ||
             !data.value(QStringLiteral("info")).toArray().isEmpty()) &&
            tracks.isEmpty())
        {
            callback({}, QStringLiteral("SchemaMismatch"),
                     QStringLiteral("歌单歌曲缺少必要 ID 或标题"));
            return;
        }
        callback(std::move(tracks), {}, {});
    };
    if (userCloudPlaylist)
        m_client.postJson(QStringLiteral("/playlist/track/all/new"), query,
                          QJsonDocument(QJsonObject{}), std::move(handleResponse));
    else
        m_client.get(QStringLiteral("/playlist/track/all"), query, std::move(handleResponse));
}

void KuGouApi::createLoginQr(std::function<void(QString, QString, QString)> callback)
{
    cancelLogin();
    m_loginClient = std::make_unique<ApiClient>(m_serviceBase, this, false);
    ApiClient *loginClient = m_loginClient.get();
    QList<QNetworkCookie> deviceCookies;
    for (const QNetworkCookie &cookie : m_client.cookieJar()->cookies())
    {
        if (cookie.name() != QByteArrayLiteral("token") &&
            cookie.name() != QByteArrayLiteral("userid"))
            deviceCookies.push_back(cookie);
    }
    loginClient->importCookies(deviceCookies);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("timestamp"),
                       QString::number(QDateTime::currentMSecsSinceEpoch()));
    loginClient->get(
        QStringLiteral("/login/qr/key"), query,
        [loginClient, callback = std::move(callback)](ApiClient::Response response) mutable
        {
            if (const auto error = endpointError(response); !error.code.isEmpty())
            {
                callback({}, {}, error.message);
                return;
            }
            const auto data = response.json.object().value(QStringLiteral("data")).toObject();
            const QString key = stringField(data, {"qrcode", "key"});
            if (key.isEmpty())
            {
                callback({}, {}, QStringLiteral("二维码 key 响应结构不匹配"));
                return;
            }

            QUrlQuery createQuery;
            createQuery.addQueryItem(QStringLiteral("key"), key);
            createQuery.addQueryItem(QStringLiteral("qrimg"), QStringLiteral("1"));
            createQuery.addQueryItem(QStringLiteral("timestamp"),
                                     QString::number(QDateTime::currentMSecsSinceEpoch()));
            loginClient->get(
                QStringLiteral("/login/qr/create"), createQuery,
                [key, callback = std::move(callback)](ApiClient::Response createResponse) mutable
                {
                    if (const auto error = endpointError(createResponse); !error.code.isEmpty())
                    {
                        callback({}, {}, error.message);
                        return;
                    }
                    const auto createData =
                        createResponse.json.object().value(QStringLiteral("data")).toObject();
                    const QString image =
                        stringField(createData, {"base64", "qrcode_img", "qrimg"});
                    if (image.isEmpty())
                    {
                        callback({}, {}, QStringLiteral("二维码创建响应缺少 base64 图片"));
                        return;
                    }
                    callback(key, image, {});
                },
                true);
        },
        true);
}

void KuGouApi::albumTracks(const QString &albumId, SearchCallback callback, int page)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), albumId);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    m_client.get(
        QStringLiteral("/album/songs"), query,
        [callback = std::move(callback)](ApiClient::Response response)
        {
            const auto error = endpointError(response);
            if (!error.code.isEmpty())
            {
                callback({}, error.code, error.message);
                return;
            }
            const auto root = response.json.object();
            const auto songs =
                root.value(QStringLiteral("data")).toObject().value(QStringLiteral("songs"));
            if (!songs.isArray())
            {
                callback({}, QStringLiteral("SchemaMismatch"),
                         QStringLiteral("专辑响应缺少歌曲列表"));
                return;
            }
            auto tracks = parseTracks(root);
            if (tracks.size() != songs.toArray().size())
            {
                callback({}, QStringLiteral("SchemaMismatch"),
                         QStringLiteral("专辑歌曲缺少必要信息"));
                return;
            }
            callback(tracks, {}, {});
        });
}

void KuGouApi::createPlaylist(const QString &name, WriteCallback callback)
{
    m_client.postJson(QStringLiteral("/playlist/add"), {},
                      QJsonDocument(QJsonObject{{"name", name}, {"type", 0}}),
                      [callback = std::move(callback)](ApiClient::Response response)
                      {
                          const auto error = endpointError(response);
                          callback(error.code, error.message);
                      });
}
void KuGouApi::deletePlaylist(const QString &listId, WriteCallback callback)
{
    m_client.postJson(QStringLiteral("/playlist/del"), {},
                      QJsonDocument(QJsonObject{{"listid", listId}}),
                      [callback = std::move(callback)](ApiClient::Response response)
                      {
                          const auto error = endpointError(response);
                          callback(error.code, error.message);
                      });
}
void KuGouApi::addPlaylistTrack(const QString &listId, const Track &track, WriteCallback callback)
{
    // Upstream uses a comma-separated list of pipe-separated records; reject names
    // that cannot round-trip through this contract rather than silently changing them.
    if (track.title.contains(QLatin1Char('|')) || track.title.contains(QLatin1Char(',')))
    {
        callback(QStringLiteral("Unsupported"),
                 QStringLiteral("服务暂不支持添加名称含逗号或竖线的歌曲"));
        return;
    }
    const QString data =
        QStringList{track.title, track.hash, track.albumId, track.albumAudioId}.join(
            QLatin1Char('|'));
    m_client.postJson(QStringLiteral("/playlist/tracks/add"), {},
                      QJsonDocument(QJsonObject{{"listid", listId}, {"data", data}}),
                      [callback = std::move(callback)](ApiClient::Response response)
                      {
                          const auto error = endpointError(response);
                          callback(error.code, error.message);
                      });
}
void KuGouApi::removePlaylistTrack(const QString &listId, const QString &fileId,
                                   WriteCallback callback)
{
    if (fileId.isEmpty())
    {
        callback(QStringLiteral("SchemaMismatch"),
                 QStringLiteral("歌曲缺少移除标识，请先刷新歌单"));
        return;
    }
    m_client.postJson(QStringLiteral("/playlist/tracks/del"), {},
                      QJsonDocument(QJsonObject{{"listid", listId}, {"fileids", fileId}}),
                      [callback = std::move(callback)](ApiClient::Response response)
                      {
                          const auto error = endpointError(response);
                          callback(error.code, error.message);
                      });
}

void KuGouApi::checkLoginQr(const QString &key, std::function<void(int, QString)> callback)
{
    if (!m_loginClient)
    {
        callback(-1, QStringLiteral("登录会话已取消"));
        return;
    }
    ApiClient *loginClient = m_loginClient.get();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key);
    query.addQueryItem(QStringLiteral("timestamp"),
                       QString::number(QDateTime::currentMSecsSinceEpoch()));
    loginClient->get(
        QStringLiteral("/login/qr/check"), query,
        [this, loginClient, callback = std::move(callback)](ApiClient::Response response) mutable
        {
            if (!m_loginClient || m_loginClient.get() != loginClient)
                return;
            if (const auto error = endpointError(response); !error.code.isEmpty())
            {
                callback(-1, error.message);
                return;
            }
            const auto root = response.json.object();
            const auto data = root.value(QStringLiteral("data")).toObject();
            const int status = data.value(QStringLiteral("status")).toInt(-1);
            if (status == 4 && !loginClient->cookieJar()->hasLoginSession())
            {
                const QString token = stringField(data, {"token", "Token"});
                const QString userId = stringField(data, {"userid", "user_id", "UserId"});
                if (!loginClient->storeLoginSession(token, userId))
                {
                    callback(-1, QStringLiteral("扫码已确认，但服务未返回可保存的登录凭据"));
                    return;
                }
            }
            if (status == 4 && !m_client.importCookies(loginClient->cookieJar()->cookies()))
            {
                callback(-1, QStringLiteral("登录凭据无法提交到主会话"));
                return;
            }
            callback(status, {});
        },
        true);
}

QString KuGouApi::scopeKey() const
{
    const auto identity =
        m_serviceBase.toString(QUrl::RemoveQuery | QUrl::RemoveFragment).toUtf8() +
        QByteArrayLiteral("|") + m_client.cookieJar()->userId().toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
bool KuGouApi::beginAuthenticatedSession()
{
    // A pre-login registration result must never suppress registration for the new account session.
    m_registrationState = RegistrationState::Unregistered;
    m_authenticated = m_client.cookieJar()->hasLoginSession();
    m_client.invalidateSession();
    m_pendingResolves.clear();
    emit sessionInvalidated();
    return m_authenticated;
}

bool KuGouApi::loginWithCookies(const QString &cookieHeader, QString *error)
{
    cancelLogin();
    if (!m_client.storeLoginCookies(cookieHeader, error))
        return false;
    return beginAuthenticatedSession();
}

void KuGouApi::cancelLogin()
{
    if (!m_loginClient)
        return;
    m_loginClient->invalidateSession();
    m_loginClient.reset();
}

bool KuGouApi::restoreAuthenticatedSession()
{
    const auto *jar = m_client.cookieJar();
    m_authenticated = jar && jar->hasLoginSession();
    return m_authenticated;
}

void KuGouApi::logout()
{
    cancelLogin();
    m_client.invalidateSession();
    if (auto *jar = m_client.cookieJar())
        jar->clearStoredCookies();
    m_authenticated = false;
    m_registrationState = RegistrationState::Unregistered;
    m_pendingResolves.clear();
    emit sessionInvalidated();
}
