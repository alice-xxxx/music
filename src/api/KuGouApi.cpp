#include "KuGouApi.h"
#include "platform/CredentialCookieJar.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
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
        QString message = stringField(root, {"error_msg", "errmsg", "message", "error", "msg"});
        if (message.isEmpty())
            message = QStringLiteral("服务业务错误 %1").arg(businessCode);
        return {searchEndpoint && businessCode == 152 ? QStringLiteral("AuthRequired")
                                                      : QStringLiteral("BusinessError"),
                message};
    }
    return {response.errorCode, response.errorMessage};
}
bool transientPlaybackError(const ApiClient::Response &response)
{
    return response.errorCode == QStringLiteral("Timeout") ||
           (response.errorCode == QStringLiteral("Unavailable") &&
            (response.httpStatus == 0 || response.httpStatus >= 500));
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
    if (cover.startsWith(QStringLiteral("//")))
        cover.prepend(QStringLiteral("https:"));
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
enum class TrackShape { Search, Daily, Rank, Playlist, Album };

QList<Track> parseTracks(const QJsonArray &rows, TrackShape shape)
{
    QList<Track> tracks;
    for (const auto &value : rows)
    {
        if (!value.isObject())
            return {};
        const auto row = value.toObject();
        if (shape == TrackShape::Playlist && row.contains(QStringLiteral("shield")) &&
            !row.contains(QStringLiteral("hash")))
            continue;
        const auto audio = row.value(QStringLiteral("audio_info")).toObject();
        const auto base = row.value(QStringLiteral("base")).toObject();
        const auto album = row.value(QStringLiteral("album_info")).toObject();
        Track track;
        switch (shape)
        {
        case TrackShape::Search:
            track.hash = stringField(row, {"FileHash"});
            track.albumAudioId = stringField(row, {"MixSongID"});
            track.title = stringField(row, {"OriSongName"});
            track.artist = stringField(row, {"SingerName"});
            track.albumId = stringField(row, {"AlbumID"});
            track.album = stringField(row, {"AlbumName"});
            track.coverUrl = normalizedCover(stringField(row, {"Image"}));
            track.durationMs = row.value(QStringLiteral("Duration")).toInteger(-1) * 1000;
            break;
        case TrackShape::Daily:
            track.hash = stringField(row, {"hash_128"});
            track.albumAudioId = stringField(row, {"album_audio_id"});
            track.title = stringField(row, {"songname"});
            track.artist = stringField(row, {"author_name"});
            track.albumId = stringField(row, {"album_id"});
            track.album = stringField(row, {"album_name"});
            track.coverUrl = normalizedCover(stringField(row, {"sizable_cover"}));
            track.durationMs = row.value(QStringLiteral("time_length")).toInteger(-1) * 1000;
            break;
        case TrackShape::Rank:
            track.hash = stringField(audio, {"hash_128"});
            track.albumAudioId = stringField(row, {"album_audio_id"});
            track.title = stringField(row, {"songname"});
            track.artist = stringField(row, {"author_name"});
            track.albumId = stringField(row, {"album_id"});
            track.album = stringField(album, {"album_name"});
            track.coverUrl = normalizedCover(stringField(album, {"sizable_cover"}));
            track.durationMs = audio.value(QStringLiteral("duration_128")).toInteger(-1);
            break;
        case TrackShape::Playlist:
            track.hash = stringField(row, {"hash"});
            track.albumAudioId = stringField(row, {"mixsongid"});
            track.title = stringField(row, {"name"});
            track.fileId = stringField(row, {"fileid"});
            track.albumId = stringField(row, {"album_id"});
            track.album = stringField(row.value(QStringLiteral("albuminfo")).toObject(), {"name"});
            track.coverUrl = normalizedCover(stringField(row, {"cover"}));
            track.durationMs = row.value(QStringLiteral("timelen")).toInteger(-1);
            break;
        case TrackShape::Album:
            track.hash = stringField(audio, {"hash_128"});
            track.albumAudioId = stringField(base, {"album_audio_id"});
            track.title = stringField(base, {"audio_name"});
            track.artist = stringField(base, {"author_name"});
            track.albumId = stringField(base, {"album_id"});
            track.album = stringField(album, {"album_name"});
            track.coverUrl = normalizedCover(stringField(album, {"cover"}));
            track.durationMs = audio.value(QStringLiteral("duration_128")).toInteger(-1);
            break;
        }
        if (track.hash.isEmpty() || track.title.isEmpty())
            return {};
        const char *singersKey = shape == TrackShape::Search ? "Singers" :
                                 shape == TrackShape::Rank || shape == TrackShape::Album ? "authors" : "singerinfo";
        for (const auto &singerValue : row.value(QLatin1String(singersKey)).toArray())
        {
            const auto singer = singerValue.toObject();
            const auto id = shape == TrackShape::Rank || shape == TrackShape::Album
                                ? stringField(singer, {"author_id"}) : stringField(singer, {"id"});
            const auto name = shape == TrackShape::Rank || shape == TrackShape::Album
                                  ? stringField(singer, {"author_name"}) : stringField(singer, {"name"});
            if (!id.isEmpty() && !name.isEmpty())
                track.artists.append(QVariantMap{{"id", id}, {"name", name}});
        }
        if (shape == TrackShape::Playlist)
        {
            QStringList names;
            for (const auto &artist : track.artists)
                names.append(artist.toMap().value(QStringLiteral("name")).toString());
            track.artist = names.join(QStringLiteral("、"));
            const auto prefix = track.artist + QStringLiteral(" - ");
            if (!track.artist.isEmpty() && track.title.startsWith(prefix))
                track.title.remove(0, prefix.size());
        }
        track.key = QStringLiteral("kugou:") + track.hash + QLatin1Char(':') + track.albumAudioId;
        tracks.append(track);
    }
    return tracks;
}
QUrl mediaUrl(const ApiClient::Response &response)
{
    const auto root = response.json.object();
    for (const char *key : {"url", "backupUrl"})
        for (const auto &candidate : root.value(QLatin1String(key)).toArray())
        {
            const QUrl url(candidate.toString());
            if (url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
                (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http")))
                return url;
        }
    return {};
}
QVariantMap playlistEntry(const QJsonObject &row, bool detail)
{
    const QString id = stringField(row, {"global_collection_id"});
    const QString title = detail ? stringField(row, {"name"}) : stringField(row, {"specialname"});
    if (id.isEmpty() || title.isEmpty())
        return {};
    const QString cover = normalizedCover(detail ? stringField(row, {"pic"})
                                                : stringField(row, {"imgurl"}));
    QVariantMap entry{{"kind", "playlist"},
            {"id", id},
            {"globalId", id},
            {"title", title},
            {"name", title},
            {"subtitle", stringField(row, {"intro"})},
            {"description", stringField(row, {"intro"})},
            {"coverUrl", cover},
            {"cover", cover},
            {"creatorUserId", stringField(row, {"list_create_userid"})},
            {"creatorListId", stringField(row, {"list_create_listid"})},
            {"creatorGid", stringField(row, {"list_create_gid"})}};
    if (detail)
        entry.insert(QStringLiteral("count"), row.value(QStringLiteral("count")).toInt());
    return entry;
}

QVariantMap commentEntry(const QJsonObject &row)
{
    const auto content = stringField(row, {"content"}).trimmed();
    if (content.isEmpty())
        return {};
    QString author = stringField(row, {"user_name"});
    if (author.isEmpty())
        author = QStringLiteral("酷狗用户");
    return {{"id", stringField(row, {"id"})},
            {"author", author},
            {"content", content},
            {"time", stringField(row, {"addtime"})},
            {"likes", stringField(row, {"like"})}};
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
    m_authPlaybackRetryAfter = 0;
    ++m_resolveSessionGeneration;
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
                     const QJsonObject data = root.value(QStringLiteral("data")).toObject();
                     const auto rows = data.value(QStringLiteral("lists"));
                     if (!rows.isArray())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("搜索响应缺少歌曲列表"));
                         return;
                     }
                     const auto tracks = parseTracks(rows.toArray(), TrackShape::Search);
                     if (tracks.size() != rows.toArray().size())
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
                         const QString cover = stringField(album, {"sizable_cover"});
                         track.coverUrl = normalizedCover(cover);
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
    if (m_registrationState != RegistrationState::Registered)
    {
        m_pendingResolves.push_back({hash, albumAudioId, std::move(callback), quality});
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
    resolveSongAfterRegistration(hash, albumAudioId, std::move(callback), quality);
}

void KuGouApi::resolveSongAfterRegistration(const QString &hash, const QString &albumAudioId,
                                            std::function<void(QUrl, QString)> callback,
                                            const QString &quality)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), hash);
    query.addQueryItem(QStringLiteral("quality"), quality);
    if (!albumAudioId.isEmpty())
        query.addQueryItem(QStringLiteral("album_audio_id"), albumAudioId);

    auto completion = std::make_shared<std::function<void(QUrl, QString)>>(std::move(callback));
    auto requestPublicUrl = [this, query, completion]()
    { requestPublicSongUrl(query, completion); };
    auto requestAuthenticatedUrl = [this, query, completion, requestPublicUrl]()
    {
        m_client.get(QStringLiteral("/song/url/auth/merge"), query,
                     [this, completion, requestPublicUrl](ApiClient::Response response)
                     {
                         const auto error = endpointError(response);
                         const QUrl url = error.code.isEmpty() ? mediaUrl(response) : QUrl{};
                         if (!url.isEmpty())
                         {
                             m_authPlaybackRetryAfter = 0;
                             (*completion)(url, {});
                         }
                         else
                         {
                             if (transientPlaybackError(response))
                                 m_authPlaybackRetryAfter = QDateTime::currentMSecsSinceEpoch() + 60000;
                             requestPublicUrl();
                         }
                     });
    };
    if (!authenticated() || QDateTime::currentMSecsSinceEpoch() < m_authPlaybackRetryAfter)
    {
        requestPublicUrl();
        return;
    }
    if (m_userAuthAttempted)
    {
        requestAuthenticatedUrl();
        return;
    }
    m_client.get(QStringLiteral("/user/verify"), {},
                 [this, requestAuthenticatedUrl, requestPublicUrl](ApiClient::Response response)
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         if (transientPlaybackError(response))
                             m_authPlaybackRetryAfter = QDateTime::currentMSecsSinceEpoch() + 60000;
                         requestPublicUrl();
                         return;
                     }
                     m_userAuthAttempted = true;
                     requestAuthenticatedUrl();
                 });
}

void KuGouApi::requestPublicSongUrl(
    const QUrlQuery &query,
    std::shared_ptr<std::function<void(QUrl, QString)>> completion, int retriesLeft)
{
    m_client.get(QStringLiteral("/song/url"), query,
                 [this, guard = QPointer<KuGouApi>(this), query, completion,
                  retriesLeft](ApiClient::Response response)
                 {
                     if (!guard)
                         return;
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         if (transientPlaybackError(response) && retriesLeft > 0)
                         {
                             const auto generation = m_resolveSessionGeneration;
                             QTimer::singleShot(350, this,
                                                [this, query, completion, retriesLeft, generation]
                                                {
                                                    if (generation == m_resolveSessionGeneration)
                                                        requestPublicSongUrl(query, completion,
                                                                             retriesLeft - 1);
                                                });
                             return;
                         }
                         (*completion)({}, error.message);
                         return;
                     }
                     const QUrl url = mediaUrl(response);
                     (*completion)(url, url.isEmpty()
                                            ? QStringLiteral("服务未返回可播放的音频地址")
                                            : QString{});
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
                     const auto groups = response.json.object().value(QStringLiteral("data"))
                                             .toObject().value(QStringLiteral("list"));
                     if (!groups.isArray())
                     {
                         callback({}, QStringLiteral("热搜响应缺少列表"));
                         return;
                     }
                     QStringList keywords;
                     for (const auto &group : groups.toArray())
                         for (const auto &item : group.toObject().value(QStringLiteral("keywords")).toArray())
                         {
                             const auto keyword = item.toObject().value(QStringLiteral("keyword")).toString().trimmed();
                             if (!keyword.isEmpty() && !keywords.contains(keyword))
                                 keywords.append(keyword);
                             if (keywords.size() >= 20)
                                 break;
                         }
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
                     const auto groups = response.json.object().value(QStringLiteral("data"));
                     if (!groups.isArray())
                     {
                         callback({}, QStringLiteral("搜索建议响应缺少列表"));
                         return;
                     }
                     QStringList suggestions;
                     for (const auto &group : groups.toArray())
                         for (const auto &item : group.toObject().value(QStringLiteral("RecordDatas")).toArray())
                         {
                             const auto hint = item.toObject().value(QStringLiteral("HintInfo")).toString().trimmed();
                             if (!hint.isEmpty() && !suggestions.contains(hint))
                                 suggestions.append(hint);
                         }
                     callback(suggestions, {});
                 });
}
void KuGouApi::dailyRecommendations(SearchCallback callback, bool fresh)
{
    QUrlQuery query;
    if (fresh)
        query.addQueryItem(QStringLiteral("timestamp"),
                           QString::number(QDateTime::currentMSecsSinceEpoch()));
    m_client.get(QStringLiteral("/everyday/recommend"), query,
                 [callback = std::move(callback)](ApiClient::Response response) mutable
                 {
                     if (const auto error = endpointError(response); !error.code.isEmpty())
                     {
                         callback({}, error.code, error.message);
                         return;
                     }
                     const auto root = response.json.object();
                     const auto rows = root.value(QStringLiteral("data")).toObject()
                                           .value(QStringLiteral("song_list"));
                     if (!rows.isArray())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("每日推荐响应缺少歌曲列表"));
                         return;
                     }
                     const auto tracks = parseTracks(rows.toArray(), TrackShape::Daily);
                     if (tracks.size() != rows.toArray().size())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("每日推荐歌曲缺少必要信息"));
                         return;
                     }
                     callback(tracks, {}, {});
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
                     const auto root = response.json.object();
                     const auto rows = root.value(QStringLiteral("data")).toObject()
                                           .value(QStringLiteral("info"));
                     if (!rows.isArray())
                     {
                         callback({}, QStringLiteral("排行榜响应缺少列表"));
                         return;
                     }
                     QVariantList entries;
                     for (const auto &value : rows.toArray())
                     {
                         const auto row = value.toObject();
                         const auto id = stringField(row, {"rankid"});
                         const auto title = stringField(row, {"rankname"});
                         if (id.isEmpty() || title.isEmpty())
                         {
                             callback({}, QStringLiteral("排行榜缺少有效 ID 或标题"));
                             return;
                         }
                         entries.append(QVariantMap{
                             {"kind", "rank"},
                             {"id", id},
                             {"title", title},
                             {"name", title},
                             {"subtitle", stringField(row, {"update_frequency"})},
                             {"coverUrl", normalizedCover(stringField(row, {"imgurl"}))}});
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
                     const auto root = response.json.object();
                     const auto data = root.value(QStringLiteral("data")).toObject();
                     const auto songlist = data.value(QStringLiteral("songlist"));
                     const auto rows = songlist.toArray();
                     const auto tracks = songlist.isArray()
                                             ? parseTracks(songlist.toArray(), TrackShape::Rank)
                                             : QList<Track>{};
                     bool totalOk = false;
                     const auto total = stringField(data, {"total"}).toLongLong(&totalOk);
                     const auto shape = rows.isEmpty()
                                            ? data.keys().mid(0, 8).join(QStringLiteral(", "))
                                            : rows.first().toObject().keys().mid(0, 8).join(
                                                  QStringLiteral(", "));
                     if (qEnvironmentVariableIsSet("MUSIC_RANK_TRACE"))
                         qInfo().noquote() << "rank/audio shape:" << shape
                                           << "rows:" << rows.size() << "parsed:" << tracks.size()
                                           << "total:" << total;
                     const bool validEmpty = rows.isEmpty() && total == 0;
                     if (!songlist.isArray() || !totalOk || total < 0 ||
                         (tracks.size() != rows.size() && !validEmpty))
                     {
                         qWarning().noquote() << "rank/audio SchemaMismatch shape:" << shape
                                              << "rows:" << rows.size() << "total:" << total;
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("榜单歌曲暂时无法显示，请重试"));
                         return;
                     }
                     callback(tracks, {}, {});
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
                     const auto rows = response.json.object().value(QStringLiteral("data"))
                                           .toObject().value(QStringLiteral("special_list"));
                     if (!rows.isArray())
                     {
                         callback({}, QStringLiteral("热门歌单响应缺少列表"));
                         return;
                     }
                     QVariantList entries;
                     for (const auto &value : rows.toArray())
                         if (const auto entry = playlistEntry(value.toObject(), false); !entry.isEmpty())
                             entries.append(entry);
                     callback(entries, entries.size() != rows.toArray().size()
                                           ? QStringLiteral("热门歌单缺少有效 ID 或标题") : QString{});
                 });
}
void KuGouApi::comments(const QString &kind, const QString &id,
                        std::function<void(QVariantList, QString, QString)> callback, int page)
{
    const QString path = kind == QStringLiteral("song") ? QStringLiteral("/comment/music")
                         : kind == QStringLiteral("album") ? QStringLiteral("/comment/album")
                         : kind == QStringLiteral("playlist") ? QStringLiteral("/comment/playlist")
                                                               : QString{};
    if (path.isEmpty())
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
                     const auto rows = response.json.object().value(QStringLiteral("list"));
                     if (!rows.isArray())
                     {
                         callback({}, QStringLiteral("SchemaMismatch"),
                                  QStringLiteral("评论响应缺少列表"));
                         return;
                     }
                     QVariantList result;
                     for (const auto &value : rows.toArray())
                         if (const auto item = commentEntry(value.toObject()); !item.isEmpty())
                             result.append(item);
                     if (result.size() != rows.toArray().size())
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
    if (path.isEmpty())
    {
        callback(QStringLiteral("InvalidArgument"), QStringLiteral("评论类型无效"));
        return;
    }
    QJsonObject body{{kind == QStringLiteral("song") ? QStringLiteral("mixsongid")
                                                     : QStringLiteral("id"), id},
                     {"content", content}};
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
                         track.hash = stringField(row, {"hash_128"});
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
            const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
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
                    const QString text = stringField(lyricResponse.json.object(), {"decodeContent"});
                    callback(text, text.isEmpty() ? QStringLiteral("歌词内容响应缺少 content")
                                                  : QString{});
                });
        });
}

void KuGouApi::userPlaylists(std::function<void(QList<Playlist>, QString, QString)> callback,
                             int page)
{
    const QString userId = m_client.cookieJar()->userId();
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
            const QJsonValue rows = root.value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("info"));
            if (!rows.isArray())
            {
                callback({}, QStringLiteral("SchemaMismatch"),
                         QStringLiteral("账号响应缺少歌单列表"));
                return;
            }
            QList<Playlist> result;
            for (const QJsonValue &value : rows.toArray())
            {
                const QJsonObject row = value.toObject();
                const QString globalId = stringField(row, {"global_collection_id"});
                const QString listId = stringField(row, {"listid"});
                const QString title = stringField(row, {"name"});
                if ((!globalId.isEmpty() || !listId.isEmpty()) && !title.isEmpty())
                {
                    Playlist playlist;
                    playlist.globalCollectionId = globalId;
                    playlist.listId = listId;
                    playlist.title = title;
                    playlist.cover = normalizedCover(stringField(row, {"pic"}));
                    playlist.description = stringField(row, {"intro"});
                    playlist.creatorUserId = stringField(row, {"list_create_userid"});
                    playlist.creatorListId = stringField(row, {"list_create_listid"});
                    playlist.trackCount = row.value(QStringLiteral("count")).toInt();
                    playlist.totalVersion = row.value(QStringLiteral("list_ver")).toInteger();
                    playlist.type = row.value(QStringLiteral("type")).toInt();
                    playlist.sort = row.value(QStringLiteral("sort")).toInt();
                    result.push_back(std::move(playlist));
                }
            }
            if (result.size() != rows.toArray().size())
            {
                callback({}, QStringLiteral("SchemaMismatch"),
                         QStringLiteral("歌单响应缺少必要 ID 或标题"));
                return;
            }
            callback(std::move(result), {}, {});
        });
}

void KuGouApi::playlistTracks(const QString &globalCollectionId, const QString &listId,
                              TrackPageCallback callback, int page)
{
    const bool userCloudPlaylist = !listId.isEmpty();
    QUrlQuery query;
    query.addQueryItem(userCloudPlaylist ? QStringLiteral("listid") : QStringLiteral("id"),
                       userCloudPlaylist ? listId : globalCollectionId);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    auto handleResponse = [userCloudPlaylist, page, callback = std::move(callback)](ApiClient::Response response) mutable
    {
        if (const auto error = endpointError(response); !error.code.isEmpty())
        {
            callback({}, error.code, error.message, false);
            return;
        }
        const QJsonObject root = response.json.object();
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        const auto rows = data.value(userCloudPlaylist ? QStringLiteral("info")
                                                      : QStringLiteral("songs"));
        const auto total = data.value(QStringLiteral("count")).toInteger(-1);
        if (total < 0 || (!rows.isArray() && !(userCloudPlaylist && rows.isNull() && total == 0)))
        {
            callback({}, QStringLiteral("SchemaMismatch"), QStringLiteral("歌单响应缺少歌曲列表"), false);
            return;
        }
        const auto tracks = parseTracks(rows.toArray(), TrackShape::Playlist);
        int shielded = 0;
        for (const auto &value : rows.toArray())
            if (const auto row = value.toObject(); row.contains(QStringLiteral("shield")) &&
                !row.contains(QStringLiteral("hash")))
                ++shielded;
        if (tracks.size() + shielded != rows.toArray().size())
        {
            callback({}, QStringLiteral("SchemaMismatch"),
                     QStringLiteral("歌单歌曲缺少必要 ID 或标题"), false);
            return;
        }
        callback(std::move(tracks), {}, {}, page * 30 < total);
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
                     const auto rows = response.json.object().value(QStringLiteral("data"));
                     if (!rows.isArray() || rows.toArray().isEmpty())
                     {
                         callback({}, QStringLiteral("歌单详情响应缺少列表"));
                         return;
                     }
                     auto detail = playlistEntry(rows.toArray().first().toObject(), true);
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
            const QString key = stringField(data, {"qrcode"});
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
                    const QString image = stringField(createData, {"base64"});
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
            auto tracks = parseTracks(songs.toArray(), TrackShape::Album);
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
    const QString name = playlist.value(QStringLiteral("title")).toString();
    const QString creatorUserId =
        playlist.value(QStringLiteral("creatorUserId")).toString();
    const QString creatorListId =
        playlist.value(QStringLiteral("creatorListId")).toString();
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
    const QJsonObject body{{"listid", listId},
                           {"total_ver", totalVersion},
                           {"type", type},
                           {"name", name},
                           {"intro", description}};
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
                const QString token = stringField(data, {"token"});
                const QString userId = stringField(data, {"userid"});
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
    m_authPlaybackRetryAfter = 0;
    ++m_resolveSessionGeneration;
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
    {
        m_userAuthAttempted = false;
        m_authPlaybackRetryAfter = 0;
    }
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
    m_authPlaybackRetryAfter = 0;
    ++m_resolveSessionGeneration;
    m_registrationState = RegistrationState::Unregistered;
    m_pendingResolves.clear();
    emit sessionInvalidated();
}
