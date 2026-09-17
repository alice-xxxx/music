#include "CatalogService.h"
#include "api/KuGouApi.h"
#include <QPointer>
#include <QDateTime>

CatalogService::CatalogService(KuGouApi *api, QObject *parent) : QObject(parent), m_api(api)
{
    connect(api, &KuGouApi::sessionInvalidated, this, &CatalogService::clear);
}
Track CatalogService::remember(Track track)
{
    const auto previous = m_tracks.value(track.key);
    if (track.coverUrl.isEmpty())
        track.coverUrl = previous.coverUrl;
    if (track.albumId.isEmpty())
        track.albumId = previous.albumId;
    if (track.album.isEmpty())
        track.album = previous.album;
    if (track.artist.isEmpty())
        track.artist = previous.artist;
    if (track.artists.isEmpty())
        track.artists = previous.artists;
    if (!m_tracks.contains(track.key))
        m_order.enqueue(track.key);
    m_tracks.insert(track.key, track);
    while (m_order.size() > 1000)
    {
        const auto key = m_order.dequeue();
        m_tracks.remove(key);
        m_requested.remove(key);
        m_retryAfter.remove(key);
        m_failures.remove(key);
    }
    return track;
}
void CatalogService::ensureMetadata(const QString &key, bool current)
{
    if (current)
    {
        m_currentKey = key;
        if (m_waiting.removeAll(key))
            m_waiting.prepend(key);
    }
    if (m_retryAfter.value(key) > QDateTime::currentMSecsSinceEpoch())
        return;
    if (!m_tracks.contains(key) || m_requested.contains(key))
        return;
    const auto value = m_tracks.value(key);
    const bool complete = !value.coverUrl.isEmpty() && !value.artists.isEmpty();
    if ((complete && (!current || value.coverUrl.contains(QStringLiteral("{size}")))) ||
        value.hash.isEmpty())
        return;
    m_requested.insert(key);
    if (current)
        m_waiting.prepend(key);
    else
        m_waiting.enqueue(key);
    pump();
}
void CatalogService::cancelQueuedMetadata(const QString &key)
{
    if (key != m_currentKey && m_waiting.removeAll(key))
        m_requested.remove(key);
}
void CatalogService::clear()
{
    ++m_generation;
    m_tracks.clear();
    m_order.clear();
    m_requested.clear();
    m_waiting.clear();
    m_currentKey.clear();
    m_retryAfter.clear();
    m_failures.clear();
    m_active = 0;
}
void CatalogService::pump()
{
    while (m_active < 2 && !m_waiting.isEmpty())
    {
        const auto key = m_waiting.dequeue();
        if (!m_tracks.contains(key))
            continue;
        ++m_active;
        const auto generation = m_generation;
        m_api->trackMetadata(m_tracks.value(key),
                             [this, guard = QPointer<CatalogService>(this), key,
                              generation](Track updated, QString error)
                             {
                                 if (!guard || generation != m_generation)
                                     return;
                                 --m_active;
                                 if (!error.isEmpty())
                                 {
                                     m_requested.remove(key);
                                     m_retryAfter[key] = QDateTime::currentMSecsSinceEpoch() +
                                                         (++m_failures[key] < 2 ? 3000 : 60000);
                                 }
                                 if (error.isEmpty() && m_tracks.contains(key))
                                 {
                                     updated = remember(std::move(updated));
                                     emit trackUpdated(updated);
                                 }
                                 pump();
                             });
    }
}
