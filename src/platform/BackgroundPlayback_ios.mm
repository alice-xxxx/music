#include "BackgroundPlayback.h"
#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>
#import <AVFoundation/AVFoundation.h>
#import <MediaPlayer/MediaPlayer.h>
#import <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

namespace
{
template <typename Callback> void dispatch(BackgroundPlayback *owner, Callback callback)
{
    if (!owner)
        return;
    QPointer<BackgroundPlayback> guard(owner);
    QMetaObject::invokeMethod(
        owner,
        [guard, callback]
        {
            if (guard)
                callback(guard.data());
        },
        Qt::AutoConnection);
}
}

@interface MusicRemoteCommandHandler : NSObject
@property(nonatomic, assign) BackgroundPlayback *owner;
- (MPRemoteCommandHandlerStatus)play:(MPRemoteCommandEvent *)event;
- (MPRemoteCommandHandlerStatus)pause:(MPRemoteCommandEvent *)event;
- (MPRemoteCommandHandlerStatus)toggle:(MPRemoteCommandEvent *)event;
- (MPRemoteCommandHandlerStatus)next:(MPRemoteCommandEvent *)event;
- (MPRemoteCommandHandlerStatus)previous:(MPRemoteCommandEvent *)event;
- (MPRemoteCommandHandlerStatus)seek:(MPChangePlaybackPositionCommandEvent *)event;
@end

@implementation MusicRemoteCommandHandler
- (MPRemoteCommandHandlerStatus)play:(MPRemoteCommandEvent *)event
{
    Q_UNUSED(event);
    dispatch(self.owner, [](BackgroundPlayback *owner) { owner->dispatchPlay(); });
    return MPRemoteCommandHandlerStatusSuccess;
}
- (MPRemoteCommandHandlerStatus)pause:(MPRemoteCommandEvent *)event
{
    Q_UNUSED(event);
    dispatch(self.owner, [](BackgroundPlayback *owner) { owner->dispatchPause(); });
    return MPRemoteCommandHandlerStatusSuccess;
}
- (MPRemoteCommandHandlerStatus)toggle:(MPRemoteCommandEvent *)event
{
    Q_UNUSED(event);
    dispatch(self.owner, [](BackgroundPlayback *owner) { owner->dispatchToggle(); });
    return MPRemoteCommandHandlerStatusSuccess;
}
- (MPRemoteCommandHandlerStatus)next:(MPRemoteCommandEvent *)event
{
    Q_UNUSED(event);
    dispatch(self.owner, [](BackgroundPlayback *owner) { owner->dispatchNext(); });
    return MPRemoteCommandHandlerStatusSuccess;
}
- (MPRemoteCommandHandlerStatus)previous:(MPRemoteCommandEvent *)event
{
    Q_UNUSED(event);
    dispatch(self.owner, [](BackgroundPlayback *owner) { owner->dispatchPrevious(); });
    return MPRemoteCommandHandlerStatusSuccess;
}
- (MPRemoteCommandHandlerStatus)seek:(MPChangePlaybackPositionCommandEvent *)event
{
    const qint64 position = static_cast<qint64>(event.positionTime * 1000.0);
    dispatch(self.owner,
             [position](BackgroundPlayback *owner) { owner->dispatchSeek(position); });
    return MPRemoteCommandHandlerStatusSuccess;
}
@end

struct AppleMediaState
{
    BackgroundPlayback *owner = nullptr;
    MusicRemoteCommandHandler *handler = nil;
    id interruptionObserver = nil;
    id routeObserver = nil;
    MPMediaItemArtwork *artwork = nil;
    bool hasTrack = false;
    bool desiredPlaying = false;
    bool playing = false;
    bool seekable = false;
    bool canSkipNext = false;
    bool canSkipPrevious = false;
    qint64 position = 0;
    qint64 duration = 0;
    QString title;
    QString artist;
    QTimer *updateTimer = nullptr;
    QElapsedTimer publishedAt;
    qint64 publishedPosition = 0;
    bool metadataDirty = true;
#if TARGET_OS_IPHONE
    bool sessionActive = false;
    bool transitionProgress = false;
    UIBackgroundTaskIdentifier transitionTask = UIBackgroundTaskInvalid;
#endif
};

bool setApplePlaybackActive(void *opaque, bool active);

void endApplePlaybackTransition(AppleMediaState *state)
{
#if TARGET_OS_IPHONE
    if (state->transitionTask == UIBackgroundTaskInvalid)
        return;
    const auto task = state->transitionTask;
    state->transitionTask = UIBackgroundTaskInvalid;
    [[UIApplication sharedApplication] endBackgroundTask:task];
#else
    Q_UNUSED(state);
#endif
}

void beginApplePlaybackTransition(void *opaque)
{
#if TARGET_OS_IPHONE
    auto *state = static_cast<AppleMediaState *>(opaque);
    if (!state)
        return;
    state->transitionProgress = false;
    if (state->transitionTask != UIBackgroundTaskInvalid)
        return;
    // Audio background mode alone does not protect the silent interval while
    // resolving a URL and buffering the next track. This lease is finite.
    state->transitionTask = [[UIApplication sharedApplication]
        beginBackgroundTaskWithName:@"Prepare next audio"
        expirationHandler:^{
            qWarning("Background audio preparation time expired");
            endApplePlaybackTransition(state);
        }];
    state->updateTimer->start();
#else
    Q_UNUSED(opaque);
#endif
}

void publishAppleNowPlaying(AppleMediaState *state)
{
    MPRemoteCommandCenter *commands = [MPRemoteCommandCenter sharedCommandCenter];
    // Explicit play/pause requests are idempotent in the controller. Keep both
    // enabled during buffering and interruptions, including headset commands.
    commands.playCommand.enabled = state->hasTrack;
    commands.pauseCommand.enabled = state->hasTrack;
    commands.togglePlayPauseCommand.enabled = state->hasTrack;
    commands.nextTrackCommand.enabled = state->hasTrack && state->canSkipNext;
    commands.previousTrackCommand.enabled = state->hasTrack && state->canSkipPrevious;
    commands.changePlaybackPositionCommand.enabled = state->hasTrack && state->seekable &&
        state->duration > 0;

    state->publishedPosition = state->position;
    state->publishedAt.restart();
    state->metadataDirty = false;
    MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
    if (!state->hasTrack)
    {
        center.nowPlayingInfo = nil;
        return;
    }
    NSMutableDictionary *info = [NSMutableDictionary dictionary];
    info[MPMediaItemPropertyTitle] = state->title.toNSString();
    info[MPMediaItemPropertyArtist] = state->artist.toNSString();
    if (state->duration > 0)
        info[MPMediaItemPropertyPlaybackDuration] = @(state->duration / 1000.0);
    if (state->artwork)
        info[MPMediaItemPropertyArtwork] = state->artwork;
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] =
        @(qMax<qint64>(0, state->position) / 1000.0);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(state->playing ? 1.0 : 0.0);
    center.nowPlayingInfo = info;
#if !TARGET_OS_IPHONE
    if (@available(macOS 10.12.2, *))
        center.playbackState = state->playing ? MPNowPlayingPlaybackStatePlaying
                                              : MPNowPlayingPlaybackStatePaused;
#endif
}

void *createAppleMediaIntegration(BackgroundPlayback *owner)
{
    auto *state = new AppleMediaState;
    state->owner = owner;
    state->handler = [[MusicRemoteCommandHandler alloc] init];
    state->handler.owner = owner;
    state->updateTimer = new QTimer(owner);
    state->updateTimer->setSingleShot(true);
    QObject::connect(state->updateTimer, &QTimer::timeout, owner, [state]
    {
#if TARGET_OS_IPHONE
        if (!state->hasTrack && state->sessionActive)
            setApplePlaybackActive(state, false);
        else if (state->hasTrack && state->desiredPlaying && !state->sessionActive)
            setApplePlaybackActive(state, true);
#endif
        // Coalesce stop/reset/start snapshots from a single track change.
        // Wait for progress, not just PlayingState, before releasing the lease.
        if (!state->hasTrack || !state->desiredPlaying || state->transitionProgress)
            endApplePlaybackTransition(state);

        const qint64 elapsed = state->publishedAt.isValid() ? state->publishedAt.elapsed() : 0;
        const qint64 expected = state->publishedPosition + (state->playing ? elapsed : 0);
        // The system extrapolates elapsed time; don't rebuild its metadata on
        // every position tick. State changes and seeks still publish immediately.
        if (state->metadataDirty || !state->publishedAt.isValid() || elapsed >= 5000 ||
            qAbs(state->position - expected) > 1500)
            publishAppleNowPlaying(state);
    });

    MPRemoteCommandCenter *commands = [MPRemoteCommandCenter sharedCommandCenter];
    [commands.playCommand addTarget:state->handler action:@selector(play:)];
    [commands.pauseCommand addTarget:state->handler action:@selector(pause:)];
    [commands.togglePlayPauseCommand addTarget:state->handler action:@selector(toggle:)];
    [commands.nextTrackCommand addTarget:state->handler action:@selector(next:)];
    [commands.previousTrackCommand addTarget:state->handler action:@selector(previous:)];
    [commands.changePlaybackPositionCommand addTarget:state->handler action:@selector(seek:)];

#if TARGET_OS_IPHONE
    const QPointer<BackgroundPlayback> ownerGuard(owner);
    NSNotificationCenter *notifications = [NSNotificationCenter defaultCenter];
    state->interruptionObserver =
        [notifications addObserverForName:AVAudioSessionInterruptionNotification
                                  object:[AVAudioSession sharedInstance]
                                   queue:[NSOperationQueue mainQueue]
                              usingBlock:^(NSNotification *notification) {
        if (!ownerGuard)
            return;
        NSDictionary *info = notification.userInfo;
        AVAudioSessionInterruptionType type =
            static_cast<AVAudioSessionInterruptionType>(
                [info[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]);
        if (type == AVAudioSessionInterruptionTypeBegan)
        {
            state->sessionActive = false;
            dispatch(ownerGuard.data(), [](BackgroundPlayback *control)
                     { control->dispatchInterruptionBegan(true); });
            return;
        }
        AVAudioSessionInterruptionOptions options =
            static_cast<AVAudioSessionInterruptionOptions>(
                [info[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue]);
        const bool shouldResume = options & AVAudioSessionInterruptionOptionShouldResume;
        dispatch(ownerGuard.data(), [shouldResume](BackgroundPlayback *control)
                 { control->dispatchInterruptionEnded(shouldResume); });
    }];
    state->routeObserver =
        [notifications addObserverForName:AVAudioSessionRouteChangeNotification
                                  object:[AVAudioSession sharedInstance]
                                   queue:[NSOperationQueue mainQueue]
                              usingBlock:^(NSNotification *notification) {
        if (!ownerGuard)
            return;
        AVAudioSessionRouteChangeReason reason = static_cast<AVAudioSessionRouteChangeReason>(
            [notification.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue]);
        if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable)
            dispatch(ownerGuard.data(), [](BackgroundPlayback *control)
                     { control->dispatchOutputDisconnected(); });
        else if (reason == AVAudioSessionRouteChangeReasonCategoryChange &&
                 state->hasTrack && state->desiredPlaying)
            QMetaObject::invokeMethod(ownerGuard.data(), [ownerGuard, state]
            {
                if (ownerGuard && state->hasTrack && state->desiredPlaying)
                    setApplePlaybackActive(state, true);
            }, Qt::QueuedConnection);
    }];
#endif
    return state;
}

void destroyAppleMediaIntegration(void *opaque)
{
    auto *state = static_cast<AppleMediaState *>(opaque);
    if (!state)
        return;
    delete state->updateTimer;
    endApplePlaybackTransition(state);
    MPRemoteCommandCenter *commands = [MPRemoteCommandCenter sharedCommandCenter];
    [commands.playCommand removeTarget:state->handler];
    [commands.pauseCommand removeTarget:state->handler];
    [commands.togglePlayPauseCommand removeTarget:state->handler];
    [commands.nextTrackCommand removeTarget:state->handler];
    [commands.previousTrackCommand removeTarget:state->handler];
    [commands.changePlaybackPositionCommand removeTarget:state->handler];
    NSNotificationCenter *notifications = [NSNotificationCenter defaultCenter];
    if (state->interruptionObserver)
        [notifications removeObserver:state->interruptionObserver];
    if (state->routeObserver)
        [notifications removeObserver:state->routeObserver];
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    state->handler.owner = nullptr;
#if !__has_feature(objc_arc)
    [state->artwork release];
    [state->handler release];
#endif
    delete state;
}

bool setApplePlaybackActive(void *opaque, bool active)
{
#if TARGET_OS_IPHONE
    auto *state = static_cast<AppleMediaState *>(opaque);
    if (!state)
        return false;
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;
    const bool categoryChanged = ![session.category isEqualToString:AVAudioSessionCategoryPlayback]
        || ![session.mode isEqualToString:AVAudioSessionModeDefault] || session.categoryOptions != 0;
    if (active && categoryChanged && ![session setCategory:AVAudioSessionCategoryPlayback
                                    mode:AVAudioSessionModeDefault
                                 options:0
                                   error:&error])
    {
        qWarning("Could not configure the Apple playback audio session: %ld",
                 static_cast<long>(error.code));
        return false;
    }
    // Screen locking must not reconfigure a healthy audio session.
    if ((!active && !state->sessionActive) ||
        (active && state->sessionActive && !categoryChanged))
        return true;
    const AVAudioSessionSetActiveOptions options = active
        ? static_cast<AVAudioSessionSetActiveOptions>(0)
        : AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation;
    if (![session setActive:active withOptions:options error:&error])
    {
        qWarning("Could not change the Apple playback audio session state: %ld",
                 static_cast<long>(error.code));
        return false;
    }
    state->sessionActive = active;
#else
    Q_UNUSED(opaque);
    Q_UNUSED(active);
#endif
    return true;
}

void updateAppleNowPlaying(void *opaque, bool hasTrack, bool desiredPlaying, bool playing,
                           bool seekable, bool canSkipNext, bool canSkipPrevious,
                           qint64 position, qint64 duration, const QString &title,
                           const QString &artist)
{
    auto *state = static_cast<AppleMediaState *>(opaque);
    if (!state)
        return;
#if TARGET_OS_IPHONE
    if (state->transitionTask != UIBackgroundTaskInvalid && playing && state->playing &&
        position > state->position)
        state->transitionProgress = true;
#endif
    state->metadataDirty = state->metadataDirty || state->hasTrack != hasTrack
        || state->desiredPlaying != desiredPlaying || state->playing != playing
        || state->seekable != seekable || state->canSkipNext != canSkipNext ||
        state->canSkipPrevious != canSkipPrevious || state->duration != duration ||
        state->title != title || state->artist != artist;
    state->hasTrack = hasTrack;
    state->desiredPlaying = desiredPlaying;
    state->playing = playing;
    state->seekable = seekable;
    state->canSkipNext = canSkipNext;
    state->canSkipPrevious = canSkipPrevious;
    state->position = position;
    state->duration = duration;
    state->title = title;
    state->artist = artist;
    state->updateTimer->start();
}

void updateAppleNowPlayingArtwork(void *opaque, const QByteArray &data)
{
    auto *state = static_cast<AppleMediaState *>(opaque);
    if (!state)
        return;
#if !__has_feature(objc_arc)
    [state->artwork release];
#endif
    state->artwork = nil;
    state->metadataDirty = true;
    if (data.isEmpty())
    {
        state->updateTimer->start();
        return;
    }

    NSData *encoded = [NSData dataWithBytes:data.constData()
                                     length:static_cast<NSUInteger>(data.size())];
#if TARGET_OS_IPHONE
    UIImage *image = [UIImage imageWithData:encoded];
    if (image)
        state->artwork = [[MPMediaItemArtwork alloc]
            initWithBoundsSize:image.size
                requestHandler:^UIImage *(CGSize) { return image; }];
#else
    NSImage *image = [[NSImage alloc] initWithData:encoded];
    if (image)
        state->artwork = [[MPMediaItemArtwork alloc]
            initWithBoundsSize:image.size
                requestHandler:^NSImage *(CGSize) { return image; }];
#if !__has_feature(objc_arc)
    [image release];
#endif
#endif
    state->updateTimer->start();
}
