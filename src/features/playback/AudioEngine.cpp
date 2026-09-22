#include "AudioEngine.h"
#include "MediaStreamRelay.h"
#include <QAudioOutput>
#include <QAudioDevice>
#include <QMediaMetaData>
#include <algorithm>

AudioEngine::AudioEngine(QObject *parent) : QObject(parent)
{
    setSource({});
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this,
            [this]
            {
                if (!m_output)
                    return;
                const QByteArray previousId = m_output->device().id();
                const auto nextDevice = QMediaDevices::defaultAudioOutput();
                if (previousId == nextDevice.id())
                    return;
                const auto outputs = QMediaDevices::audioOutputs();
                const bool disconnected =
                    !previousId.isEmpty() &&
                    std::none_of(outputs.cbegin(), outputs.cend(),
                                 [&previousId](const QAudioDevice &device)
                                 { return device.id() == previousId; });
                m_output->setDevice(nextDevice);
                emit outputDeviceChanged(disconnected);
            });
}
void AudioEngine::setAudioOutput(QAudioOutput *output)
{
    m_output = output;
    m_player->setAudioOutput(output);
}
void AudioEngine::setSource(const QUrl &url)
{
    m_source = url;
    if (m_relay)
    {
        disconnect(m_relay, nullptr, this, nullptr);
        m_relay->deleteLater();
        m_relay = nullptr;
    }
    if (m_player)
    {
        disconnect(m_player, nullptr, this, nullptr);
        m_player->stop();
        m_player->setAudioOutput(nullptr);
        m_player->deleteLater();
    }
    auto *player = new QMediaPlayer(this);
    m_player = player;
    player->setAudioOutput(m_output);
    connect(player, &QMediaPlayer::metaDataChanged, this,
            [this, player]
            {
                if (player == m_player)
                    emit audioMetadataChanged();
            });
    connect(player, &QMediaPlayer::positionChanged, this,
            [this, player](qint64 value)
            {
                if (player == m_player)
                    emit positionChanged(value);
            });
    connect(player, &QMediaPlayer::durationChanged, this,
            [this, player](qint64 value)
            {
                if (player == m_player)
                    emit durationChanged(value);
            });
    connect(player, &QMediaPlayer::seekableChanged, this,
            [this, player](bool value)
            {
                if (player == m_player)
                    emit seekableChanged(value);
            });
    connect(player, &QMediaPlayer::playbackStateChanged, this,
            [this, player](auto value)
            {
                if (player == m_player)
                    emit playbackStateChanged(value);
            });
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this, player](auto value)
            {
                if (player == m_player)
                    emit mediaStatusChanged(value);
            });
    connect(player, &QMediaPlayer::errorOccurred, this,
            [this, player](auto error, const QString &)
            {
                // Backend messages may contain signed URLs. Keep the public error independent of
                // those strings.
                if (player == m_player)
                    emit errorOccurred(error,
                                       QStringLiteral("音频暂时无法播放，请重试或切换下一首"));
            });
    if (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
    {
        auto *relay = new MediaStreamRelay(this);
        m_relay = relay;
        connect(relay, &MediaStreamRelay::ready, this,
                [this, player, relay](const QUrl &playable)
                {
                    if (m_player != player || m_relay != relay)
                        return;
                    player->setSource(playable);
                    emit sourceReady();
                });
        connect(relay, &MediaStreamRelay::failed, this,
                [this, player, relay]
                {
                    if (m_player == player && m_relay == relay)
                        emit errorOccurred(QMediaPlayer::ResourceError,
                                           QStringLiteral("媒体连接暂时不可用，请重试"));
                });
        relay->open(url);
    }
    else
    {
        player->setSource(url);
        if (!url.isEmpty())
            emit sourceReady();
    }
    emit positionChanged(0);
    emit audioMetadataChanged();
    emit durationChanged(0);
    emit seekableChanged(false);
    emit playbackStateChanged(player->playbackState());
}
QUrl AudioEngine::source() const
{
    return m_source;
}
qint64 AudioEngine::position() const
{
    return m_player->position();
}
qint64 AudioEngine::duration() const
{
    return m_player->duration();
}
bool AudioEngine::isSeekable() const
{
    return m_player->isSeekable();
}
int AudioEngine::audioBitRate() const
{
    return qMax(0, m_player->metaData().value(QMediaMetaData::AudioBitRate).toInt());
}
QMediaPlayer::PlaybackState AudioEngine::playbackState() const
{
    return m_player->playbackState();
}
QMediaPlayer::MediaStatus AudioEngine::mediaStatus() const
{
    return m_player->mediaStatus();
}
void AudioEngine::play()
{
    emit playbackStarting();
    m_player->play();
}
void AudioEngine::pause()
{
    m_player->pause();
}
void AudioEngine::stop()
{
    m_player->stop();
}
void AudioEngine::setPosition(qint64 position)
{
    m_player->setPosition(position);
}
