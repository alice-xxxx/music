#include <QPointer>
#include "LibraryViewModel.h"
#include "api/KuGouApi.h"
#include "features/catalog/CatalogService.h"

LibraryViewModel::LibraryViewModel(KuGouApi *api, CatalogService *catalog, QObject *parent)
    : QObject(parent), m_api(api), m_catalog(catalog)
{
    connect(m_catalog, &CatalogService::trackUpdated, this,
            [this](const Track &track)
            {
                bool updated = false;
                for (auto &row : m_tracks)
                    if (row.toMap().value("key").toString() == track.key)
                    {
                        auto existing = Track::fromMap(row.toMap());
                        existing.coverUrl = track.coverUrl;
                        existing.album = track.album;
                        existing.albumId = track.albumId;
                        row = existing.toMap();
                        updated = true;
                    }
                if (updated)
                    emit changed();
            });
    connect(m_api, &KuGouApi::sessionInvalidated, this, &LibraryViewModel::invalidateSession);
}
void LibraryViewModel::load()
{
    if (m_loading)
        return;
    ++m_generation;
    requestPlaylists(1);
}
void LibraryViewModel::requestPlaylists(int page)
{
    const quint64 generation = m_generation;
    m_loading = true;
    m_error.clear();
    emit changed();
    m_api->userPlaylists(
        [this, guard = QPointer<LibraryViewModel>(this), generation,
         page](QList<KuGouApi::Playlist> rows, QString code, QString message)
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
            if (page == 1)
                m_playlists.clear();
            m_playlistPage = page;
            m_morePlaylists = rows.size() == 30;
            for (const auto &row : rows)
                m_playlists.append(QVariantMap{{"globalCollectionId", row.globalCollectionId},
                                               {"listId", row.listId},
                                               {"title", row.title},
                                               {"cover", row.cover},
                                               {"description", row.description},
                                               {"creatorUserId", row.creatorUserId},
                                               {"creatorListId", row.creatorListId},
                                               {"count", row.trackCount},
                                               {"totalVersion", row.totalVersion},
                                               {"type", row.type},
                                               {"sort", row.sort}});
            emit changed();
        },
        page);
}
void LibraryViewModel::openPlaylist(int index)
{
    if (index < 0 || index >= m_playlists.size())
        return;
    ++m_generation;
    m_selected = m_playlists.at(index).toMap();
    m_title = m_selected.value("title").toString();
    m_tracks.clear();
    m_trackPage = 0;
    m_moreTracks = false;
    requestTracks(1);
}
void LibraryViewModel::requestTracks(int page)
{
    const auto generation = m_generation;
    m_loading = true;
    m_error.clear();
    emit changed();
    m_api->playlistTracks(
        m_selected.value("globalCollectionId").toString(), m_selected.value("listId").toString(),
        [this, guard = QPointer<LibraryViewModel>(this), generation,
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
            if (page == 1)
                m_tracks.clear();
            m_trackPage = page;
            m_moreTracks = hasMore;
            for (const auto &track : rows)
                m_tracks.append(m_catalog->remember(track).toMap());
            emit changed();
        },
        page);
}
void LibraryViewModel::loadMore()
{
    if (m_loading)
        return;
    if (m_title.isEmpty())
    {
        if (m_morePlaylists)
            requestPlaylists(m_playlistPage + 1);
    }
    else if (m_moreTracks)
        requestTracks(m_trackPage + 1);
}
void LibraryViewModel::retry()
{
    if (m_loading)
        return;
    if (m_title.isEmpty())
        requestPlaylists(qMax(1, m_playlistPage + 1));
    else
        requestTracks(qMax(1, m_trackPage + 1));
}
QVariantMap LibraryViewModel::source() const
{
    return {{"kind", "playlist"},
            {"id", m_selected.value("listId")},
            {"globalId", m_selected.value("globalCollectionId")},
            {"nextPage", m_trackPage + 1},
            {"hasMore", m_moreTracks},
            {"title", m_title}};
}
void LibraryViewModel::closePlaylist()
{
    ++m_generation;
    m_title.clear();
    m_selected.clear();
    m_tracks.clear();
    m_error.clear();
    m_loading = false;
    emit changed();
}
void LibraryViewModel::invalidateSession()
{
    ++m_actionGeneration;
    m_actionBusy = false;
    m_actionUncertain = false;
    m_actionSucceeded = false;
    m_actionMessage.clear();
    emit actionChanged();
    ++m_generation;
    m_playlists.clear();
    m_tracks.clear();
    m_title.clear();
    m_error.clear();
    m_loading = false;
    emit changed();
}

void LibraryViewModel::createPlaylist(const QString &name)
{
    if (m_actionBusy || m_actionUncertain)
        return;
    ++m_actionGeneration;
    m_actionName = name;
    m_actionKind = QStringLiteral("create");
    m_actionSucceeded = false;
    m_actionBusy = true;
    m_actionMessage = QStringLiteral("正在创建歌单…");
    emit actionChanged();
    const auto generation = m_actionGeneration;
    m_api->createPlaylist(
        m_actionName,
        [this, guard = QPointer<LibraryViewModel>(this), generation](QString code, QString message)
        {
            if (guard && generation == m_actionGeneration)
                finishWrite(code, message);
        });
}
void LibraryViewModel::deleteSelectedPlaylist()
{
    if (m_actionBusy || m_actionUncertain || m_selected.value("listId").toString().isEmpty())
        return;
    ++m_actionGeneration;
    m_actionKind = QStringLiteral("delete");
    m_actionTarget = m_selected.value("listId").toString();
    m_actionName = m_title;
    m_actionSucceeded = false;
    m_actionBusy = true;
    m_actionMessage = QStringLiteral("正在删除歌单…");
    emit actionChanged();
    const auto generation = m_actionGeneration;
    m_api->deletePlaylist(
        m_actionTarget,
        [this, guard = QPointer<LibraryViewModel>(this), generation](QString code, QString message)
        {
            if (guard && generation == m_actionGeneration)
                finishWrite(code, message);
        });
}
void LibraryViewModel::updateSelectedPlaylist(const QString &name, const QString &description)
{
    if (m_actionBusy || m_actionUncertain ||
        m_selected.value("listId").toString().isEmpty())
        return;
    ++m_actionGeneration;
    m_actionKind = QStringLiteral("update");
    m_actionTarget = m_selected.value("listId").toString();
    m_actionName = name;
    m_actionDescription = description;
    m_actionSucceeded = false;
    m_actionBusy = true;
    m_actionMessage = QStringLiteral("正在更新歌单…");
    emit actionChanged();
    const auto generation = m_actionGeneration;
    m_api->updatePlaylist(
        m_actionTarget, m_selected.value("totalVersion").toLongLong(),
        m_selected.value("type").toInt(), m_actionName, m_actionDescription,
        [this, guard = QPointer<LibraryViewModel>(this), generation](QString code, QString message)
        {
            if (guard && generation == m_actionGeneration)
                finishWrite(code, message);
        });
}
void LibraryViewModel::addTrack(int playlistIndex, const QVariantMap &data)
{
    if (m_actionBusy || m_actionUncertain || playlistIndex < 0 ||
        playlistIndex >= m_playlists.size())
        return;
    const auto track = Track::fromMap(data);
    m_actionTarget = m_playlists.at(playlistIndex).toMap().value("listId").toString();
    if (m_actionTarget.isEmpty() || track.key.isEmpty())
        return;
    ++m_actionGeneration;
    m_addedTrack = track;
    m_actionName = m_playlists.at(playlistIndex).toMap().value("title").toString();
    m_actionKind = QStringLiteral("add");
    m_actionSucceeded = false;
    m_actionTrackKey = track.key;
    m_actionBusy = true;
    m_actionMessage = QStringLiteral("正在加入歌单…");
    emit actionChanged();
    writeAddedTrack();
}
void LibraryViewModel::writeAddedTrack()
{
    const auto generation = m_actionGeneration;
    m_api->addPlaylistTrack(
        m_actionTarget, m_addedTrack,
        [this, guard = QPointer<LibraryViewModel>(this), generation](QString code, QString message)
        {
            if (guard && generation == m_actionGeneration)
                finishWrite(code, message);
        });
}
void LibraryViewModel::removeTrack(int trackIndex)
{
    if (m_actionBusy || m_actionUncertain || trackIndex < 0 || trackIndex >= m_tracks.size())
        return;
    const auto track = Track::fromMap(m_tracks.at(trackIndex).toMap());
    m_actionTarget = m_selected.value("listId").toString();
    if (m_actionTarget.isEmpty())
        return;
    ++m_actionGeneration;
    m_actionKind = QStringLiteral("remove");
    m_actionName = track.title;
    m_actionSucceeded = false;
    m_actionTrackKey = track.key;
    m_actionFileId = track.fileId;
    m_actionBusy = true;
    m_actionMessage = QStringLiteral("正在移除歌曲…");
    emit actionChanged();
    const auto generation = m_actionGeneration;
    m_api->removePlaylistTrack(
        m_actionTarget, track.fileId,
        [this, guard = QPointer<LibraryViewModel>(this), generation](QString code, QString message)
        {
            if (guard && generation == m_actionGeneration)
                finishWrite(code, message);
        });
}
void LibraryViewModel::finishWrite(QString code, QString message)
{
    if (code.isEmpty())
    {
        completeAction(true);
        return;
    }
    if (code != QStringLiteral("Timeout") && code != QStringLiteral("Unavailable"))
    {
        m_actionBusy = false;
        m_actionSucceeded = false;
        m_actionMessage = message;
        emit actionChanged();
        return;
    }
    m_actionMessage = QStringLiteral("正在核对服务结果…");
    emit actionChanged();
    confirmPage(1);
}
void LibraryViewModel::confirmAction()
{
    if (m_actionBusy || !m_actionUncertain)
        return;
    m_actionBusy = true;
    emit actionChanged();
    confirmPage(1);
}
void LibraryViewModel::completeAction(bool confirmed)
{
    m_actionBusy = false;
    m_actionUncertain = !confirmed;
    m_actionSucceeded = confirmed;
    if (!confirmed)
        m_actionMessage = QStringLiteral("结果待确认，请查询状态后再操作");
    else if (m_actionKind == QStringLiteral("create"))
        m_actionMessage = QStringLiteral("已创建“%1”").arg(m_actionName);
    else if (m_actionKind == QStringLiteral("delete"))
        m_actionMessage = QStringLiteral("已删除“%1”").arg(m_actionName);
    else if (m_actionKind == QStringLiteral("add"))
        m_actionMessage = QStringLiteral("已添加到“%1”").arg(m_actionName);
    else if (m_actionKind == QStringLiteral("update"))
        m_actionMessage = QStringLiteral("已更新歌单“%1”").arg(m_actionName);
    else
        m_actionMessage = QStringLiteral("已从歌单移除“%1”").arg(m_actionName);
    emit actionChanged();
    if (!confirmed)
        return;
    if (m_actionKind == QStringLiteral("create"))
    {
        ++m_generation;
        requestPlaylists(1);
        return;
    }
    if (m_actionKind == QStringLiteral("update"))
    {
        m_title = m_actionName;
        m_selected.insert(QStringLiteral("title"), m_actionName);
        m_selected.insert(QStringLiteral("description"), m_actionDescription);
        for (auto &row : m_playlists)
        {
            auto playlist = row.toMap();
            if (playlist.value("listId").toString() == m_actionTarget)
            {
                playlist.insert(QStringLiteral("title"), m_actionName);
                playlist.insert(QStringLiteral("description"), m_actionDescription);
                row = playlist;
                break;
            }
        }
        emit changed();
        return;
    }
    if (m_actionKind == QStringLiteral("delete") &&
        m_selected.value("listId").toString() == m_actionTarget)
        closePlaylist();
    if ((m_actionKind == QStringLiteral("add") || m_actionKind == QStringLiteral("remove")) &&
        m_selected.value("listId").toString() == m_actionTarget)
    {
        ++m_generation;
        requestTracks(1);
    }
    else if (m_title.isEmpty())
    {
        ++m_generation;
        requestPlaylists(1);
    }
}
void LibraryViewModel::confirmPage(int page)
{
    const auto generation = m_actionGeneration;
    const auto guard = QPointer<LibraryViewModel>(this);
    if (m_actionKind == QStringLiteral("add") || m_actionKind == QStringLiteral("remove"))
    {
        m_api->playlistTracks(
            {}, m_actionTarget,
            [this, guard, generation, page](QList<Track> rows, QString code, QString, bool hasMore)
            {
                if (!guard || generation != m_actionGeneration)
                    return;
                if (!code.isEmpty())
                {
                    completeAction(false);
                    return;
                }
                for (const auto &track : rows)
                    if (m_actionKind == QStringLiteral("remove") ? track.fileId == m_actionFileId
                                                                 : track.key == m_actionTrackKey)
                    {
                        completeAction(m_actionKind != QStringLiteral("remove"));
                        return;
                    }
                if (hasMore && page < 500)
                {
                    confirmPage(page + 1);
                    return;
                }
                completeAction(!hasMore && m_actionKind == QStringLiteral("remove"));
            },
            page);
        return;
    }
    m_api->userPlaylists(
        [this, guard, generation, page](QList<KuGouApi::Playlist> rows, QString code, QString)
        {
            if (!guard || generation != m_actionGeneration)
                return;
            if (!code.isEmpty())
            {
                completeAction(false);
                return;
            }
            for (const auto &playlist : rows)
            {
                if (m_actionKind == QStringLiteral("delete") && playlist.listId == m_actionTarget)
                {
                    completeAction(false);
                    return;
                }
                if (m_actionKind == QStringLiteral("update") &&
                    playlist.listId == m_actionTarget && playlist.title == m_actionName)
                {
                    completeAction(true);
                    return;
                }
                if (m_actionKind == QStringLiteral("create") && playlist.title == m_actionName)
                {
                    completeAction(true);
                    return;
                }
            }
            if (rows.size() == 30 && page < 100)
            {
                confirmPage(page + 1);
                return;
            }
            if (rows.size() == 30)
            {
                completeAction(false);
                return;
            }
            completeAction(m_actionKind == QStringLiteral("delete"));
        },
        page);
}
