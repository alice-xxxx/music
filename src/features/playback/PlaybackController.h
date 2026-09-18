#pragma once
#include "AudioEngine.h"
#include <QObject>
#include <QVariantList>
#include <QTimer>
#include "LyricsModel.h"
#include "QueueModel.h"
#include "domain/Track.h"
class QAudioOutput;
class KuGouApi;
class CatalogService;
class LocalStore;
class PlaybackController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageErrorChanged)
    Q_PROPERTY(bool undoAvailable READ undoAvailable NOTIFY undoChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString playbackStatus READ playbackStatus NOTIFY snapshotChanged)
    Q_PROPERTY(int audioBitRate READ audioBitRate NOTIFY audioMetadataChanged)
    Q_PROPERTY(QVariantList recentTracks READ recentTracks NOTIFY historyChanged)
    Q_PROPERTY(QVariantMap currentTrack READ currentTrack NOTIFY snapshotChanged)
    Q_PROPERTY(LyricsModel *lyricLines READ lyricLines CONSTANT)
    Q_PROPERTY(QString trackKey READ trackKey NOTIFY snapshotChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY snapshotChanged)
    Q_PROPERTY(QueueModel *queueModel READ queueModel CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY snapshotChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY snapshotChanged)
    Q_PROPERTY(bool hasCurrentTrack READ hasCurrentTrack NOTIFY snapshotChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY snapshotChanged)
    Q_PROPERTY(bool desiredPlaying READ desiredPlaying NOTIFY snapshotChanged)
    Q_PROPERTY(bool preparing READ preparing NOTIFY snapshotChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
    Q_PROPERTY(int queueCount READ queueCount NOTIFY queueChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
  public:
    QueueModel *queueModel()
    {
        return &m_queueModel;
    }
    ~PlaybackController() override;
    explicit PlaybackController(KuGouApi *api, CatalogService *catalog, LocalStore *store,
                                QObject *parent = nullptr);
    QString storageError() const
    {
        return m_storageError;
    }
    bool undoAvailable() const
    {
        return m_undoTimer.isActive();
    }
    QString notice() const
    {
        return m_notice;
    }
    QString playbackStatus() const;
    int audioBitRate() const
    {
        return m_player.audioBitRate();
    }
    void setRequestedQuality(const QString &quality);
    QVariantList recentTracks() const;
    Q_INVOKABLE void playCollection(const QVariantList &tracks, int index,
                                    const QVariantMap &source);
    Q_INVOKABLE void enqueueSong(const QVariantMap &track, bool next);
    Q_INVOKABLE void moveQueueItem(int from, int to);
    Q_INVOKABLE void undoQueueEdit();
    QVariantMap currentTrack() const
    {
        return m_track.toMap();
    }
    Q_INVOKABLE void playSong(const QVariantMap &track);
    Q_INVOKABLE void setArtworkVisible(const QVariantMap &track, bool visible);
    LyricsModel *lyricLines()
    {
        return &m_lyricLines;
    }
    QString trackKey() const
    {
        return m_track.key;
    }
    int currentIndex() const
    {
        return m_currentIndex;
    }
    QString title() const;
    QString artist() const;
    QString coverUrl() const
    {
        return m_track.coverUrl;
    }
    bool hasCurrentTrack() const;
    bool playing() const;
    bool desiredPlaying() const
    {
        return m_desiredPlaying;
    }
    bool preparing() const
    {
        return m_resolving;
    }
    qint64 position() const;
    qint64 duration() const;
    bool seekable() const;
    int queueCount() const;
    QString errorMessage() const;
    int volume() const;
    void setVolume(int percent);
    QString lyrics() const;
    int repeatMode() const;
    void setRepeatMode(int mode);
    bool shuffle() const;
    void setShuffle(bool enabled);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void playQueueIndex(int index);
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void removeQueueIndex(int index);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void seek(qint64 positionMs);
    void pauseForInterruption(bool resumable);
    void resumeAfterInterruption(bool shouldResume);
    void pauseForOutputLoss();
  signals:
    void audioMetadataChanged();
    void storageErrorChanged();
    void undoChanged();
    void noticeChanged();
    void detailRequested();
    void snapshotChanged();
    void positionChanged();
    void durationChanged();
    void seekableChanged();
    void queueChanged();
    void errorChanged();
    void volumeChanged();
    void lyricsChanged();
    void historyChanged();
    void repeatModeChanged();
    void shuffleChanged();

  private:
    QVariantList queueItems() const;
    void resolveCurrentMedia();
    void maybePrefetchNextMedia();
    int nextPrefetchIndex(bool *startsNewShuffleRound) const;
    void invalidatePrefetch();
    void prepareSourcePlayback();
    void applyResumePosition();
    void handlePlaybackError(QMediaPlayer::Error error, const QString &message);
    void handleOutputDeviceChange(bool disconnected);
    QTimer m_recoveryTimer;
    QTimer m_resumeDeadline;
    int m_recoveryAttempts = 0;
    bool m_recovering = false;
    bool m_waitingForSeek = false;
    qint64 m_lastGoodPosition = 0;
    QString m_requestedQuality = QStringLiteral("128");
    struct QueueItem
    {
        Track track;
        quint64 queueItemId = 0;
    };
    void restoreScope();
    void saveSnapshot();
    QByteArray serializedSnapshot() const;
    LocalStore *m_store;
    QString m_scope;
    QString m_storageError;
    QTimer m_saveTimer;
    QTimer m_positionSaveTimer;
    bool m_restoring = false;
    bool m_queueChangedDuringRestore = false;
    bool m_historyChangedDuringRestore = false;
    bool m_storageWritable = true;
    quint64 m_restoreGeneration = 0;
    void loadCollectionPage(bool advance = false);
    void cancelCollection();
    QVariantMap m_source;
    QVariantMap m_undoSource;
    quint64 m_sourceGeneration = 0;
    bool m_sourceLoading = false;
    bool m_advanceSource = false;
    void rememberUndo();
    void invalidateUndo();
    void playIndex(int index);
    void clearCurrentSource();
    void invalidateOnlineSession();
    void setError(const QString &message);
    void recordHistory(const QueueItem &item);
    QByteArray serializedHistory() const;
    void requestLyrics(const Track &track, quint64 generation);
    QList<QueueItem> m_undoQueue;
    int m_undoIndex = -1;
    qint64 m_undoPosition = 0;
    QTimer m_undoTimer;
    QString m_notice;
    qint64 m_pendingSeek = -1;
    bool m_resolving = false;
    QAudioOutput *m_audioOutput;
    AudioEngine m_player;
    QList<QueueItem> m_queue;
    int m_currentIndex = -1;
    Track m_track;
    CatalogService *m_catalog;
    QString m_errorMessage;
    KuGouApi *m_api;
    quint64 m_generation = 0;
    quint64 m_prefetchGeneration = 0;
    quint64 m_prefetchedQueueItemId = 0;
    QString m_prefetchedQuality;
    QUrl m_prefetchedMedia;
    bool m_prefetchedStartsShuffleRound = false;
    QTimer m_prefetchExpiry;
    QList<QueueItem> m_history;
    QString m_lyrics;
    LyricsModel m_lyricLines;
    int m_repeatMode = 0;
    bool m_shuffle = false;
    bool m_desiredPlaying = false;
    bool m_resumeAfterInterruption = false;
    QList<quint64> m_playedItemIds;
    QList<quint64> m_shuffleRound;
    QueueModel m_queueModel;
    quint64 m_nextQueueItemId = 0;
};
