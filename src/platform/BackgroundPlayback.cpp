#include "BackgroundPlayback.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QtGlobal>

namespace
{
constexpr qsizetype maximumArtworkBytes = 8 * 1024 * 1024;

QString normalizedArtworkUrl(QString url)
{
    url = url.trimmed();
    url.replace(QStringLiteral("{size}"), QStringLiteral("400"));
    if (url.startsWith(QStringLiteral("//")))
        url.prepend(QStringLiteral("https:"));
    return url;
}
} // namespace

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef MUSIC_APP_HAS_WINRT
void *createWindowsMediaIntegration(BackgroundPlayback *owner);
void destroyWindowsMediaIntegration(void *state);
bool windowsMediaIntegrationHandlesCommands(void *state);
void updateWindowsMediaIntegration(void *state, bool hasTrack, bool playing, qint64 position,
                                   qint64 duration, const QString &title, const QString &artist);
#endif

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
void *createAppleMediaIntegration(BackgroundPlayback *owner);
void destroyAppleMediaIntegration(void *state);
bool setApplePlaybackActive(void *state, bool active);
void beginApplePlaybackTransition(void *state);
void updateAppleNowPlaying(void *state, bool hasTrack, bool desiredPlaying, bool playing,
                           bool seekable, bool canSkipNext, bool canSkipPrevious,
                           qint64 position, qint64 duration, const QString &title,
                           const QString &artist);
void updateAppleNowPlayingArtwork(void *state, const QByteArray &data);
#endif

#ifdef MUSIC_APP_HAS_DBUS
void *createLinuxMediaIntegration(BackgroundPlayback *owner);
void destroyLinuxMediaIntegration(void *state);
void updateLinuxMediaIntegration(void *state, bool hasTrack, bool playing, qint64 position,
                                 qint64 duration, const QString &title, const QString &artist);
#endif

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

namespace
{
QMutex androidOwnerMutex;
QPointer<BackgroundPlayback> androidOwner;

template <typename Callback> void dispatchAndroid(Callback callback)
{
    QPointer<BackgroundPlayback> owner;
    {
        QMutexLocker locker(&androidOwnerMutex);
        owner = androidOwner;
    }
    if (!owner)
        return;
    QMetaObject::invokeMethod(
        owner,
        [owner, callback]
        {
            if (owner)
                callback(owner.data());
        },
        Qt::QueuedConnection);
}
} // namespace

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativePlay(JNIEnv *, jclass)
{
    dispatchAndroid([](BackgroundPlayback *owner) { owner->dispatchPlay(); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativePause(JNIEnv *, jclass)
{
    dispatchAndroid([](BackgroundPlayback *owner) { owner->dispatchPause(); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativeToggle(JNIEnv *, jclass)
{
    dispatchAndroid([](BackgroundPlayback *owner) { owner->dispatchToggle(); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativeNext(JNIEnv *, jclass)
{
    dispatchAndroid([](BackgroundPlayback *owner) { owner->dispatchNext(); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativePrevious(JNIEnv *, jclass)
{
    dispatchAndroid([](BackgroundPlayback *owner) { owner->dispatchPrevious(); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativeSeek(JNIEnv *, jclass, jlong position)
{
    dispatchAndroid(
        [position](BackgroundPlayback *owner) { owner->dispatchSeek(position); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativeInterruptionBegan(JNIEnv *, jclass,
                                                                       jboolean resumable)
{
    dispatchAndroid([resumable](BackgroundPlayback *owner)
                    { owner->dispatchInterruptionBegan(resumable); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativeInterruptionEnded(JNIEnv *, jclass,
                                                                       jboolean shouldResume)
{
    dispatchAndroid([shouldResume](BackgroundPlayback *owner)
                    { owner->dispatchInterruptionEnded(shouldResume); });
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_musicclient_app_PlaybackService_nativeOutputDisconnected(JNIEnv *, jclass)
{
    dispatchAndroid(
        [](BackgroundPlayback *owner) { owner->dispatchOutputDisconnected(); });
}
#endif

BackgroundPlayback::BackgroundPlayback(QObject *parent) : QObject(parent)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    m_artworkNetwork = new QNetworkAccessManager(this);
    auto *cache = new QNetworkDiskCache(m_artworkNetwork);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                             QStringLiteral("/media-session-artwork"));
    cache->setMaximumCacheSize(64 * 1024 * 1024);
    m_artworkNetwork->setCache(cache);
#endif

#ifdef Q_OS_WIN
#ifdef MUSIC_APP_HAS_WINRT
    m_platformState = createWindowsMediaIntegration(this);
#endif
    QCoreApplication::instance()->installNativeEventFilter(this);
#elif defined(Q_OS_ANDROID)
    QMutexLocker locker(&androidOwnerMutex);
    androidOwner = this;
#elif defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    m_platformState = createAppleMediaIntegration(this);
#elif defined(MUSIC_APP_HAS_DBUS)
    m_platformState = createLinuxMediaIntegration(this);
#endif
}

BackgroundPlayback::~BackgroundPlayback()
{
#ifdef Q_OS_WIN
    QCoreApplication::instance()->removeNativeEventFilter(this);
#ifdef MUSIC_APP_HAS_WINRT
    destroyWindowsMediaIntegration(m_platformState);
#endif
#elif defined(Q_OS_ANDROID)
    {
        QMutexLocker locker(&androidOwnerMutex);
        if (androidOwner == this)
            androidOwner.clear();
    }
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid())
        QJniObject::callStaticMethod<void>("io/github/musicclient/app/PlaybackService", "shutdown",
                                           "(Landroid/content/Context;)V",
                                           context.object<jobject>());
#elif defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    destroyAppleMediaIntegration(m_platformState);
#elif defined(MUSIC_APP_HAS_DBUS)
    destroyLinuxMediaIntegration(m_platformState);
#endif
}

#ifdef Q_OS_WIN
bool BackgroundPlayback::nativeEventFilter(const QByteArray &eventType, void *message,
                                           qintptr *result)
{
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const auto *native = static_cast<MSG *>(message);
    if (native->message != WM_APPCOMMAND)
        return false;
#ifdef MUSIC_APP_HAS_WINRT
    if (windowsMediaIntegrationHandlesCommands(m_platformState))
        return false;
#endif
    switch (GET_APPCOMMAND_LPARAM(native->lParam))
    {
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
        dispatchToggle();
        break;
    case APPCOMMAND_MEDIA_NEXTTRACK:
        dispatchNext();
        break;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
        dispatchPrevious();
        break;
    case APPCOMMAND_MEDIA_PLAY:
        dispatchPlay();
        break;
    case APPCOMMAND_MEDIA_PAUSE:
    case APPCOMMAND_MEDIA_STOP:
        dispatchPause();
        break;
    default:
        return false;
    }
    *result = 1;
    return true;
}
#endif

void BackgroundPlayback::update(bool hasTrack, bool desiredPlaying, bool playing, bool buffering,
                                bool failed, bool seekable, bool canSkipNext,
                                bool canSkipPrevious, qint64 position, qint64 duration,
                                const QString &title, const QString &artist,
                                const QString &artworkUrl)
{
    const auto state = !hasTrack ? MediaSessionState::None
                       : failed ? MediaSessionState::Error
                       : !desiredPlaying ? MediaSessionState::Paused
                       : buffering || !playing ? MediaSessionState::Buffering
                                               : MediaSessionState::Playing;
    const bool audible = state == MediaSessionState::Playing;
    updateArtwork(hasTrack ? artworkUrl : QString{});

#ifdef Q_OS_WIN
#ifdef MUSIC_APP_HAS_WINRT
    updateWindowsMediaIntegration(m_platformState, hasTrack, audible, position, duration, title,
                                  artist);
#else
    Q_UNUSED(hasTrack);
    Q_UNUSED(desiredPlaying);
    Q_UNUSED(playing);
    Q_UNUSED(position);
    Q_UNUSED(duration);
    Q_UNUSED(title);
    Q_UNUSED(artist);
    Q_UNUSED(seekable);
    Q_UNUSED(canSkipNext);
    Q_UNUSED(canSkipPrevious);
#endif
#elif defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid())
    {
        const QJniObject javaTitle = QJniObject::fromString(title);
        const QJniObject javaArtist = QJniObject::fromString(artist);
        QJniObject::callStaticMethod<void>(
            "io/github/musicclient/app/PlaybackService", "updateSession",
            "(Landroid/content/Context;ZZIZZZJJLjava/lang/String;Ljava/lang/String;)V",
            context.object<jobject>(), static_cast<jboolean>(hasTrack),
            static_cast<jboolean>(desiredPlaying), static_cast<jint>(state),
            static_cast<jboolean>(seekable), static_cast<jboolean>(canSkipNext),
            static_cast<jboolean>(canSkipPrevious),
            static_cast<jlong>(position), static_cast<jlong>(duration),
            javaTitle.object<jstring>(), javaArtist.object<jstring>());
    }
#elif defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    updateAppleNowPlaying(m_platformState, hasTrack, desiredPlaying, audible, seekable,
                          canSkipNext, canSkipPrevious, position, duration, title, artist);
#elif defined(MUSIC_APP_HAS_DBUS)
    updateLinuxMediaIntegration(m_platformState, hasTrack, audible, position, duration, title,
                                artist);
    Q_UNUSED(seekable);
    Q_UNUSED(canSkipNext);
    Q_UNUSED(canSkipPrevious);
#else
    Q_UNUSED(hasTrack);
    Q_UNUSED(desiredPlaying);
    Q_UNUSED(playing);
    Q_UNUSED(position);
    Q_UNUSED(duration);
    Q_UNUSED(title);
    Q_UNUSED(artist);
    Q_UNUSED(seekable);
    Q_UNUSED(canSkipNext);
    Q_UNUSED(canSkipPrevious);
#endif
}

void BackgroundPlayback::beginPlaybackTransition()
{
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    beginApplePlaybackTransition(m_platformState);
#endif
}

void BackgroundPlayback::preparePlayback()
{
    beginPlaybackTransition();
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    setApplePlaybackActive(m_platformState, true);
#endif
}

void BackgroundPlayback::updateArtwork(const QString &artworkUrl)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    const QString normalizedUrl = normalizedArtworkUrl(artworkUrl);
    if (normalizedUrl == m_artworkUrl)
        return;
    m_artworkUrl = normalizedUrl;
    if (m_artworkReply)
    {
        m_artworkReply->abort();
        m_artworkReply->deleteLater();
        m_artworkReply.clear();
    }
    m_artworkData.clear();
    publishArtwork();

    const QUrl url(normalizedUrl);
    if (!m_artworkNetwork || !url.isValid() ||
        (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
        return;

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
    auto *reply = m_artworkNetwork->get(request);
    m_artworkReply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, normalizedUrl]
            {
                if (reply != m_artworkReply || normalizedUrl != m_artworkUrl)
                {
                    reply->deleteLater();
                    return;
                }
                m_artworkReply.clear();
                const QByteArray data = reply->error() == QNetworkReply::NoError
                                                ? reply->readAll()
                                                : QByteArray{};
                reply->deleteLater();
                if (data.isEmpty() || data.size() > maximumArtworkBytes)
                    return;
                m_artworkData = data;
                publishArtwork();
            });
#else
    Q_UNUSED(artworkUrl);
#endif
}

void BackgroundPlayback::publishArtwork()
{
#ifdef Q_OS_ANDROID
    QJniEnvironment environment;
    jbyteArray data = environment->NewByteArray(static_cast<jsize>(m_artworkData.size()));
    if (!data)
        return;
    if (!m_artworkData.isEmpty())
        environment->SetByteArrayRegion(
            data, 0, static_cast<jsize>(m_artworkData.size()),
            reinterpret_cast<const jbyte *>(m_artworkData.constData()));
    QJniObject::callStaticMethod<void>("io/github/musicclient/app/PlaybackService",
                                       "updateArtwork", "([B)V", data);
    environment->DeleteLocalRef(data);
#elif defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    updateAppleNowPlayingArtwork(m_platformState, m_artworkData);
#endif
}

void BackgroundPlayback::dispatchPlay()
{
    emit playRequested();
}
void BackgroundPlayback::dispatchPause()
{
    emit pauseRequested();
}
void BackgroundPlayback::dispatchToggle()
{
    emit toggleRequested();
}
void BackgroundPlayback::dispatchNext()
{
    emit nextRequested();
}
void BackgroundPlayback::dispatchPrevious()
{
    emit previousRequested();
}
void BackgroundPlayback::dispatchSeek(qint64 position)
{
    emit seekRequested(position);
}
void BackgroundPlayback::dispatchInterruptionBegan(bool resumable)
{
    emit interruptionBegan(resumable);
}
void BackgroundPlayback::dispatchInterruptionEnded(bool shouldResume)
{
    emit interruptionEnded(shouldResume);
}
void BackgroundPlayback::dispatchOutputDisconnected()
{
    emit outputDisconnected();
}
