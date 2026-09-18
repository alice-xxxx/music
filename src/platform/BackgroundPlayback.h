#pragma once
#include <QObject>
#include <QString>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#endif

class BackgroundPlayback final : public QObject
#ifdef Q_OS_WIN
                               , public QAbstractNativeEventFilter
#endif
{
    Q_OBJECT
  public:
    explicit BackgroundPlayback(QObject *parent = nullptr);
    ~BackgroundPlayback();
    void update(bool hasTrack, bool desiredPlaying, bool playing, qint64 position,
                qint64 duration, const QString &title, const QString &artist);

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
    bool m_active = false;
    void *m_platformState = nullptr;
};
