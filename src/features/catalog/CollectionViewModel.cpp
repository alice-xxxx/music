#include "CollectionViewModel.h"
#include "CatalogService.h"
#include "api/KuGouApi.h"
#include <QPointer>

CollectionViewModel::CollectionViewModel(KuGouApi *api, CatalogService *catalog, QObject *parent)
    : QObject(parent), m_api(api), m_catalog(catalog)
{
    connect(catalog, &CatalogService::trackUpdated, this,
            [this](const Track &track)
            {
                bool updated = false;
                for (auto &row : m_tracks)
                    if (row.toMap().value("key").toString() == track.key)
                    {
                        row = track.toMap();
                        updated = true;
                    }
                if (updated)
                    emit changed();
            });
    connect(api, &KuGouApi::sessionInvalidated, this,
            [this]
            {
                ++m_generation;
                m_tracks.clear();
                m_backStack.clear();
                m_title.clear();
                m_cover.clear();
                m_description.clear();
                m_metadata.clear();
                m_error.clear();
                m_detailError.clear();
                m_id.clear();
                m_loading = false;
                m_detailLoading = false;
                m_hasMore = false;
                m_favoriteBusy = false;
                m_favorited = false;
                m_canUnfavorite = false;
                m_favoriteListId.clear();
                m_favoriteMessage.clear();
                emit changed();
                emit scopeReset();
            });
}
QVariantMap CollectionViewModel::source() const
{
    return {{"kind", m_kind},
            {"id", m_kind == QStringLiteral("playlist") ? QString{} : m_id},
            {"globalId", m_kind == QStringLiteral("playlist") ? m_id : QString{}},
            {"nextPage", m_page + 1},
            {"hasMore", m_hasMore},
            {"title", m_title}};
}
void CollectionViewModel::open(const QVariantMap &track)
{
    m_backStack.clear();
    openCollection(QStringLiteral("album"), track.value("albumId").toString(),
                   track.value("album").toString(), track.value("coverUrl").toString());
}
void CollectionViewModel::openArtist(const QVariantMap &artist)
{
    m_backStack.clear();
    openCollection(QStringLiteral("artist"), artist.value("id").toString(),
                   artist.value("name").toString(), {});
}
void CollectionViewModel::openPlaylist(const QVariantMap &playlist)
{
    m_backStack.clear();
    openCollection(QStringLiteral("playlist"), playlist.value("id").toString(),
                   playlist.value("title").toString(), playlist.value("coverUrl").toString(),
                   playlist);
}
void CollectionViewModel::openRank(const QVariantMap &rank)
{
    m_backStack.clear();
    openCollection(QStringLiteral("rank"), rank.value("id").toString(),
                   rank.value("title").toString(), rank.value("coverUrl").toString(), rank);
}
void CollectionViewModel::openNestedAlbum(const QVariantMap &track, qreal scrollPosition)
{
    if (track.value("albumId").toString().isEmpty() ||
        (m_kind == QStringLiteral("album") && track.value("albumId").toString() == m_id))
        return;
    m_backStack.append(QVariantMap{{"kind", m_kind},
                                   {"id", m_id},
                                   {"title", m_title},
                                   {"cover", m_cover},
                                   {"description", m_description},
                                   {"metadata", m_metadata},
                                   {"error", m_error},
                                   {"detailError", m_detailError},
                                   {"tracks", m_tracks},
                                   {"page", m_page},
                                   {"more", m_hasMore},
                                   {"favorited", m_favorited},
                                   {"canUnfavorite", m_canUnfavorite},
                                   {"favoriteListId", m_favoriteListId},
                                   {"position", scrollPosition}});
    if (m_backStack.size() > 8)
        m_backStack.removeFirst();
    openCollection(QStringLiteral("album"), track.value("albumId").toString(),
                   track.value("album").toString(), track.value("coverUrl").toString());
}
bool CollectionViewModel::goBack()
{
    if (m_backStack.isEmpty())
        return false;
    const auto state = m_backStack.takeLast().toMap();
    restoreState(state);
    return true;
}
QVariantMap CollectionViewModel::captureState() const
{
    return {{"kind", m_kind},
            {"id", m_id},
            {"title", m_title},
            {"cover", m_cover},
            {"description", m_description},
            {"metadata", m_metadata},
            {"error", m_error},
            {"detailError", m_detailError},
            {"tracks", m_tracks},
            {"page", m_page},
            {"more", m_hasMore},
            {"position", m_scrollPosition},
            {"favorited", m_favorited},
            {"canUnfavorite", m_canUnfavorite},
            {"favoriteListId", m_favoriteListId},
            {"backStack", m_backStack}};
}
void CollectionViewModel::restoreState(const QVariantMap &state)
{
    ++m_generation;
    if (state.contains("backStack"))
        m_backStack = state.value("backStack").toList();
    m_kind = state.value("kind").toString();
    m_id = state.value("id").toString();
    m_title = state.value("title").toString();
    m_cover = state.value("cover").toString();
    m_description = state.value("description").toString();
    m_metadata = state.value("metadata").toMap();
    m_tracks = state.value("tracks").toList();
    m_page = state.value("page").toInt();
    m_hasMore = state.value("more").toBool();
    m_scrollPosition = state.value("position").toReal();
    m_loading = false;
    m_detailLoading = false;
    m_error = state.value("error").toString();
    m_detailError = state.value("detailError").toString();
    m_favoriteBusy = false;
    m_favorited = state.value("favorited").toBool();
    m_canUnfavorite = state.value("canUnfavorite").toBool();
    m_favoriteListId = state.value("favoriteListId").toString();
    m_favoriteMessage.clear();
    emit changed();
    if (m_page == 0 && !m_id.isEmpty())
        request(1);
    if (m_kind == QStringLiteral("artist") && m_cover.isEmpty())
        requestArtistDetail();
    if (m_kind == QStringLiteral("playlist"))
    {
        requestPlaylistDetail();
        refreshFavoriteState();
    }
}
void CollectionViewModel::openCollection(const QString &kind, const QString &id,
                                         const QString &title, const QString &cover,
                                         const QVariantMap &metadata)
{
    if (id.isEmpty())
        return;
    if (id != m_id || kind != m_kind)
    {
        ++m_generation;
        m_id = id;
        m_kind = kind;
        m_title = title;
        m_cover = cover;
        m_description.clear();
        m_metadata = metadata;
        m_detailError.clear();
        m_detailLoading = false;
        m_scrollPosition = 0;
        m_tracks.clear();
        m_page = 0;
        m_hasMore = false;
        m_favoriteBusy = false;
        m_favorited = false;
        m_canUnfavorite = false;
        m_favoriteListId.clear();
        m_favoriteMessage.clear();
        request(1);
        if (m_kind == QStringLiteral("artist"))
            requestArtistDetail();
        else if (m_kind == QStringLiteral("playlist"))
        {
            requestPlaylistDetail();
            refreshFavoriteState();
        }
    }
    emit opened();
}
void CollectionViewModel::loadMore()
{
    if (m_hasMore && !m_loading)
        request(m_page + 1);
}
void CollectionViewModel::retry()
{
    if (!m_loading && !m_id.isEmpty())
    {
        if (m_page == 0 || !m_error.isEmpty())
            request(m_page + 1);
        if (m_kind == QStringLiteral("artist") && !m_detailLoading &&
            (!m_detailError.isEmpty() || m_cover.isEmpty()))
            requestArtistDetail();
        if (m_kind == QStringLiteral("playlist") && !m_detailLoading &&
            (!m_detailError.isEmpty() || m_metadata.value("creatorListId").toString().isEmpty()))
            requestPlaylistDetail();
    }
}
void CollectionViewModel::requestArtistDetail()
{
    if (m_detailLoading)
        return;
    m_detailLoading = true;
    m_detailError.clear();
    emit changed();
    const auto generation = m_generation;
    m_api->artistDetail(m_id,
                        [this, guard = QPointer<CollectionViewModel>(this),
                         generation](QVariantMap detail, QString error)
                        {
                            if (!guard || generation != m_generation)
                                return;
                            m_detailLoading = false;
                            if (!error.isEmpty())
                                m_detailError = error;
                            else
                            {
                                m_title = detail.value("title").toString();
                                m_cover = detail.value("cover").toString();
                                m_description = detail.value("description").toString();
                            }
                            emit changed();
                        });
}
void CollectionViewModel::requestPlaylistDetail()
{
    if (m_detailLoading || m_kind != QStringLiteral("playlist"))
        return;
    m_detailLoading = true;
    m_detailError.clear();
    emit changed();
    const auto generation = m_generation;
    m_api->playlistDetail(
        m_id,
        [this, guard = QPointer<CollectionViewModel>(this), generation](QVariantMap detail,
                                                                        QString error)
        {
            if (!guard || generation != m_generation)
                return;
            m_detailLoading = false;
            if (!error.isEmpty())
                m_detailError = error;
            else
            {
                for (auto it = detail.cbegin(); it != detail.cend(); ++it)
                    if (!it.value().toString().isEmpty())
                        m_metadata.insert(it.key(), it.value());
                m_title = detail.value("title", m_title).toString();
                m_cover = detail.value("coverUrl", m_cover).toString();
                m_description = detail.value("description").toString();
            }
            emit changed();
        });
}
void CollectionViewModel::refreshFavoriteState(int page)
{
    if (m_kind != QStringLiteral("playlist") || !m_api->authenticated())
        return;
    const auto generation = m_generation;
    if (page == 1)
    {
        m_favoriteBusy = true;
        m_favorited = false;
        m_canUnfavorite = false;
        m_favoriteListId.clear();
        emit changed();
    }
    m_api->userPlaylists(
        [this, guard = QPointer<CollectionViewModel>(this), generation,
         page](QList<KuGouApi::Playlist> rows, QString code, QString message)
        {
            if (!guard || generation != m_generation)
                return;
            if (!code.isEmpty())
            {
                m_favoriteBusy = false;
                m_favoriteMessage = message;
                emit changed();
                return;
            }
            for (const auto &playlist : rows)
                if (playlist.globalCollectionId == m_id)
                {
                    m_favorited = true;
                    m_canUnfavorite = playlist.type == 1 && !playlist.listId.isEmpty();
                    m_favoriteListId = playlist.listId;
                    m_favoriteBusy = false;
                    emit changed();
                    return;
                }
            if (rows.size() == 30 && page < 100)
            {
                refreshFavoriteState(page + 1);
                return;
            }
            m_favoriteBusy = false;
            emit changed();
        },
        page);
}
void CollectionViewModel::toggleFavorite()
{
    if (m_kind != QStringLiteral("playlist") || m_favoriteBusy)
        return;
    if (m_favorited && !m_canUnfavorite)
        return;
    if (!m_api->authenticated())
    {
        emit loginRequired();
        return;
    }
    m_favoriteBusy = true;
    m_favoriteMessage = m_favorited ? QStringLiteral("正在取消收藏…")
                                    : QStringLiteral("正在收藏歌单…");
    emit changed();
    const auto generation = m_generation;
    auto callback = [this, guard = QPointer<CollectionViewModel>(this), generation](QString code,
                                                                                    QString message)
    {
        if (!guard || generation != m_generation)
            return;
        if (!code.isEmpty())
        {
            m_favoriteBusy = false;
            m_favoriteMessage = message;
            emit changed();
            return;
        }
        m_favoriteMessage = m_favorited ? QStringLiteral("已取消收藏")
                                        : QStringLiteral("已收藏到音乐库");
        refreshFavoriteState();
    };
    if (m_favorited)
        m_api->deletePlaylist(m_favoriteListId, std::move(callback));
    else
        m_api->favoritePlaylist(m_metadata, std::move(callback));
}
void CollectionViewModel::request(int page)
{
    m_loading = true;
    m_error.clear();
    emit changed();
    const auto generation = m_generation;
    auto pageCallback = [this, guard = QPointer<CollectionViewModel>(this), generation,
                         page](QList<Track> rows, QString code, QString message, bool hasMore)
    {
        if (!guard || generation != m_generation)
            return;
        m_loading = false;
        if (!code.isEmpty())
        {
            m_error = message;
            emit changed();
            return;
        }
        m_page = page;
        m_hasMore = hasMore;
        if (!rows.isEmpty() && m_kind == QStringLiteral("album"))
        {
            m_title = rows.first().album;
            m_cover = rows.first().coverUrl;
        }
        for (const auto &track : rows)
            m_tracks.append(m_catalog->remember(track).toMap());
        emit changed();
    };
    auto callback = [pageCallback](QList<Track> rows, QString code, QString message) mutable
    {
        const bool hasMore = rows.size() == 30;
        pageCallback(std::move(rows), std::move(code), std::move(message), hasMore);
    };
    if (m_kind == QStringLiteral("artist"))
        m_api->artistTracks(m_id, callback, page);
    else if (m_kind == QStringLiteral("rank"))
        m_api->rankTracks(m_id, callback, page);
    else if (m_kind == QStringLiteral("playlist"))
        m_api->playlistTracks(m_id, {}, pageCallback, page);
    else
        m_api->albumTracks(m_id, callback, page);
}
