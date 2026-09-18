#include "PlaybackController.h"
#include "api/KuGouApi.h"
#include "features/catalog/CatalogService.h"
#include "storage/LocalStore.h"
#include <QAudioOutput>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QPointer>
#include <QSet>
#include <algorithm>

PlaybackController::PlaybackController(KuGouApi *api, CatalogService *catalog, LocalStore *store,
                                       QObject *parent)
    : QObject(parent), m_store(store), m_audioOutput(new QAudioOutput(this)), m_catalog(catalog),
      m_api(api)
{
    connect(this, &PlaybackController::queueChanged, this,
            [this] { m_queueModel.synchronize(queueItems()); });
    m_undoTimer.setSingleShot(true);
    m_undoTimer.setInterval(5000);
    connect(&m_undoTimer, &QTimer::timeout, this,
            [this]
            {
                m_undoQueue.clear();
                emit undoChanged();
            });
    m_prefetchExpiry.setSingleShot(true);
    connect(&m_prefetchExpiry, &QTimer::timeout, this,
            &PlaybackController::invalidatePrefetch);
    connect(m_catalog, &CatalogService::trackUpdated, this,
            [this](const Track &track)
            {
                bool queueUpdated = false;
                for (auto &item : m_queue)
                    if (item.track.key == track.key)
                    {
                        item.track = track;
                        queueUpdated = true;
                    }
                if (queueUpdated)
                    emit queueChanged();
                for (auto &item : m_history)
                    if (item.track.key == track.key)
                        item.track = track;
                emit historyChanged();
                if (m_track.key == track.key)
                {
                    m_track = track;
                    emit snapshotChanged();
                }
            });
    m_audioOutput->setVolume(QSettings().value(QStringLiteral("playback/volume"), 80).toInt() /
                             100.0F);
    m_repeatMode = QSettings().value(QStringLiteral("playback/repeatMode"), 0).toInt();
    m_shuffle = QSettings().value(QStringLiteral("playback/shuffle"), false).toBool();
    m_player.setAudioOutput(m_audioOutput);
    connect(&m_player, &AudioEngine::sourceReady, this, &PlaybackController::prepareSourcePlayback);
    connect(&m_player, &AudioEngine::audioMetadataChanged, this,
            &PlaybackController::audioMetadataChanged);
    connect(m_api, &KuGouApi::sessionInvalidated, this,
            &PlaybackController::invalidateOnlineSession);
    connect(&m_player, &AudioEngine::positionChanged, this, &PlaybackController::positionChanged);
    connect(&m_player, &AudioEngine::durationChanged, this, &PlaybackController::durationChanged);
    connect(&m_player, &AudioEngine::seekableChanged, this, &PlaybackController::seekableChanged);
    connect(&m_player, &AudioEngine::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state)
            {
                if (state == QMediaPlayer::PlayingState && m_currentIndex >= 0 &&
                    m_currentIndex < m_queue.size())
                    recordHistory(m_queue.at(m_currentIndex));
                emit snapshotChanged();
            });
    connect(&m_player, &AudioEngine::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status)
            {
                applyResumePosition();
                emit snapshotChanged();
                if (status == QMediaPlayer::EndOfMedia && m_desiredPlaying && !m_resolving &&
                    !m_recovering && m_errorMessage.isEmpty())
                {
                    if (m_repeatMode == 2)
                    {
                        m_player.setPosition(0);
                        m_player.play();
                    }
                    else
                        next();
                }
            });
    connect(&m_player, &AudioEngine::errorOccurred, this, &PlaybackController::handlePlaybackError);
    connect(&m_player, &AudioEngine::outputDeviceChanged, this,
            &PlaybackController::handleOutputDeviceChange);
    connect(&m_player, &AudioEngine::seekableChanged, this, [this] { applyResumePosition(); });
    connect(&m_player, &AudioEngine::positionChanged, this,
            [this](qint64 value)
            {
                if (!m_resolving && !m_recovering &&
                    m_player.playbackState() != QMediaPlayer::StoppedState)
                    m_lastGoodPosition = value;
                maybePrefetchNextMedia();
            });
    m_recoveryTimer.setSingleShot(true);
    connect(&m_recoveryTimer, &QTimer::timeout, this,
            [this]
            {
                if (m_recovering && m_desiredPlaying)
                    resolveCurrentMedia();
            });
    m_resumeDeadline.setSingleShot(true);
    m_resumeDeadline.setInterval(12000);
    connect(&m_resumeDeadline, &QTimer::timeout, this,
            [this]
            {
                if (!m_waitingForSeek)
                    return;
                clearCurrentSource();
                setError(QStringLiteral("此资源暂时无法恢复到中断位置，请重试"));
                emit snapshotChanged();
            });
    connect(&m_player, &AudioEngine::positionChanged, this,
            [this] { m_lyricLines.setPosition(position()); });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(250);
    connect(&m_saveTimer, &QTimer::timeout, this, &PlaybackController::saveSnapshot);
    connect(this, &PlaybackController::queueChanged, this,
            [this]
            {
                if (m_restoring)
                    m_queueChangedDuringRestore = true;
                else
                    m_saveTimer.start();
            });
    connect(this, &PlaybackController::historyChanged, this,
            [this]
            {
                if (!m_restoring)
                    m_saveTimer.start();
            });
    connect(this, &PlaybackController::snapshotChanged, this,
            [this]
            {
                if (!m_restoring)
                    m_saveTimer.start();
            });
    m_positionSaveTimer.setInterval(5000);
    connect(&m_positionSaveTimer, &QTimer::timeout, this, &PlaybackController::saveSnapshot);
    m_positionSaveTimer.start();
    connect(m_store, &LocalStore::saved, this,
            [this](const QString &scope, const QString &error)
            {
                if (scope == m_scope)
                {
                    m_storageError = error;
                    emit storageErrorChanged();
                }
            });
    connect(m_store, &LocalStore::loaded, this,
            [this](const QString &scope, const QByteArray &snapshot, const QString &error)
            {
                if (scope != m_scope)
                    return;
                m_restoring = false;
                m_storageError = error;
                m_storageWritable = error.isEmpty();
                if (error.isEmpty() && !snapshot.isEmpty())
                {
                    QJsonParseError parseError;
                    const auto document = QJsonDocument::fromJson(snapshot, &parseError);
                    if (parseError.error != QJsonParseError::NoError || !document.isObject())
                    {
                        m_storageWritable = false;
                        m_storageError =
                            QStringLiteral("Stored music data is damaged; original data retained");
                    }
                    else
                    {
                        const auto root = document.object();
                        if (!m_queueChangedDuringRestore && m_generation == m_restoreGeneration)
                        {
                            m_source = root.value("source").toObject().toVariantMap();
                            m_queue.clear();
                            QSet<quint64> restoredIds;
                            for (const auto &value : root.value("queue").toArray())
                            {
                                const auto map = value.toObject().toVariantMap();
                                Track track = Track::fromMap(map);
                                if (track.key.isEmpty() || track.hash.isEmpty() ||
                                    m_queue.size() >= 1000)
                                    continue;
                                auto id = map.value("queueItemId").toString().toULongLong();
                                if (id == 0 || restoredIds.contains(id))
                                    id = ++m_nextQueueItemId;
                                m_nextQueueItemId = qMax(m_nextQueueItemId, id);
                                restoredIds.insert(id);
                                m_queue.append({track, id});
                            }
                            m_currentIndex = root.value("index").toInt(-1);
                            if (m_currentIndex < 0 || m_currentIndex >= m_queue.size())
                                m_currentIndex = -1;
                            m_track =
                                m_currentIndex >= 0 ? m_queue.at(m_currentIndex).track : Track{};
                            m_shuffle = root.value("shuffle").toBool(m_shuffle);
                            m_repeatMode =
                                qBound(0, root.value("repeatMode").toInt(m_repeatMode), 2);
                            m_shuffleRound.clear();
                            m_playedItemIds.clear();
                            for (const auto &value : root.value("shuffleRound").toArray())
                            {
                                const auto id = value.toString().toULongLong();
                                if (restoredIds.contains(id) && !m_shuffleRound.contains(id))
                                    m_shuffleRound.append(id);
                            }
                            for (const auto &value : root.value("playedItems").toArray())
                            {
                                const auto id = value.toString().toULongLong();
                                if (restoredIds.contains(id) && m_playedItemIds.size() < 1000)
                                    m_playedItemIds.append(id);
                            }
                            if (m_shuffle && m_currentIndex >= 0 &&
                                !m_shuffleRound.contains(m_queue.at(m_currentIndex).queueItemId))
                                m_shuffleRound.append(m_queue.at(m_currentIndex).queueItemId);
                            m_pendingSeek = qMax<qint64>(0, root.value("position").toInteger());
                        }
                        if (!m_historyChangedDuringRestore)
                        {
                            m_history.clear();
                            for (const auto &value : root.value("history").toArray())
                            {
                                Track track = Track::fromMap(value.toObject().toVariantMap());
                                if (!track.key.isEmpty() && !track.hash.isEmpty() &&
                                    m_history.size() < 100)
                                    m_history.append({track, 0});
                            }
                        }
                        emit queueChanged();
                        emit historyChanged();
                        emit snapshotChanged();
                        emit positionChanged();
                        emit shuffleChanged();
                        emit repeatModeChanged();
                    }
                }
                emit storageErrorChanged();
            });
    restoreScope();
}
QString PlaybackController::title() const
{
    return m_track.title;
}
PlaybackController::~PlaybackController()
{
    saveSnapshot();
}
QString PlaybackController::artist() const
{
    return m_track.artist;
}
bool PlaybackController::hasCurrentTrack() const
{
    return m_currentIndex >= 0 || !m_track.key.isEmpty();
}
bool PlaybackController::playing() const
{
    return m_player.playbackState() == QMediaPlayer::PlayingState;
}
qint64 PlaybackController::position() const
{
    return m_pendingSeek >= 0 ? m_pendingSeek : m_player.position();
}
qint64 PlaybackController::duration() const
{
    return m_player.duration() > 0 ? m_player.duration() : qMax<qint64>(0, m_track.durationMs);
}
bool PlaybackController::seekable() const
{
    return m_player.isSeekable();
}
int PlaybackController::queueCount() const
{
    return m_queue.size();
}
QVariantList PlaybackController::queueItems() const
{
    QVariantList rows;
    for (const auto &item : m_queue)
    {
        auto row = item.track.toMap();
        row.insert(QStringLiteral("queueItemId"), QString::number(item.queueItemId));
        rows.append(row);
    }
    return rows;
}
QString PlaybackController::errorMessage() const
{
    return m_errorMessage;
}
int PlaybackController::volume() const
{
    return qRound(m_audioOutput->volume() * 100.0F);
}
QString PlaybackController::lyrics() const
{
    return m_lyrics;
}
int PlaybackController::repeatMode() const
{
    return m_repeatMode;
}
void PlaybackController::setRepeatMode(int mode)
{
    mode = qBound(0, mode, 2);
    if (m_repeatMode == mode)
        return;
    m_repeatMode = mode;
    invalidatePrefetch();
    QSettings().setValue(QStringLiteral("playback/repeatMode"), mode);
    emit repeatModeChanged();
}
bool PlaybackController::shuffle() const
{
    return m_shuffle;
}
void PlaybackController::setShuffle(bool enabled)
{
    if (m_shuffle == enabled)
        return;
    m_shuffle = enabled;
    invalidatePrefetch();
    m_shuffleRound.clear();
    if (enabled && m_currentIndex >= 0)
        m_shuffleRound.append(m_queue.at(m_currentIndex).queueItemId);
    QSettings().setValue(QStringLiteral("playback/shuffle"), enabled);
    emit shuffleChanged();
}
void PlaybackController::setRequestedQuality(const QString &quality)
{
    if (m_requestedQuality == quality)
        return;
    m_requestedQuality = quality;
    invalidatePrefetch();
}
void PlaybackController::setVolume(int percent)
{
    percent = qBound(0, percent, 100);
    if (volume() == percent)
        return;
    m_audioOutput->setVolume(percent / 100.0F);
    QSettings().setValue(QStringLiteral("playback/volume"), percent);
    emit volumeChanged();
}
void PlaybackController::playSong(const QVariantMap &data)
{
    Track track = Track::fromMap(data);
    if (track.key.isEmpty() || track.hash.isEmpty())
        return;
    track = m_catalog->remember(track);
    if (track.key == m_track.key && m_currentIndex >= 0)
    {
        emit detailRequested();
        return;
    }
    for (int step = 1; step <= m_queue.size(); ++step)
    {
        const int index = (m_currentIndex + step) % m_queue.size();
        if (m_queue.at(index).track.key == track.key)
        {
            playIndex(index);
            return;
        }
    }
    invalidateUndo();
    cancelCollection();
    const int insertion = m_currentIndex + 1;
    invalidatePrefetch();
    m_queue.insert(insertion, {track, ++m_nextQueueItemId});
    emit queueChanged();
    playIndex(insertion);
}
void PlaybackController::togglePlayback()
{
    m_resumeAfterInterruption = false;
    if (!hasCurrentTrack())
        return;
    if (!m_errorMessage.isEmpty() && !m_resolving && m_currentIndex >= 0)
    {
        m_pendingSeek = position();
        playIndex(m_currentIndex);
        return;
    }
    if (!m_desiredPlaying && !m_resolving && m_player.source().isEmpty() && m_currentIndex >= 0)
    {
        playIndex(m_currentIndex);
        return;
    }
    m_desiredPlaying = !m_desiredPlaying;
    if (!m_desiredPlaying && m_recoveryTimer.isActive())
    {
        m_recoveryTimer.stop();
        m_recovering = false;
        m_resolving = false;
    }
    if (!m_desiredPlaying)
    {
        m_player.pause();
        emit snapshotChanged();
        return;
    }

    // Activate the native background-audio session/service before playback starts.
    emit snapshotChanged();
    if (!m_player.source().isEmpty() && !m_waitingForSeek)
        m_player.play();
}
void PlaybackController::next()
{
    if (m_queue.isEmpty())
        return;
    if (!m_prefetchedMedia.isEmpty())
    {
        const auto prepared = std::find_if(
            m_queue.cbegin(), m_queue.cend(),
            [this](const QueueItem &item) { return item.queueItemId == m_prefetchedQueueItemId; });
        if (prepared != m_queue.cend())
        {
            const int index = static_cast<int>(std::distance(m_queue.cbegin(), prepared));
            const bool eligible = m_shuffle
                                      ? index != m_currentIndex &&
                                            (m_prefetchedStartsShuffleRound ||
                                             !m_shuffleRound.contains(prepared->queueItemId))
                                      : index == m_currentIndex + 1 ||
                                            (m_repeatMode == 1 &&
                                             m_currentIndex == m_queue.size() - 1 && index == 0);
            if (eligible)
            {
                if (m_prefetchedStartsShuffleRound)
                {
                    m_shuffleRound.clear();
                    m_shuffleRound.append(m_queue.at(m_currentIndex).queueItemId);
                }
                playIndex(index);
                return;
            }
        }
        invalidatePrefetch();
    }
    if (m_shuffle && m_queue.size() > 1)
    {
        QList<int> candidates;
        for (int i = 0; i < m_queue.size(); ++i)
            if (i != m_currentIndex && !m_shuffleRound.contains(m_queue.at(i).queueItemId))
                candidates.append(i);
        if (candidates.isEmpty())
        {
            if (m_source.value("hasMore").toBool())
            {
                loadCollectionPage(true);
                return;
            }
            if (m_repeatMode != 1)
            {
                ++m_generation;
                clearCurrentSource();
                emit snapshotChanged();
                return;
            }
            m_shuffleRound.clear();
            if (m_currentIndex >= 0)
                m_shuffleRound.append(m_queue.at(m_currentIndex).queueItemId);
            for (int i = 0; i < m_queue.size(); ++i)
                if (i != m_currentIndex)
                    candidates.append(i);
        }
        playIndex(candidates.at(QRandomGenerator::global()->bounded(candidates.size())));
    }
    else if (m_currentIndex + 1 < m_queue.size())
        playIndex(m_currentIndex + 1);
    else if (m_source.value("hasMore").toBool())
        loadCollectionPage(true);
    else if (m_repeatMode == 1)
        playIndex(0);
    else
    {
        ++m_generation;
        clearCurrentSource();
        emit snapshotChanged();
    }
}
void PlaybackController::previous()
{
    if (m_player.position() > 3000)
    {
        m_player.setPosition(0);
        return;
    }
    if (m_shuffle && m_playedItemIds.size() > 1)
    {
        m_playedItemIds.removeLast();
        while (!m_playedItemIds.isEmpty())
        {
            const quint64 itemId = m_playedItemIds.takeLast();
            const auto item =
                std::find_if(m_queue.cbegin(), m_queue.cend(),
                             [itemId](const QueueItem &row) { return row.queueItemId == itemId; });
            if (item != m_queue.cend())
            {
                playIndex(static_cast<int>(std::distance(m_queue.cbegin(), item)));
                return;
            }
        }
        return;
    }
    if (m_currentIndex > 0)
        playIndex(m_currentIndex - 1);
    else
        m_player.setPosition(0);
}
void PlaybackController::playQueueIndex(int index)
{
    if (index != m_currentIndex)
        playIndex(index);
}
void PlaybackController::clearQueue()
{
    rememberUndo();
    cancelCollection();
    ++m_generation;
    clearCurrentSource();
    invalidatePrefetch();
    m_queue.clear();
    m_pendingSeek = -1;
    m_playedItemIds.clear();
    m_shuffleRound.clear();
    m_currentIndex = -1;
    m_track = {};
    m_lyrics.clear();
    m_lyricLines.setLyrics({});
    emit lyricsChanged();
    setError({});
    emit queueChanged();
    emit snapshotChanged();
}
void PlaybackController::clearHistory()
{
    if (m_restoring)
        m_historyChangedDuringRestore = true;
    m_history.clear();
    emit historyChanged();
}
void PlaybackController::removeQueueIndex(int index)
{
    if (index < 0 || index >= m_queue.size())
        return;
    rememberUndo();
    cancelCollection();
    invalidatePrefetch();
    const bool removingCurrent = index == m_currentIndex;
    const bool resume = m_desiredPlaying;
    m_playedItemIds.removeAll(m_queue.at(index).queueItemId);
    m_shuffleRound.removeAll(m_queue.at(index).queueItemId);
    m_queue.removeAt(index);
    if (removingCurrent)
    {
        ++m_generation;
        clearCurrentSource();
        m_currentIndex = -1;
        m_track = {};
        m_lyrics.clear();
        m_lyricLines.setLyrics({});
        emit lyricsChanged();
        emit snapshotChanged();
    }
    else if (index < m_currentIndex)
    {
        --m_currentIndex;
    }
    emit queueChanged();
    if (removingCurrent && !m_queue.isEmpty())
    {
        const int successor = index < m_queue.size() ? index : (m_repeatMode == 1 ? 0 : -1);
        if (successor >= 0)
        {
            playIndex(successor);
            if (!resume)
            {
                m_desiredPlaying = false;
                m_player.pause();
                emit snapshotChanged();
            }
        }
    }
    else
        emit snapshotChanged();
}
void PlaybackController::seek(qint64 positionMs)
{
    if (seekable())
        m_player.setPosition(qBound<qint64>(0, positionMs, duration()));
}
void PlaybackController::setArtworkVisible(const QVariantMap &data, bool visible)
{
    const auto track = Track::fromMap(data);
    if (visible)
    {
        m_catalog->remember(track);
        m_catalog->ensureMetadata(track.key);
    }
    else
        m_catalog->cancelQueuedMetadata(track.key);
}

void PlaybackController::playIndex(int index)
{
    if (index < 0 || index >= m_queue.size())
        return;
    const auto item = m_queue.at(index);
    const QUrl prefetchedMedia = item.queueItemId == m_prefetchedQueueItemId &&
                                        m_prefetchedQuality == m_requestedQuality
                                    ? m_prefetchedMedia
                                    : QUrl{};
    invalidatePrefetch();
    if (m_track.key != item.track.key)
        m_pendingSeek = -1;
    ++m_generation;
    clearCurrentSource();
    m_recoveryAttempts = 0;
    m_lastGoodPosition = qMax<qint64>(0, m_pendingSeek);
    m_desiredPlaying = true;
    m_resolving = true;
    m_currentIndex = index;
    // Navigation history follows item identities because removing rows changes their positions.
    if (m_shuffle && !m_shuffleRound.contains(item.queueItemId))
        m_shuffleRound.append(item.queueItemId);
    if (m_playedItemIds.isEmpty() || m_playedItemIds.last() != item.queueItemId)
        m_playedItemIds.push_back(item.queueItemId);
    if (m_playedItemIds.size() > 1000)
        m_playedItemIds.removeFirst();
    m_track = item.track;
    m_catalog->remember(m_track);
    m_catalog->ensureMetadata(m_track.key, true);
    m_lyricLines.setLyrics({});
    m_lyrics = QStringLiteral("歌词加载中…");
    emit lyricsChanged();

    requestLyrics(item.track, m_generation);
    setError({});
    if (prefetchedMedia.isEmpty())
        resolveCurrentMedia();
    else
    {
        m_waitingForSeek = m_pendingSeek > 0;
        m_player.setSource(prefetchedMedia);
    }
    emit snapshotChanged();
    if (m_currentIndex + 3 >= m_queue.size())
        loadCollectionPage();
}

void PlaybackController::recordHistory(const QueueItem &item)
{
    if (m_restoring)
        m_historyChangedDuringRestore = true;
    m_history.erase(std::remove_if(m_history.begin(), m_history.end(), [&item](const QueueItem &row)
                                   { return row.track.key == item.track.key; }),
                    m_history.end());
    m_history.prepend(item);
    while (m_history.size() > 100)
        m_history.removeLast();
    emit historyChanged();
}

QByteArray PlaybackController::serializedHistory() const
{
    QJsonArray rows;
    for (const auto &row : m_history)
    {
        rows.append(QJsonObject::fromVariantMap(row.track.toMap()));
    }
    return QJsonDocument(rows).toJson(QJsonDocument::Compact);
}

void PlaybackController::requestLyrics(const Track &track, quint64 generation)
{
    if (track.hash.isEmpty())
        return;
    m_api->lyrics(track.hash,
                  [this, guard = QPointer<PlaybackController>(this), key = track.key,
                   generation](QString text, QString error)
                  {
                      if (!guard || generation != m_generation || key != m_track.key)
                          return;
                      m_lyricLines.setLyrics(text);
                      m_lyricLines.setPosition(position());
                      m_lyrics = m_lyricLines.rowCount() > 0
                                     ? QString{}
                                     : (text.isEmpty()
                                            ? (error.isEmpty() ? QStringLiteral("暂无歌词") : error)
                                            : QStringLiteral("暂无同步歌词"));
                      emit lyricsChanged();
                  });
}
void PlaybackController::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorChanged();
}

void PlaybackController::clearCurrentSource()
{
    m_recoveryTimer.stop();
    m_resumeDeadline.stop();
    m_recovering = false;
    m_waitingForSeek = false;
    m_desiredPlaying = false;
    m_resolving = false;
    m_player.stop();
    m_player.setSource({});
}

void PlaybackController::invalidateOnlineSession()
{
    saveSnapshot();
    m_restoring = true;
    m_queueChangedDuringRestore = false;
    clearQueue();
    invalidateUndo();
    m_history.clear();
    emit historyChanged();
    restoreScope();
}

QString PlaybackController::playbackStatus() const
{
    if (m_recovering)
        return m_desiredPlaying ? QStringLiteral("连接中断，正在恢复播放…")
                                : QStringLiteral("恢复准备中 · 已暂停");
    if (m_resolving)
        return m_desiredPlaying ? QStringLiteral("正在准备播放…")
                                : QStringLiteral("准备中 · 已暂停");
    if (!m_errorMessage.isEmpty())
        return QStringLiteral("播放失败");
    if (m_player.mediaStatus() == QMediaPlayer::StalledMedia)
        return QStringLiteral("正在缓冲…");
    if (playing())
        return QStringLiteral("正在播放");
    return hasCurrentTrack() ? QStringLiteral("已暂停") : QString{};
}
QVariantList PlaybackController::recentTracks() const
{
    QVariantList tracks;
    for (const auto &item : m_history)
        tracks.append(item.track.toMap());
    return tracks;
}
void PlaybackController::enqueueSong(const QVariantMap &data, bool next)
{
    Track track = Track::fromMap(data);
    if (track.key.isEmpty() || track.hash.isEmpty())
        return;
    track = m_catalog->remember(track);
    invalidateUndo();
    cancelCollection();
    const int insertion = next ? m_currentIndex + 1 : m_queue.size();
    invalidatePrefetch();
    m_queue.insert(insertion, {track, ++m_nextQueueItemId});
    m_notice = next ? QStringLiteral("已加入下一首") : QStringLiteral("已加入队列末尾");
    emit noticeChanged();
    emit queueChanged();
    if (m_currentIndex < 0)
    {
        m_currentIndex = 0;
        m_track = m_queue.first().track;
        emit snapshotChanged();
    }
}
void PlaybackController::moveQueueItem(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_queue.size() || to >= m_queue.size() || from == to)
        return;
    invalidateUndo();
    cancelCollection();
    invalidatePrefetch();
    const auto currentId = m_currentIndex >= 0 ? m_queue.at(m_currentIndex).queueItemId : 0;
    m_queue.move(from, to);
    for (int i = 0; i < m_queue.size(); ++i)
        if (m_queue.at(i).queueItemId == currentId)
            m_currentIndex = i;
    emit queueChanged();
    emit snapshotChanged();
}
void PlaybackController::rememberUndo()
{
    m_undoQueue = m_queue;
    m_undoSource = m_source;
    m_undoIndex = m_currentIndex;
    m_undoPosition = position();
    m_undoTimer.start();
    emit undoChanged();
}
void PlaybackController::invalidateUndo()
{
    m_undoTimer.stop();
    m_undoQueue.clear();
    emit undoChanged();
}
void PlaybackController::undoQueueEdit()
{
    if (!undoAvailable())
        return;
    ++m_generation;
    clearCurrentSource();
    cancelCollection();
    invalidatePrefetch();
    m_queue = m_undoQueue;
    m_source = m_undoSource;
    m_currentIndex = m_undoIndex;
    m_track = m_currentIndex >= 0 && m_currentIndex < m_queue.size()
                  ? m_queue.at(m_currentIndex).track
                  : Track{};
    m_pendingSeek = m_undoPosition;
    m_lyricLines.setLyrics({});
    m_lyrics.clear();
    m_playedItemIds.clear();
    m_shuffleRound.clear();
    invalidateUndo();
    emit lyricsChanged();
    emit queueChanged();
    emit snapshotChanged();
    emit positionChanged();
}

void PlaybackController::restoreScope()
{
    m_scope = m_api->scopeKey();
    m_restoring = true;
    m_historyChangedDuringRestore = false;
    m_queueChangedDuringRestore = false;
    m_storageWritable = true;
    m_restoreGeneration = m_generation;
    m_store->load(m_scope);
}
QByteArray PlaybackController::serializedSnapshot() const
{
    QJsonArray shuffleRound, playedItems;
    for (auto id : m_shuffleRound)
        shuffleRound.append(QString::number(id));
    for (auto id : m_playedItemIds)
        playedItems.append(QString::number(id));
    QJsonArray queue;
    for (const auto &item : m_queue)
    {
        auto map = item.track.toMap();
        map.insert("queueItemId", QString::number(item.queueItemId));
        queue.append(QJsonObject::fromVariantMap(map));
    }
    return QJsonDocument(
               QJsonObject{{"queue", queue},
                           {"shuffle", m_shuffle},
                           {"repeatMode", m_repeatMode},
                           {"shuffleRound", shuffleRound},
                           {"playedItems", playedItems},
                           {"index", m_currentIndex},
                           {"position", position()},
                           {"source", QJsonObject::fromVariantMap(m_source)},
                           {"history", QJsonDocument::fromJson(serializedHistory()).array()}})
        .toJson(QJsonDocument::Compact);
}
void PlaybackController::saveSnapshot()
{
    if (!m_restoring && m_storageWritable)
        m_store->save(m_scope, serializedSnapshot());
}

void PlaybackController::cancelCollection()
{
    ++m_sourceGeneration;
    m_source.clear();
    m_sourceLoading = false;
    m_advanceSource = false;
}
void PlaybackController::playCollection(const QVariantList &tracks, int index,
                                        const QVariantMap &source)
{
    if (tracks.isEmpty() || index < 0 || index >= tracks.size())
        return;
    if (Track::fromMap(tracks.at(index).toMap()).key == m_track.key)
    {
        emit detailRequested();
        return;
    }
    rememberUndo();
    cancelCollection();
    invalidatePrefetch();
    m_queue.clear();
    for (const auto &row : tracks)
    {
        auto track = Track::fromMap(row.toMap());
        track = m_catalog->remember(track);
        m_queue.append({track, ++m_nextQueueItemId});
    }
    m_source = source;
    m_playedItemIds.clear();
    m_shuffleRound.clear();
    m_notice = QStringLiteral("正在播放：") + source.value("title").toString();
    emit noticeChanged();
    emit queueChanged();
    playIndex(index);
}
void PlaybackController::loadCollectionPage(bool advance)
{
    if (!m_source.value("hasMore").toBool())
        return;
    if (advance)
    {
        m_advanceSource = true;
        ++m_generation;
        clearCurrentSource();
        m_desiredPlaying = true;
        m_resolving = true;
        emit snapshotChanged();
    }
    if (m_sourceLoading)
        return;
    if (m_queue.size() >= 1000)
    {
        m_source["hasMore"] = false;
        setError(QStringLiteral("队列已达到 1000 首，请从列表选择后续歌曲继续"));
        m_desiredPlaying = false;
        m_resolving = false;
        emit snapshotChanged();
        return;
    }
    m_sourceLoading = true;
    const auto generation = m_sourceGeneration;
    auto callback = [this, guard = QPointer<PlaybackController>(this),
                     generation](QList<Track> tracks, QString code, QString message)
    {
        if (!guard || generation != m_sourceGeneration)
            return;
        m_sourceLoading = false;
        if (!code.isEmpty())
        {
            if (m_advanceSource)
            {
                m_resolving = false;
                m_desiredPlaying = false;
                setError(message);
                emit snapshotChanged();
            }
            return;
        }
        m_source["hasMore"] = tracks.size() == 30;
        m_source["nextPage"] = m_source.value("nextPage").toInt() + 1;
        const int nextIndex = m_queue.size();
        invalidatePrefetch();
        for (auto track : tracks)
        {
            if (m_queue.size() >= 1000)
            {
                m_source["hasMore"] = false;
                m_notice = QStringLiteral("队列已达到 1000 首上限，可从专辑或歌单选择后续歌曲");
                emit noticeChanged();
                break;
            }
            track = m_catalog->remember(track);
            m_queue.append({track, ++m_nextQueueItemId});
        }
        emit queueChanged();
        if (m_advanceSource)
        {
            m_advanceSource = false;
            const bool resume = m_desiredPlaying;
            if (nextIndex < m_queue.size())
            {
                playIndex(nextIndex);
                if (!resume)
                {
                    m_desiredPlaying = false;
                    m_player.pause();
                    emit snapshotChanged();
                }
            }
            else
            {
                m_desiredPlaying = false;
                m_resolving = false;
                emit snapshotChanged();
            }
        }
    };
    const auto kind = m_source.value("kind").toString();
    const auto id = m_source.value("id").toString();
    const int page = m_source.value("nextPage", 2).toInt();
    if (kind == QStringLiteral("album"))
        m_api->albumTracks(id, callback, page);
    else if (kind == QStringLiteral("artist"))
        m_api->artistTracks(id, callback, page);
    else
        m_api->playlistTracks(m_source.value("globalId").toString(), id, callback, page);
}

void PlaybackController::resolveCurrentMedia()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_queue.size())
        return;
    const auto track = m_queue.at(m_currentIndex).track;
    const auto generation = m_generation;
    m_api->resolveSong(
        track.hash, track.albumAudioId,
        [this, guard = QPointer<PlaybackController>(this), generation,
         key = track.key](QUrl url, QString error)
        {
            if (!guard || generation != m_generation || key != m_track.key)
                return;
            if (!error.isEmpty() || url.isEmpty())
            {
                clearCurrentSource();
                setError(error.isEmpty() ? QStringLiteral("暂时无法获取播放地址，请重试") : error);
                emit snapshotChanged();
                return;
            }
            m_waitingForSeek = m_pendingSeek > 0;
            m_player.setSource(url);
            emit snapshotChanged();
        },
        m_requestedQuality);
}

int PlaybackController::nextPrefetchIndex(bool *startsNewShuffleRound) const
{
    *startsNewShuffleRound = false;
    if (m_currentIndex < 0 || m_currentIndex >= m_queue.size() || m_repeatMode == 2)
        return -1;
    if (!m_shuffle)
    {
        if (m_currentIndex + 1 < m_queue.size())
            return m_currentIndex + 1;
        return m_repeatMode == 1 && !m_queue.isEmpty() ? 0 : -1;
    }
    if (m_queue.size() < 2)
        return -1;
    QList<int> candidates;
    for (int i = 0; i < m_queue.size(); ++i)
        if (i != m_currentIndex && !m_shuffleRound.contains(m_queue.at(i).queueItemId))
            candidates.append(i);
    if (candidates.isEmpty() && m_repeatMode == 1 &&
        !m_source.value("hasMore").toBool())
    {
        *startsNewShuffleRound = true;
        for (int i = 0; i < m_queue.size(); ++i)
            if (i != m_currentIndex)
                candidates.append(i);
    }
    return candidates.isEmpty()
               ? -1
               : candidates.at(QRandomGenerator::global()->bounded(candidates.size()));
}

void PlaybackController::maybePrefetchNextMedia()
{
    if (m_prefetchedQueueItemId != 0 || !m_desiredPlaying || m_resolving || m_recovering ||
        m_player.playbackState() != QMediaPlayer::PlayingState || duration() <= 0 ||
        duration() - position() > 45000)
        return;
    bool startsNewShuffleRound = false;
    const int index = nextPrefetchIndex(&startsNewShuffleRound);
    if (index < 0)
        return;
    const auto item = m_queue.at(index);
    m_prefetchedQueueItemId = item.queueItemId;
    m_prefetchedQuality = m_requestedQuality;
    m_prefetchedStartsShuffleRound = startsNewShuffleRound;
    const auto generation = ++m_prefetchGeneration;
    m_api->resolveSong(
        item.track.hash, item.track.albumAudioId,
        [this, guard = QPointer<PlaybackController>(this), generation,
         itemId = item.queueItemId](QUrl url, QString error)
        {
            if (!guard || generation != m_prefetchGeneration ||
                itemId != m_prefetchedQueueItemId)
                return;
            if (error.isEmpty() && !url.isEmpty())
            {
                m_prefetchedMedia = url;
                m_prefetchExpiry.start(90000);
            }
        },
        m_requestedQuality);
}

void PlaybackController::invalidatePrefetch()
{
    ++m_prefetchGeneration;
    m_prefetchExpiry.stop();
    m_prefetchedQueueItemId = 0;
    m_prefetchedQuality.clear();
    m_prefetchedMedia = QUrl{};
    m_prefetchedStartsShuffleRound = false;
}

void PlaybackController::prepareSourcePlayback()
{
    if (m_waitingForSeek)
    {
        m_resumeDeadline.start();
        applyResumePosition();
    }
    else
    {
        m_pendingSeek = -1;
        m_resolving = false;
        m_recovering = false;
        if (m_desiredPlaying)
            m_player.play();
    }
    emit snapshotChanged();
}

void PlaybackController::applyResumePosition()
{
    const auto status = m_player.mediaStatus();
    if (!m_waitingForSeek || !m_player.isSeekable() ||
        (status != QMediaPlayer::LoadedMedia && status != QMediaPlayer::BufferedMedia))
        return;
    const auto target = m_pendingSeek;
    m_waitingForSeek = false;
    m_resumeDeadline.stop();
    m_player.setPosition(target);
    m_pendingSeek = -1;
    m_lastGoodPosition = target;
    m_resolving = false;
    m_recovering = false;
    if (m_desiredPlaying)
        m_player.play();
    emit positionChanged();
    emit snapshotChanged();
}

void PlaybackController::handlePlaybackError(QMediaPlayer::Error error, const QString &message)
{
    if (error == QMediaPlayer::NoError || m_currentIndex < 0 || m_recoveryTimer.isActive())
        return;
    const bool wantedPlayback = m_desiredPlaying;
    m_pendingSeek = qMax(position(), m_lastGoodPosition);
    const bool recoverable = error == QMediaPlayer::NetworkError ||
                             error == QMediaPlayer::ResourceError ||
                             (error == QMediaPlayer::FormatError && m_pendingSeek > 0);
    ++m_generation;
    clearCurrentSource();
    if (wantedPlayback && recoverable && m_recoveryAttempts < 2)
    {
        ++m_recoveryAttempts;
        m_desiredPlaying = true;
        m_resolving = true;
        m_recovering = true;
        setError({});
        if (m_lyricLines.rowCount() == 0)
            requestLyrics(m_track, m_generation);
        m_recoveryTimer.start(m_recoveryAttempts == 1 ? 500 : 1500);
    }
    else
        setError(message.isEmpty() ? QStringLiteral("音频暂时无法播放，请重试") : message);
    emit positionChanged();
    emit snapshotChanged();
}

void PlaybackController::pauseForInterruption(bool resumable)
{
    if (!m_desiredPlaying)
    {
        if (!resumable)
            m_resumeAfterInterruption = false;
        return;
    }
    m_resumeAfterInterruption = resumable;
    m_desiredPlaying = false;
    m_player.pause();
    emit snapshotChanged();
}

void PlaybackController::resumeAfterInterruption(bool shouldResume)
{
    const bool resume = m_resumeAfterInterruption && shouldResume;
    m_resumeAfterInterruption = false;
    if (!resume || m_desiredPlaying || !hasCurrentTrack())
        return;
    m_desiredPlaying = true;
    emit snapshotChanged();
    if (!m_player.source().isEmpty() && !m_waitingForSeek)
        m_player.play();
}

void PlaybackController::pauseForOutputLoss()
{
    m_resumeAfterInterruption = false;
    if (!m_desiredPlaying)
        return;
    m_desiredPlaying = false;
    m_player.pause();
    m_notice = QStringLiteral("音频输出已断开，播放已暂停");
    emit noticeChanged();
    emit snapshotChanged();
}

void PlaybackController::handleOutputDeviceChange(bool disconnected)
{
    if (disconnected)
        pauseForOutputLoss();
}
