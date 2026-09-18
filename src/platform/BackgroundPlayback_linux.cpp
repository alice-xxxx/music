#include "BackgroundPlayback.h"
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QVariantMap>

namespace
{
class LinuxMediaState;

class RootAdaptor final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool Fullscreen READ fullscreen CONSTANT)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)

  public:
    explicit RootAdaptor(LinuxMediaState *state);
    bool canQuit() const { return false; }
    bool fullscreen() const { return false; }
    bool canSetFullscreen() const { return false; }
    bool canRaise() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Music"); }
    QString desktopEntry() const { return QStringLiteral("music"); }
    QStringList supportedUriSchemes() const
    {
        return {QStringLiteral("http"), QStringLiteral("https")};
    }
    QStringList supportedMimeTypes() const
    {
        return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/mp4"),
                QStringLiteral("audio/flac")};
    }

  public slots:
    void Raise() {}
    void Quit() {}
};

class PlayerAdaptor final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus)
    Q_PROPERTY(double Rate READ rate CONSTANT)
    Q_PROPERTY(bool Shuffle READ shuffle CONSTANT)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume CONSTANT)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

  public:
    explicit PlayerAdaptor(LinuxMediaState *state);
    QString playbackStatus() const;
    QString loopStatus() const { return QStringLiteral("None"); }
    double rate() const { return 1.0; }
    bool shuffle() const { return false; }
    QVariantMap metadata() const;
    double volume() const { return 1.0; }
    qlonglong position() const;
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const { return true; }

  public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    void OpenUri(const QString &uri) { Q_UNUSED(uri); }

  private:
    LinuxMediaState *m_state;
};

class LinuxMediaState final : public QObject
{
  public:
    explicit LinuxMediaState(BackgroundPlayback *owner) : owner(owner)
    {
        new RootAdaptor(this);
        new PlayerAdaptor(this);
        auto bus = QDBusConnection::sessionBus();
        serviceName = QStringLiteral("org.mpris.MediaPlayer2.music");
        if (!bus.registerService(serviceName))
        {
            serviceName += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
            bus.registerService(serviceName);
        }
        registered = bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this,
                                        QDBusConnection::ExportAdaptors);
    }

    ~LinuxMediaState() override
    {
        auto bus = QDBusConnection::sessionBus();
        if (registered)
            bus.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
        if (!serviceName.isEmpty())
            bus.unregisterService(serviceName);
    }

    QVariantMap metadata() const
    {
        QVariantMap result;
        result.insert(QStringLiteral("mpris:trackid"),
                      QVariant::fromValue(QDBusObjectPath(
                          hasTrack ? QStringLiteral("/org/mpris/MediaPlayer2/track/current")
                                   : QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"))));
        result.insert(QStringLiteral("mpris:length"), duration * 1000);
        result.insert(QStringLiteral("xesam:title"), title);
        result.insert(QStringLiteral("xesam:artist"), QStringList{artist});
        return result;
    }

    void notifyChanged()
    {
        QVariantMap changed;
        changed.insert(QStringLiteral("PlaybackStatus"),
                       playing ? QStringLiteral("Playing")
                               : hasTrack ? QStringLiteral("Paused") : QStringLiteral("Stopped"));
        changed.insert(QStringLiteral("Metadata"), metadata());
        changed.insert(QStringLiteral("CanPlay"), hasTrack);
        changed.insert(QStringLiteral("CanPause"), hasTrack);
        changed.insert(QStringLiteral("CanSeek"), hasTrack && duration > 0);
        auto signal = QDBusMessage::createSignal(
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        signal << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList{};
        QDBusConnection::sessionBus().send(signal);
    }

    BackgroundPlayback *owner;
    QString serviceName;
    bool registered = false;
    bool hasTrack = false;
    bool playing = false;
    qint64 position = 0;
    qint64 duration = 0;
    QString title;
    QString artist;
};

RootAdaptor::RootAdaptor(LinuxMediaState *state) : QDBusAbstractAdaptor(state) {}
PlayerAdaptor::PlayerAdaptor(LinuxMediaState *state) : QDBusAbstractAdaptor(state), m_state(state)
{
}
QString PlayerAdaptor::playbackStatus() const
{
    return m_state->playing ? QStringLiteral("Playing")
                            : m_state->hasTrack ? QStringLiteral("Paused")
                                                : QStringLiteral("Stopped");
}
QVariantMap PlayerAdaptor::metadata() const { return m_state->metadata(); }
qlonglong PlayerAdaptor::position() const { return m_state->position * 1000; }
bool PlayerAdaptor::canGoNext() const { return m_state->hasTrack; }
bool PlayerAdaptor::canGoPrevious() const { return m_state->hasTrack; }
bool PlayerAdaptor::canPlay() const { return m_state->hasTrack; }
bool PlayerAdaptor::canPause() const { return m_state->hasTrack; }
bool PlayerAdaptor::canSeek() const { return m_state->hasTrack && m_state->duration > 0; }
void PlayerAdaptor::Next() { m_state->owner->dispatchNext(); }
void PlayerAdaptor::Previous() { m_state->owner->dispatchPrevious(); }
void PlayerAdaptor::Pause() { m_state->owner->dispatchPause(); }
void PlayerAdaptor::PlayPause() { m_state->owner->dispatchToggle(); }
void PlayerAdaptor::Stop() { m_state->owner->dispatchPause(); }
void PlayerAdaptor::Play() { m_state->owner->dispatchPlay(); }
void PlayerAdaptor::Seek(qlonglong offset)
{
    m_state->owner->dispatchSeek(qMax<qint64>(0, m_state->position + offset / 1000));
}
void PlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong position)
{
    Q_UNUSED(trackId);
    m_state->owner->dispatchSeek(qMax<qlonglong>(0, position / 1000));
}
} // namespace

void *createLinuxMediaIntegration(BackgroundPlayback *owner)
{
    return new LinuxMediaState(owner);
}

void destroyLinuxMediaIntegration(void *opaque)
{
    delete static_cast<LinuxMediaState *>(opaque);
}

void updateLinuxMediaIntegration(void *opaque, bool hasTrack, bool playing, qint64 position,
                                 qint64 duration, const QString &title, const QString &artist)
{
    auto *state = static_cast<LinuxMediaState *>(opaque);
    if (!state)
        return;
    const bool propertiesChanged = state->hasTrack != hasTrack || state->playing != playing ||
                                   state->duration != duration || state->title != title ||
                                   state->artist != artist;
    state->hasTrack = hasTrack;
    state->playing = playing;
    state->position = qMax<qint64>(0, position);
    state->duration = qMax<qint64>(0, duration);
    state->title = title;
    state->artist = artist;
    if (propertiesChanged)
        state->notifyChanged();
}

#include "BackgroundPlayback_linux.moc"
