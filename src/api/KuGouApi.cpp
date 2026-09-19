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
QJsonArray findFirstArray(const QJsonValue &value,
                          std::initializer_list<const char *> preferredKeys, int depth = 0)
{
    if (depth > 6)
        return {};
    if (value.isArray())
        return value.toArray();
    if (!value.isObject())
        return {};
    const auto object = value.toObject();
    for (const char *key : preferredKeys)
    {
        const auto candidate = object.value(QLatin1String(key));
        if (candidate.isArray() && !candidate.toArray().isEmpty())
            return candidate.toArray();
    }
    for (auto it = object.begin(); it != object.end(); ++it)
        if (it.value().isObject())
            if (auto rows = findFirstArray(it.value(), preferredKeys, depth + 1); !rows.isEmpty())
                return rows;
    return {};
}
void collectKeywords(const QJsonValue &value, QStringList &keywords, int depth = 0)
{
    if (depth > 7 || keywords.size() >= 20)
        return;
    if (value.isArray())
    {
        for (const auto &item : value.toArray())
            collectKeywords(item, keywords, depth + 1);
        return;
    }
    if (!value.isObject())
        return;
    const auto object = value.toObject();
    for (const auto &key : {"keyword", "keywords", "name", "text", "tip", "HintInfo",
                            "hint", "query", "word"})
    {
        const QString candidate = object.value(QLatin1String(key)).toString().trimmed();
        if (!candidate.isEmpty() && candidate.size() <= 80 && !keywords.contains(candidate))
            keywords.append(candidate);
    }
    for (auto it = object.begin(); it != object.end(); ++it)
        if (it.value().isArray() || it.value().isObject())
            collectKeywords(it.value(), keywords, depth + 1);
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
    if (rows.isEmpty())
        rows = findFirstArray(root, {"song_list", "songs", "audio_list", "audios", "items",
                                     "lists", "info", "list"});
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
QString findMediaUrlForQuality(const QJsonValue &value, const QString &quality, int depth = 0)
{
    if (depth > 6)
        return {};
    if (value.isObject())
    {
        const auto object = value.toObject();
        const QString objectQuality =
            stringField(object, {"quality", "quality_type", "audio_quality", "type"});
        if (objectQuality.compare(quality, Qt::CaseInsensitive) == 0)
            if (const auto url = findMediaUrl(object); !url.isEmpty())
                return url;
        for (const auto &key : {"url", "play_url", "playUrl"})
        {
            const auto named = object.value(quality);
            if (!named.isUndefined())
                if (const auto url = findMediaUrl(named); !url.isEmpty())
                    return url;
            const auto prefixed = object.value(quality + QLatin1Char('_') + QLatin1String(key));
            if (!prefixed.isUndefined())
                if (const auto url = findMediaUrl(prefixed); !url.isEmpty())
                    return url;
        }
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.value().isArray() || it.value().isObject())
                if (const auto url = findMediaUrlForQuality(it.value(), quality, depth + 1);
                    !url.isEmpty())
                    return url;
    }
    else if (value.isArray())
        for (const auto &item : value.toArray())
            if (const auto url = findMediaUrlForQuality(item, quality, depth + 1); !url.isEmpty())
                return url;
    return {};
}
QUrl mediaUrl(const ApiClient::Response &response, const QString &quality)
{
    const auto root = response.json.object();
    const QJsonValue payload = root.value(QStringLiteral("data")).isUndefined()
                                   ? QJsonValue(root)
                                   : root.value(QStringLiteral("data"));
    QString value = findMediaUrlForQuality(payload, quality);
    if (value.isEmpty())
        value = findMediaUrl(payload);
    QUrl url(value);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
        return {};
    return url;
}
QVariantMap playlistEntry(const QJsonObject &row)
{
    const QString id = stringField(
        row, {"global_collection_id", "globalCollectionId", "gid", "specialid", "id"});
    const QString title =
        stringField(row, {"specialname", "name", "title", "playlist_name"});
    if (id.isEmpty() || title.isEmpty())
        return {};
    const QString cover = normalizedCover(
        stringField(row, {"img", "pic", "cover", "cover_url", "sizable_cover"}));
    return {{"kind", "playlist"},
            {"id", id},
            {"globalId", id},
            {"title", title},
            {"name", title},
            {"subtitle", stringField(row, {"intro", "description", "desc"})},
            {"description", stringField(row, {"intro", "description", "desc"})},
            {"coverUrl", cover},
            {"cover", cover},
            {"count", stringField(row, {"song_count", "count", "total"}).toInt()},
            {"creatorUserId", stringField(row, {"list_create_userid", "userid", "create_userid"})},
            {"creatorListId", stringField(row, {"list_create_listid", "listid", "list_id"})},
            {"creatorGid", stringField(row, {"list_create_gid", "gid"})}};
}

QJsonArray commentRows(const QJsonObject &root, bool *found)
{
    const auto data = root.value(QStringLiteral("data"));
    const auto container = data.isObject() ? data.toObject() : root;
    for (const char *key : {"comments", "comment_list", "commentList", "list", "info"})
    {
        const auto value = container.value(QLatin1String(key));
        if (value.isArray())
        {
            *found = true;
            return value.toArray();
        }
        if (value.isObject())
            for (const char *nested : {"list", "comments", "info"})
                if (value.toObject().value(QLatin1String(nested)).isArray())
                {
                    *found = true;
                    return value.toObject().value(QLatin1String(nested)).toArray();
                }
    }
    if (data.isArray())
    {
        *found = true;
        return data.toArray();
    }
    *found = false;
    return {};
}

QVariantMap commentEntry(const QJsonObject &row)
{
    const auto user = row.value(QStringLiteral("user")).toObject();
    const auto userInfo = user.isEmpty() ? row.value(QStringLiteral("userinfo")).toObject() : user;
    const auto content = stringField(row, {"content", "comment", "text", "msg"}).trimmed();
    if (content.isEmpty())
        return {};
    QString author = stringField(row, {"nickname", "user_name", "username", "nick_name"});
    if (author.isEmpty())
        author = stringField(userInfo, {"nickname", "nick_name", "name", "username"});
    if (author.isEmpty())
        author = QStringLiteral("酷狗用户");
    return {{"id", stringField(row, {"id", "comment_id", "tid"})},
            {"author", author},
            {"content", content},
            {"time", stringField(row, {"addtime", "create_time", "time", "date"})},
            {"likes", stringField(row, {"like_count", "liked_count", "likedCount", "support"})}};
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
    m_userAuthAttempted = false;
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
    static const QStringList supportedQualities{
        QStringLiteral("128"),         QStringLiteral("320"),
        QStringLiteral("flac"),        QStringLiteral("high"),
        QStringLiteral("piano"),       QStringLiteral("acappella"),
        QStringLiteral("subwoofer"),   QStringLiteral("ancient"),
        QStringLiteral("surnay"),      QStringLiteral("dj"),
        QStringLiteral("viper_atmos"), QStringLiteral("viper_clear"),
        QStringLiteral("viper_tape"),  QStringLiteral("super")};
    const auto requestedQuality = supportedQualities.contains(quality) ? quality
                                                                        : QStringLiteral("128");
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
    auto completion =
        std::make_shared<std::function<void(QUrl, QString)>>(std::move(callback));
    auto lastError = std::make_shared<QString>();
    auto query = [hash, albumAudioId, quality]
    {
        QUrlQuery result;
        result.addQueryItem(QStringLiteral("hash"), hash);
        result.addQueryItem(QStringLiteral("quality"), quality);
        if (!albumAudioId.isEmpty())
            result.addQueryItem(QStringLiteral("album_audio_id"), albumAudioId);
        return result;
    };

    auto requestLegacy = std::make_shared<std::function<void()>>();
    *requestLegacy = [this, query, quality, completion, lastError]
    {
        m_client.get(QStringLiteral("/song/url"), query(),
                     [quality, completion, lastError](ApiClient::Response response)
                     {
                         const auto error = endpointError(response);
                         if (error.code.isEmpty())
                             if (const QUrl url = mediaUrl(response, quality); !url.isEmpty())
                             {
                                 (*completion)(url, {});
                                 return;
                             }
                         if (!error.message.isEmpty())
                             *lastError = error.message;
                         (*completion)({}, lastError->isEmpty()
                                               ? QStringLiteral("服务未返回可播放的音频地址")
                                               : *lastError);
                     });
    };

    auto requestNew = std::make_shared<std::function<void()>>();
    *requestNew = [this, query, quality, completion, lastError, requestLegacy]
    {
        m_client.get(QStringLiteral("/song/url/new"), query(),
                     [quality, completion, lastError,
                      requestLegacy](ApiClient::Response response)
                     {
                         const auto error = endpointError(response);
                         if (error.code.isEmpty())
                             if (const QUrl url = mediaUrl(response, quality); !url.isEmpty())
                             {
                                 (*completion)(url, {});
                                 return;
                             }
                         if (!error.message.isEmpty())
                             *lastError = error.message;
                         (*requestLegacy)();
                     });
    };

    auto requestAuthorized = std::make_shared<std::function<void()>>();
    *requestAuthorized = [this, query, quality, completion, lastError, requestNew]
    {
        m_client.get(QStringLiteral("/song/url/auth/merge"), query(),
                     [quality, completion, lastError,
                      requestNew](ApiClient::Response response)
                     {
                         const auto error = endpointError(response);
                         if (error.code.isEmpty())
                             if (const QUrl url = mediaUrl(response, quality); !url.isEmpty())
                             {
                                 (*completion)(url, {});
                                 return;
                             }
                         if (!error.message.isEmpty())
                             *lastError = error.message;
                         (*requestNew)();
                     });
    };

    if (!authenticated())
    {
        (*requestNew)();
        return;
    }
    if (m_userAuthAttempted)
    {
        (*requestAuthorized)();
        return;
    }
    m_userAuthAttempted = true;
    m_client.get(QStringLiteral("/user/verify"), {},
                 [requestAuthorized, requestNew](ApiClient::Response response)
                 {
                     if (endpointError(response).code.isEmpty())
                         (*requestAuthorized)();
                     else
                         (*requestNew)();
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
void KuGouApi::hotSearches(std::function<void(QStringList, QString)> callback)
{
    m_client.get(QStringLiteral("/search/hot"), {},
                 [callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response, true); !error.code.isEmpty())
                     {
                         callback({}, error.message);
                         return;
                     }
                     QStringList keywords;
                     collectKeywords(response.json.object(), keywords);
                     callback(keywords, keywords.isEmpty() ? QStringLiteral("热搜响应缺少关键词")
                                                          : QString{});
                 });
}
void KuGouApi::searchSuggestions(const QString &keywords,
                                 std::function<void(QStringList, QString)> callback)
{
    if (keywords.trimmed().isEmpty())
    {
        callback({}, {});
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keywords"), keywords.trimmed());
    query.addQueryItem(QStringLiteral("albumTipCount"), QStringLiteral("5"));
    query.addQueryItem(QStringLiteral("correctTipCount"), QStringLiteral("5"));
    query.addQueryItem(QStringLiteral("mvTipCount"), QStringLiteral("3"));
    query.addQueryItem(QStringLiteral("musicTipCount"), QStringLiteral("8"));
    m_client.get(QStringLiteral("/search/suggest"), query,
                 [callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response, true); !error.code.isEmpty())
                     {
                         callback({}, error.message);
                         return;
                     }
                     QStringList suggestions;
                     collectKeywords(response.json.object(), suggestions);
                     callback(suggestions, {});
                 });
}
void KuGouApi::dailyRecommendations(SearchCallback callback)
{
    m_client.get(QStringLiteral("/everyday/recommend"), {},
                 [callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.code, error.message);
                         return;
                     }
                     callback(parseTracks(response.json.object()), {}, {});
                 });
}
void KuGouApi::rankEntries(std::function<void(QVariantList, QString)> callback)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("withsong"), QStringLiteral("0"));
    m_client.get(QStringLiteral("/rank/list"), query,
                 [callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.message);
                         return;
                     }
                     const auto rows = findFirstArray(
                         response.json.object(), {"rank_list", "ranklist", "list", "info"});
                     QVariantList entries;
                     for (const auto &value : rows)
                     {
                         const auto row = value.toObject();
                         const QString id = stringField(row, {"rankid", "rank_id", "id"});
                         const QString title =
                             stringField(row, {"rankname", "rank_name", "name", "title"});
                         if (id.isEmpty() || title.isEmpty())
                             continue;
                         entries.append(QVariantMap{
                             {"kind", "rank"},
                             {"id", id},
                             {"title", title},
                             {"name", title},
                             {"subtitle", stringField(row, {"update_frequency", "intro", "desc"})},
                             {"coverUrl", normalizedCover(stringField(
                                              row, {"imgurl", "image", "cover", "banner7url",
                                                    "share_logo"}))}});
                     }
                     callback(entries, entries.isEmpty() ? QStringLiteral("排行榜响应缺少列表")
                                                        : QString{});
                 });
}
void KuGouApi::rankTracks(const QString &rankId, SearchCallback callback, int page)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("rankid"), rankId);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    m_client.get(QStringLiteral("/rank/audio"), query,
                 [callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.code, error.message);
                         return;
                     }
                     callback(parseTracks(response.json.object()), {}, {});
                 });
}
void KuGouApi::popularPlaylists(std::function<void(QVariantList, QString)> callback, int page)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("category_id"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("withsong"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("withtag"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    m_client.get(QStringLiteral("/top/playlist"), query,
                 [callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.message);
                         return;
                     }
                     const auto rows = findFirstArray(response.json.object(),
                                                      {"special_list", "playlists", "lists",
                                                       "items", "info", "list"});
                     QVariantList entries;
                     for (const auto &value : rows)
                         if (const auto entry = playlistEntry(value.toObject()); !entry.isEmpty())
                             entries.append(entry);
                     callback(entries, entries.isEmpty() ? QStringLiteral("热门歌单响应缺少列表")
                                                        : QString{});
                 });
}
void KuGouApi::comments(const QString &kind, const QString &id,
                        std::function<void(QVariantList, QString, QString)> callback, int page)
{
    const QString path = kind == QStringLiteral("song") ? QStringLiteral("/comment/music")
                         : kind == QStringLiteral("album") ? QStringLiteral("/comment/album")
                         : kind == QStringLiteral("playlist") ? QStringLiteral("/comment/playlist")
                                                               : QString{};
    if (path.isEmpty() || id.trimmed().isEmpty() || page < 1)
    {
        callback({}, QStringLiteral("InvalidArgument"), QStringLiteral("评论目标无效"));
        return;
    }
    QUrlQuery query;
    query.addQueryItem(kind == QStringLiteral("song") ? QStringLiteral("mixsongid")
                                                       : QStringLiteral("id"), id);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    m_client.get(path, query,
                 [callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.code, error.message);
                         return;
                     }
                     if (!response.json.isObject())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("评论响应不是 JSON 对象"));
                         return;
                     }
                     bool found = false;
                     const auto rows = commentRows(response.json.object(), &found);
                     if (!found)
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("评论响应缺少列表"));
                         return;
                     }
                     QVariantList result;
                     for (const auto &value : rows)
                         if (const auto item = commentEntry(value.toObject()); !item.isEmpty())
                             result.append(item);
                     if (!rows.isEmpty() && result.isEmpty())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("评论响应缺少内容字段"));
                         return;
                     }
                     callback(result, {}, {});
                 });
}
void KuGouApi::sendComment(const QString &kind, const QString &id, const QString &name,
                           const QString &content, std::function<void(QString, QString)> callback)
{
    const QString path = kind == QStringLiteral("song") ? QStringLiteral("/comment/music/send")
                         : kind == QStringLiteral("album") ? QStringLiteral("/comment/album/send")
                         : kind == QStringLiteral("playlist") ? QStringLiteral("/comment/playlist/send")
                                                               : QString{};
    const QString message = content.trimmed();
    if (path.isEmpty() || id.trimmed().isEmpty() || message.isEmpty() || message.size() > 500)
    {
        callback(QStringLiteral("InvalidArgument"), QStringLiteral("评论目标或内容无效（最多 500 字）"));
        return;
    }
    if (!authenticated())
    {
        callback(QStringLiteral("AuthRequired"), QStringLiteral("登录后才能发表评论"));
        return;
    }
    QJsonObject body{{kind == QStringLiteral("song") ? QStringLiteral("mixsongid")
                                                     : QStringLiteral("id"), id},
                     {"content", message}};
    if (!name.trimmed().isEmpty())
        body.insert(QStringLiteral("name"), name.trimmed());
    m_client.postJson(path, {}, QJsonDocument(body),
                      [callback = std::move(callback)](ApiClient::Response response)
                      {
                          const auto error = endpointError(response);
                          if (error.code.isEmpty() && !response.json.isObject())
                          {
                              callback(QStringLiteral("UnknownResult"),
                                       QStringLiteral("服务已响应但未返回可确认的发布结果"));
                              return;
                          }
                          callback(error.code, error.message);
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
                {
                    Playlist playlist;
                    playlist.globalCollectionId = globalId;
                    playlist.listId = listId;
                    playlist.title = title;
                    playlist.cover =
                        normalizedCover(stringField(row, {"img", "pic", "cover"}));
                    playlist.description = stringField(row, {"intro", "description", "desc"});
                    playlist.creatorUserId =
                        stringField(row, {"list_create_userid", "create_userid", "userid"});
                    playlist.creatorListId =
                        stringField(row, {"list_create_listid", "source_listid"});
                    playlist.trackCount =
                        stringField(row, {"song_count", "count", "total"}).toInt();
                    playlist.totalVersion =
                        stringField(row, {"total_ver", "total_version"}).toLongLong();
                    playlist.type = stringField(row, {"type", "list_type"}).toInt();
                    playlist.sort = stringField(row, {"sort", "sort_index"}).toInt();
                    result.push_back(std::move(playlist));
                }
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

void KuGouApi::playlistDetail(const QString &globalCollectionId,
                              std::function<void(QVariantMap, QString)> callback)
{
    if (globalCollectionId.isEmpty())
    {
        callback({}, QStringLiteral("歌单缺少 global_collection_id"));
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ids"), globalCollectionId);
    m_client.get(QStringLiteral("/playlist/detail"), query,
                 [globalCollectionId, callback = std::move(callback)](
                     ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.message);
                         return;
                     }
                     const QJsonObject root = response.json.object();
                     auto rows = findFirstArray(root, {"info", "lists", "items", "data"});
                     QJsonObject row;
                     if (!rows.isEmpty())
                         row = rows.first().toObject();
                     else if (root.value(QStringLiteral("data")).isObject())
                         row = root.value(QStringLiteral("data")).toObject();
                     auto detail = playlistEntry(row);
                     if (detail.isEmpty())
                     {
                         callback({}, QStringLiteral("歌单详情响应缺少有效 ID 或标题"));
                         return;
                     }
                     detail.insert(QStringLiteral("id"), globalCollectionId);
                     detail.insert(QStringLiteral("globalId"), globalCollectionId);
                     callback(detail, {});
                 });
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
void KuGouApi::favoritePlaylist(const QVariantMap &playlist, WriteCallback callback)
{
    if (!authenticated())
    {
        callback(QStringLiteral("AuthRequired"), QStringLiteral("登录后才能收藏歌单"));
        return;
    }
    const QString name = playlist.value(QStringLiteral("title")).toString().trimmed();
    const QString creatorUserId =
        playlist.value(QStringLiteral("creatorUserId")).toString();
    const QString creatorListId =
        playlist.value(QStringLiteral("creatorListId")).toString();
    if (name.isEmpty() || creatorUserId.isEmpty() || creatorListId.isEmpty())
    {
        callback(QStringLiteral("SchemaMismatch"),
                 QStringLiteral("歌单详情缺少收藏所需的创建者信息"));
        return;
    }
    const QJsonObject body{{"name", name},
                           {"type", 1},
                           {"source", 1},
                           {"list_create_userid", creatorUserId},
                           {"list_create_listid", creatorListId},
                           {"list_create_gid",
                            playlist.value(QStringLiteral("creatorGid")).toString()}};
    m_client.postJson(QStringLiteral("/playlist/add"), {}, QJsonDocument(body),
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
void KuGouApi::updatePlaylist(const QString &listId, qint64 totalVersion, int type,
                              const QString &name, const QString &description,
                              WriteCallback callback)
{
    if (listId.isEmpty() || name.trimmed().isEmpty())
    {
        callback(QStringLiteral("InvalidArgument"), QStringLiteral("歌单名称不能为空"));
        return;
    }
    const QJsonObject body{{"listid", listId},
                           {"total_ver", totalVersion},
                           {"type", type},
                           {"name", name.trimmed()},
                           {"intro", description.trimmed()}};
    m_client.postJson(QStringLiteral("/playlist/update"), {}, QJsonDocument(body),
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
    m_userAuthAttempted = false;
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
    if (!m_authenticated)
        m_userAuthAttempted = false;
    return m_authenticated;
}

void KuGouApi::logout()
{
    cancelLogin();
    m_client.invalidateSession();
    if (auto *jar = m_client.cookieJar())
        jar->clearStoredCookies();
    m_authenticated = false;
    m_userAuthAttempted = false;
    m_registrationState = RegistrationState::Unregistered;
    m_pendingResolves.clear();
    emit sessionInvalidated();
}
