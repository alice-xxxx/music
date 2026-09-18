#include "BackgroundPlayback.h"
#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#import <AVFoundation/AVFoundation.h>
#import <MediaPlayer/MediaPlayer.h>
#import <TargetConditionals.h>

namespace
{
template <typename Callback> void dispatch(BackgroundPlayback *owner, Callback callback)
{
    QPointer<BackgroundPlayback> guard(owner);
    QMetaObject::invokeMethod(
        owner,
        [guard, callback]
        {
            if (guard)
                callback(guard.data());
        },
        Qt::QueuedConnection);
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
};

void *createAppleMediaIntegration(BackgroundPlayback *owner)
{
    auto *state = new AppleMediaState;
    state->owner = owner;
    state->handler = [[MusicRemoteCommandHandler alloc] init];
    state->handler.owner = owner;

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
    }];
#endif
    return state;
}

void destroyAppleMediaIntegration(void *opaque)
{
    auto *state = static_cast<AppleMediaState *>(opaque);
    if (!state)
        return;
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
    [state->handler release];
#endif
    delete state;
}

bool setApplePlaybackActive(void *opaque, bool active)
{
    Q_UNUSED(opaque);
#if TARGET_OS_IPHONE
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;
    if (active && ![session setCategory:AVAudioSessionCategoryPlayback
                                    mode:AVAudioSessionModeDefault
                                 options:0
                                   error:&error])
    {
        qWarning("Could not configure the Apple playback audio session: %ld",
                 static_cast<long>(error.code));
        return false;
    }
    const AVAudioSessionSetActiveOptions options = active
        ? static_cast<AVAudioSessionSetActiveOptions>(0)
        : AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation;
    if (![session setActive:active withOptions:options error:&error])
    {
        qWarning("Could not change the Apple playback audio session state: %ld",
                 static_cast<long>(error.code));
        return false;
    }
#else
    Q_UNUSED(active);
#endif
    return true;
}

void updateAppleNowPlaying(void *opaque, bool hasTrack, bool playing, qint64 position,
                           qint64 duration, const QString &title, const QString &artist)
{
    auto *state = static_cast<AppleMediaState *>(opaque);
    if (!state)
        return;
    MPRemoteCommandCenter *commands = [MPRemoteCommandCenter sharedCommandCenter];
    commands.playCommand.enabled = hasTrack && !playing;
    commands.pauseCommand.enabled = hasTrack && playing;
    commands.togglePlayPauseCommand.enabled = hasTrack;
    commands.nextTrackCommand.enabled = hasTrack;
    commands.previousTrackCommand.enabled = hasTrack;
    commands.changePlaybackPositionCommand.enabled = hasTrack && duration > 0;

    MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
    if (!hasTrack)
    {
        center.nowPlayingInfo = nil;
        return;
    }
    NSMutableDictionary *info = [NSMutableDictionary dictionary];
    info[MPMediaItemPropertyTitle] = title.toNSString();
    info[MPMediaItemPropertyArtist] = artist.toNSString();
    if (duration > 0)
        info[MPMediaItemPropertyPlaybackDuration] = @(duration / 1000.0);
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(qMax<qint64>(0, position) / 1000.0);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
    center.nowPlayingInfo = info;
    if (@available(iOS 13.0, macOS 10.12.2, *))
        center.playbackState = playing ? MPNowPlayingPlaybackStatePlaying
                                       : MPNowPlayingPlaybackStatePaused;
}
