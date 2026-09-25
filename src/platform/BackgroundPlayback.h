#pragma once
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#endif

class QNetworkAccessManager;
class QNetworkReply;

enum class MediaSessionState : int
{
    None = 0,
    Paused = 1,
    Buffering = 2,
    Playing = 3,
    Error = 4
};

class BackgroundPlayback final : public QObject
#ifdef Q_OS_WIN
                               , public QAbstractNativeEventFilter
#endif
{
    Q_OBJECT
  public:
    explicit BackgroundPlayback(QObject *parent = nullptr);
    ~BackgroundPlayback();
    void beginPlaybackTransition();
    void preparePlayback();
    void update(bool hasTrack, bool desiredPlaying, bool playing, bool buffering,
                bool failed, bool seekable, bool canSkipNext, bool canSkipPrevious,
                qint64 position, qint64 duration,
                const QString &title, const QString &artist,
                const QString &artworkUrl);

    void dispatchPlay();
    void dispatchPause();
    void dispatchToggle();
    void dispatchNext();
    void dispatchPrevious();
    void dispatchSeek(qint64 position);
    void dispatchInterruptionBegan(bool resumable);
    void dispatchInterruptionEnded(bool shouldResume);
    void dispatchOutputDisconnected();
#ifdef Q_OS_WIN
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

  signals:
    void playRequested();
    void pauseRequested();
    void toggleRequested();
    void nextRequested();
    void previousRequested();
    void seekRequested(qint64 position);
    void interruptionBegan(bool resumable);
    void interruptionEnded(bool shouldResume);
    void outputDisconnected();

  private:
    void updateArtwork(const QString &artworkUrl);
    void publishArtwork();

    void *m_platformState = nullptr;
    QNetworkAccessManager *m_artworkNetwork = nullptr;
    QPointer<QNetworkReply> m_artworkReply;
    QString m_artworkUrl;
    QByteArray m_artworkData;
};
