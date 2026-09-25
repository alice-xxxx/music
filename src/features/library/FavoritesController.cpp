#include "FavoritesController.h"
#include "api/KuGouApi.h"
#include "features/account/SessionManager.h"
#include <QPointer>
#include <algorithm>

namespace
{
const QString favoriteName = QStringLiteral("音乐 · 喜欢的音乐");
}
FavoritesController::FavoritesController(KuGouApi *api, SessionManager *session, QObject *parent)
    : QObject(parent), m_api(api), m_session(session)
{
    connect(api, &KuGouApi::sessionInvalidated, this,
            [this]
            {
                ++m_generation;
                m_listId.clear();
                m_tracks.clear();
                m_readTracks.clear();
                m_busy = false;
                m_uncertain = false;
                m_creationPending = false;
                if (!m_waitingLogin)
                    m_pending = {};
                emit changed();
            });
    connect(session, &SessionManager::authenticatedChanged, this,
            [this]
            {
                if (m_waitingLogin && m_session->authenticated())
                {
                    m_waitingLogin = false;
                    m_busy = true;
                    emit resumeTrackRequested(m_pending.toMap());
                    discover(1);
                }
            });
}
bool FavoritesController::liked() const
{
    return std::any_of(m_tracks.cbegin(), m_tracks.cend(),
                       [this](const Track &track) { return track.key == m_current.key; });
}
QVariantList FavoritesController::tracks() const
{
    QVariantList rows;
    for (const auto &track : m_tracks)
        rows.append(track.toMap());
    return rows;
}
void FavoritesController::setCurrentTrack(const QVariantMap &track)
{
    const QString previous = m_current.key;
    m_current = Track::fromMap(track);
    emit changed();
    if (previous != m_current.key && !m_current.key.isEmpty() && m_session->authenticated() &&
        !m_busy)
        refresh();
}
void FavoritesController::toggle()
{
    if (m_uncertain)
    {
        refresh();
        return;
    }
    if (m_busy || m_current.key.isEmpty())
        return;
    m_pending = m_current;
    m_wantLiked = !liked();
    if (!m_session->authenticated())
    {
        m_waitingLogin = true;
        emit loginRequested();
        return;
    }
    m_busy = true;
    m_message = QStringLiteral("正在确认喜欢状态…");
    emit changed();
    if (m_listId.isEmpty())
        discover(1);
    else
        readTracks(1);
}
void FavoritesController::refresh()
{
    if (m_busy || !m_session->authenticated())
        return;
    if (m_uncertain)
    {
        m_busy = true;
        emit changed();
        readTracks(1, true);
        return;
    }
    m_pending = {};
    m_busy = true;
    m_message = QStringLiteral("正在加载喜欢的音乐…");
    emit changed();
    if (m_listId.isEmpty())
        discover(1);
    else
        readTracks(1);
}
void FavoritesController::cancelPending()
{
    if (!m_waitingLogin)
        return;
    m_waitingLogin = false;
    m_pending = {};
    emit changed();
}
void FavoritesController::fail(const QString &message)
{
    m_message = message;
    m_busy = false;
    emit changed();
}
void FavoritesController::discover(int page, bool afterCreate)
{
    const auto generation = m_generation;
    m_api->userPlaylists(
        [this, guard = QPointer<FavoritesController>(this), generation, page,
         afterCreate](QList<KuGouApi::Playlist> rows, QString code, QString message)
        {
            if (!guard || generation != m_generation)
                return;
            if (!code.isEmpty())
            {
                fail(message);
                return;
            }
            for (const auto &playlist : rows)
                if (playlist.title == favoriteName && !playlist.listId.isEmpty())
                {
                    m_listId = playlist.listId;
                    m_creationPending = false;
                    readTracks(1);
                    return;
                }
            if (rows.size() == 30 && page < 100)
            {
                discover(page + 1, afterCreate);
                return;
            }
            if (rows.size() == 30)
            {
                fail(QStringLiteral("歌单过多，尚未完成喜欢歌单查找"));
                return;
            }
            if (m_pending.key.isEmpty())
            {
                m_tracks.clear();
                fail(QStringLiteral("还没有喜欢的音乐"));
                return;
            }
            if (afterCreate || m_creationPending)
            {
                fail(QStringLiteral("创建结果待确认，请刷新后再操作"));
                return;
            }
            m_creationPending = true;
            m_api->createPlaylist(favoriteName,
                                  [this, guard, generation](QString code, QString message)
                                  {
                                      if (!guard || generation != m_generation)
                                          return;
                                      if (!code.isEmpty() && code != QStringLiteral("Timeout") &&
                                          code != QStringLiteral("Unavailable"))
                                      {
                                          fail(message);
                                          return;
                                      }
                                      discover(1, true);
                                  });
        },
        page);
}
void FavoritesController::readTracks(int page, bool confirmWrite)
{
    if (page == 1)
        m_readTracks.clear();
    const auto generation = m_generation;
    m_api->playlistTracks(
        {}, m_listId,
        [this, guard = QPointer<FavoritesController>(this), generation, page,
         confirmWrite](QList<Track> rows, QString code, QString message)
        {
            if (!guard || generation != m_generation)
                return;
            if (!code.isEmpty())
            {
                fail(confirmWrite ? QStringLiteral("结果待确认，请刷新喜欢列表") : message);
                return;
            }
            m_readTracks.append(rows);
            if (rows.size() == 30 && page < 500)
            {
                readTracks(page + 1, confirmWrite);
                return;
            }
            if (rows.size() == 30)
            {
                fail(QStringLiteral("喜欢列表过大，尚未完成状态核对"));
                return;
            }
            m_tracks = m_readTracks;
            if (confirmWrite)
            {
                const bool present =
                    std::any_of(m_tracks.cbegin(), m_tracks.cend(),
                                [this](const Track &track) { return track.key == m_pending.key; });
                const bool confirmed = present == m_wantLiked;
                m_uncertain = !confirmed;
                if (confirmed)
                    m_pending = {};
                fail(confirmed ? QStringLiteral("喜欢状态已更新")
                               : QStringLiteral("服务尚未确认更改，请刷新后重试"));
            }
            else if (!m_pending.key.isEmpty())
                write();
            else
                fail({});
        },
        page);
}
void FavoritesController::write()
{
    const auto existing =
        std::find_if(m_tracks.cbegin(), m_tracks.cend(),
                     [this](const Track &track) { return track.key == m_pending.key; });
    if ((existing != m_tracks.cend()) == m_wantLiked)
    {
        m_pending = {};
        fail({});
        return;
    }
    const auto generation = m_generation;
    auto callback = [this, guard = QPointer<FavoritesController>(this), generation](QString code,
                                                                                    QString)
    {
        if (!guard || generation != m_generation)
            return;
        if (!code.isEmpty() && code != QStringLiteral("Timeout") &&
            code != QStringLiteral("Unavailable"))
        {
            m_uncertain = true;
            fail(QStringLiteral("结果待确认，请重新查询喜欢状态"));
            return;
        }
        m_message = QStringLiteral("正在核对服务结果…");
        m_uncertain = true;
        emit changed();
        readTracks(1, true);
    };
    if (m_wantLiked)
        m_api->addPlaylistTrack(m_listId, m_pending, callback);
    else
        m_api->removePlaylistTrack(m_listId, existing->fileId, callback);
}
