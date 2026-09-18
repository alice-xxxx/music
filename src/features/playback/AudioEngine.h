#pragma once
#include <QMediaPlayer>
#include <QMediaDevices>
class MediaStreamRelay;

class AudioEngine final : public QObject
{
    Q_OBJECT
  public:
    explicit AudioEngine(QObject *parent = nullptr);
    void setAudioOutput(QAudioOutput *output);
    void setSource(const QUrl &url);
    QUrl source() const;
    qint64 position() const;
    qint64 duration() const;
    bool isSeekable() const;
    int audioBitRate() const;
    QMediaPlayer::PlaybackState playbackState() const;
    QMediaPlayer::MediaStatus mediaStatus() const;
    void play();
    void pause();
    void stop();
    void setPosition(qint64 position);
  signals:
    void sourceReady();
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void seekableChanged(bool seekable);
    void audioMetadataChanged();
    void outputDeviceChanged(bool disconnected);
    void playbackStateChanged(QMediaPlayer::PlaybackState state);
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);
    void errorOccurred(QMediaPlayer::Error error, const QString &message);

  private:
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_output = nullptr;
    QMediaDevices m_devices;
    MediaStreamRelay *m_relay = nullptr;
    QUrl m_source;
};
