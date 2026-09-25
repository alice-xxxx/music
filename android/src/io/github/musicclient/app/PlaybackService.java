package io.github.musicclient.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.util.Log;
import java.util.concurrent.atomic.AtomicLong;

public final class PlaybackService extends Service {
    private static final String TAG = "MusicPlayback";
    private static final String CHANNEL_ID = "music_playback";
    private static final int NOTIFICATION_ID = 1;
    private static final String ACTION_TOGGLE = "io.github.musicclient.app.action.TOGGLE";
    private static final String ACTION_NEXT = "io.github.musicclient.app.action.NEXT";
    private static final String ACTION_PREVIOUS = "io.github.musicclient.app.action.PREVIOUS";
    private static final String EXTRA_HAS_TRACK = "hasTrack";
    private static final String EXTRA_DESIRED = "desired";
    private static final String EXTRA_STATUS = "status";
    private static final String EXTRA_SEEKABLE = "seekable";
    private static final String EXTRA_CAN_NEXT = "canNext";
    private static final String EXTRA_CAN_PREVIOUS = "canPrevious";
    private static final String EXTRA_SEQUENCE = "sequence";
    private static final String EXTRA_POSITION = "position";
    private static final String EXTRA_DURATION = "duration";
    private static final String EXTRA_TITLE = "title";
    private static final String EXTRA_ARTIST = "artist";
    // Keep these values aligned with MediaSessionState in BackgroundPlayback.h.
    private static final int STATUS_NONE = 0;
    private static final int STATUS_PAUSED = 1;
    private static final int STATUS_BUFFERING = 2;
    private static final int STATUS_PLAYING = 3;
    private static final int STATUS_ERROR = 4;

    private static volatile PlaybackService instance;
    private static volatile byte[] pendingArtwork;
    private static volatile boolean latestHasTrack;
    private static final AtomicLong latestUpdate = new AtomicLong();

    private final Handler handler = new Handler(Looper.getMainLooper());
    private MediaSession mediaSession;
    private AudioManager audioManager;
    private AudioFocusRequest focusRequest;
    private PowerManager.WakeLock wakeLock;
    private boolean hasAudioFocus;
    private boolean foreground;
    private boolean resumeOnFocusGain;
    private boolean focusDenialPending;
    private boolean hasTrack;
    private boolean desiredPlaying;
    private boolean seekable;
    private boolean canSkipNext;
    private boolean canSkipPrevious;
    private int playbackStatus = STATUS_NONE;
    private long position;
    private long duration;
    private String title = "";
    private String artist = "";
    private Bitmap artwork;
    private boolean metadataDirty = true;
    private boolean statePublished;
    private int publishedStatus = STATUS_NONE;
    private boolean publishedSeekable;
    private boolean publishedCanNext;
    private boolean publishedCanPrevious;
    private long publishedPosition;
    private long publishedAt;

    private static native void nativePlay();
    private static native void nativePause();
    private static native void nativeToggle();
    private static native void nativeNext();
    private static native void nativePrevious();
    private static native void nativeSeek(long position);
    private static native void nativeInterruptionBegan(boolean resumable);
    private static native void nativeInterruptionEnded(boolean shouldResume);
    private static native void nativeOutputDisconnected();

    public static void updateSession(Context context, boolean hasTrack, boolean desired,
            int status, boolean seekable, boolean canNext, boolean canPrevious,
            long position, long duration, String title, String artist) {
        long sequence = latestUpdate.incrementAndGet();
        latestHasTrack = hasTrack;
        PlaybackService service = instance;
        if (service != null) {
            service.handler.post(() -> {
                if (sequence == latestUpdate.get())
                    service.applyState(hasTrack, desired, status, seekable, canNext, canPrevious,
                            position,
                            duration, title, artist);
            });
            return;
        }
        Context application = context.getApplicationContext();
        if (!hasTrack) {
            application.stopService(new Intent(application, PlaybackService.class));
            return;
        }
        Intent intent = stateIntent(application, sequence, hasTrack, desired, status, seekable,
                canNext, canPrevious, position, duration, title, artist);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                application.startForegroundService(intent);
            else
                application.startService(intent);
        } catch (RuntimeException error) {
            Log.e(TAG, "Unable to prepare the playback media session", error);
        }
    }

    public static void updateArtwork(byte[] encoded) {
        pendingArtwork = encoded;
        PlaybackService service = instance;
        if (service == null)
            return;
        service.handler.post(() -> service.applyArtwork(encoded));
    }

    public static void shutdown(Context context) {
        context.getApplicationContext().stopService(
                new Intent(context.getApplicationContext(), PlaybackService.class));
    }

    private static Intent stateIntent(Context context, long sequence, boolean hasTrack,
            boolean desired, int status, boolean seekable, boolean canNext,
            boolean canPrevious, long position, long duration, String title, String artist) {
        return new Intent(context, PlaybackService.class)
                .putExtra(EXTRA_SEQUENCE, sequence)
                .putExtra(EXTRA_HAS_TRACK, hasTrack)
                .putExtra(EXTRA_DESIRED, desired)
                .putExtra(EXTRA_STATUS, status)
                .putExtra(EXTRA_SEEKABLE, seekable)
                .putExtra(EXTRA_CAN_NEXT, canNext)
                .putExtra(EXTRA_CAN_PREVIOUS, canPrevious)
                .putExtra(EXTRA_POSITION, position)
                .putExtra(EXTRA_DURATION, duration)
                .putExtra(EXTRA_TITLE, title)
                .putExtra(EXTRA_ARTIST, artist);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
        audioManager = (AudioManager)getSystemService(AUDIO_SERVICE);
        createNotificationChannel();
        createMediaSession();
        applyArtwork(pendingArtwork);
        IntentFilter filter = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            registerReceiver(noisyReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        else
            registerReceiver(noisyReceiver, filter);
    }

    private void createMediaSession() {
        mediaSession = new MediaSession(this, "MusicPlayback");
        mediaSession.setFlags(MediaSession.FLAG_HANDLES_MEDIA_BUTTONS |
                MediaSession.FLAG_HANDLES_TRANSPORT_CONTROLS);
        mediaSession.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() {
                if (prepareUserPlayback())
                    nativePlay();
            }
            @Override public void onPause() {
                cancelFocusResume();
                nativePause();
            }
            @Override public void onStop() {
                cancelFocusResume();
                nativePause();
            }
            @Override public void onSkipToNext() {
                if (!canSkipNext)
                    return;
                if (prepareUserPlayback())
                    nativeNext();
            }
            @Override public void onSkipToPrevious() {
                if (!canSkipPrevious)
                    return;
                if (prepareUserPlayback())
                    nativePrevious();
            }
            @Override public void onSeekTo(long nextPosition) {
                if (seekable)
                    nativeSeek(nextPosition);
            }
            @Override public boolean onMediaButtonEvent(Intent mediaButtonIntent) {
                return super.onMediaButtonEvent(mediaButtonIntent);
            }
        });
        mediaSession.setActive(true);
        updateMediaSession();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        if (ACTION_TOGGLE.equals(action)) {
            if (desiredPlaying || prepareUserPlayback()) {
                if (desiredPlaying)
                    cancelFocusResume();
                nativeToggle();
            }
        } else if (ACTION_NEXT.equals(action)) {
            if (canSkipNext && prepareUserPlayback()) {
                nativeNext();
            }
        } else if (ACTION_PREVIOUS.equals(action)) {
            if (canSkipPrevious && prepareUserPlayback()) {
                nativePrevious();
            }
        }

        if (intent != null && intent.hasExtra(EXTRA_HAS_TRACK)) {
            if (intent.getLongExtra(EXTRA_SEQUENCE, 0) == latestUpdate.get())
                applyState(intent.getBooleanExtra(EXTRA_HAS_TRACK, false),
                    intent.getBooleanExtra(EXTRA_DESIRED, false),
                    intent.getIntExtra(EXTRA_STATUS, STATUS_NONE),
                    intent.getBooleanExtra(EXTRA_SEEKABLE, false),
                    intent.getBooleanExtra(EXTRA_CAN_NEXT, false),
                    intent.getBooleanExtra(EXTRA_CAN_PREVIOUS, false),
                    intent.getLongExtra(EXTRA_POSITION, 0),
                    intent.getLongExtra(EXTRA_DURATION, 0),
                    intent.getStringExtra(EXTRA_TITLE), intent.getStringExtra(EXTRA_ARTIST));
            else if (!latestHasTrack)
                stopSelf(startId);
        }
        return START_NOT_STICKY;
    }

    private void applyState(boolean hasTrack, boolean desired, int status, boolean seekable,
            boolean canNext, boolean canPrevious, long position, long duration,
            String title, String artist) {
        boolean wasDesired = desiredPlaying;
        String nextTitle = title == null ? "" : title;
        String nextArtist = artist == null ? "" : artist;
        metadataDirty |= this.hasTrack != hasTrack || this.duration != duration ||
                !this.title.equals(nextTitle) || !this.artist.equals(nextArtist);
        this.hasTrack = hasTrack;
        this.desiredPlaying = desired;
        this.playbackStatus = status;
        this.seekable = seekable;
        this.canSkipNext = canNext;
        this.canSkipPrevious = canPrevious;
        this.position = Math.max(0, position);
        this.duration = Math.max(0, duration);
        this.title = nextTitle;
        this.artist = nextArtist;
        updateMediaSession();
        if (hasTrack) {
            enterForeground();
            updatePlaybackResources(wasDesired);
        } else {
            leaveForeground(true, true);
            long sequence = latestUpdate.get();
            handler.postDelayed(() -> {
                if (!this.hasTrack && latestUpdate.get() == sequence)
                    stopSelf();
            }, 500);
        }
    }

    private void applyArtwork(byte[] encoded) {
        artwork = decodeArtwork(encoded);
        metadataDirty = true;
        updateMediaSession();
    }

    private static Bitmap decodeArtwork(byte[] encoded) {
        if (encoded == null || encoded.length == 0)
            return null;
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeByteArray(encoded, 0, encoded.length, bounds);
        int largest = Math.max(bounds.outWidth, bounds.outHeight);
        if (largest <= 0)
            return null;
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = 1;
        while (largest / options.inSampleSize > 400)
            options.inSampleSize *= 2;
        Bitmap decoded = BitmapFactory.decodeByteArray(encoded, 0, encoded.length, options);
        if (decoded == null)
            return null;
        int width = decoded.getWidth();
        int height = decoded.getHeight();
        if (Math.max(width, height) <= 400)
            return decoded;
        double scale = 400.0 / Math.max(width, height);
        Bitmap scaled = Bitmap.createScaledBitmap(decoded,
                Math.max(1, (int)(width * scale)), Math.max(1, (int)(height * scale)), true);
        if (scaled != decoded)
            decoded.recycle();
        return scaled;
    }

    private void updateMediaSession() {
        if (mediaSession == null)
            return;
        long now = SystemClock.elapsedRealtime();
        long elapsed = now - publishedAt;
        long expected = publishedPosition +
                (publishedStatus == STATUS_PLAYING ? elapsed : 0);
        boolean controlsChanged = !statePublished || playbackStatus != publishedStatus ||
                seekable != publishedSeekable || canSkipNext != publishedCanNext ||
                canSkipPrevious != publishedCanPrevious;
        boolean positionChanged = Math.abs(position - expected) > 1500 || elapsed >= 5000;
        if (controlsChanged || positionChanged) {
            long actions = 0;
            if (hasTrack) {
                actions = PlaybackState.ACTION_PLAY_PAUSE;
                actions |= desiredPlaying && playbackStatus != STATUS_ERROR
                        ? PlaybackState.ACTION_PAUSE : PlaybackState.ACTION_PLAY;
                if (canSkipNext)
                    actions |= PlaybackState.ACTION_SKIP_TO_NEXT;
                if (canSkipPrevious)
                    actions |= PlaybackState.ACTION_SKIP_TO_PREVIOUS;
                if (seekable && duration > 0)
                    actions |= PlaybackState.ACTION_SEEK_TO;
            }
            int state = playbackStatus == STATUS_PLAYING ? PlaybackState.STATE_PLAYING
                    : playbackStatus == STATUS_BUFFERING ? PlaybackState.STATE_BUFFERING
                    : playbackStatus == STATUS_PAUSED ? PlaybackState.STATE_PAUSED
                    : playbackStatus == STATUS_ERROR ? PlaybackState.STATE_ERROR
                    : PlaybackState.STATE_NONE;
            PlaybackState.Builder playback = new PlaybackState.Builder()
                    .setActions(actions)
                    .setState(state, position, state == PlaybackState.STATE_PLAYING ? 1.0f : 0.0f);
            if (state == PlaybackState.STATE_ERROR)
                playback.setErrorMessage("音频暂时无法播放，请重试");
            mediaSession.setPlaybackState(playback.build());
            statePublished = true;
            publishedStatus = playbackStatus;
            publishedSeekable = seekable;
            publishedCanNext = canSkipNext;
            publishedCanPrevious = canSkipPrevious;
            publishedPosition = position;
            publishedAt = now;
        }
        boolean metadataChanged = metadataDirty;
        if (metadataChanged) {
            MediaMetadata.Builder metadata = new MediaMetadata.Builder()
                    .putString(MediaMetadata.METADATA_KEY_TITLE, title)
                    .putString(MediaMetadata.METADATA_KEY_ARTIST, artist);
            if (duration > 0)
                metadata.putLong(MediaMetadata.METADATA_KEY_DURATION, duration);
            if (artwork != null) {
                metadata.putBitmap(MediaMetadata.METADATA_KEY_ART, artwork);
                metadata.putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, artwork);
                metadata.putBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON, artwork);
            }
            mediaSession.setMetadata(metadata.build());
            metadataDirty = false;
        }
        if (hasTrack && foreground && (controlsChanged || metadataChanged))
            getSystemService(NotificationManager.class).notify(NOTIFICATION_ID,
                    createNotification());
    }

    private void enterForeground() {
        if (foreground)
            return;
        Notification notification = createNotification();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        else
            startForeground(NOTIFICATION_ID, notification);
        foreground = true;
    }

    private void updatePlaybackResources(boolean wasDesired) {
        if (!desiredPlaying) {
            pausePlaybackResources(!resumeOnFocusGain);
            return;
        }
        acquireWakeLock();
        if (!wasDesired) {
            focusDenialPending = false;
            resumeOnFocusGain = false;
        }
        if (resumeOnFocusGain || focusDenialPending)
            return;
        if (!requestAudioFocus()) {
            focusDenialPending = true;
            nativeInterruptionBegan(false);
            pausePlaybackResources(true);
        }
    }

    private void cancelFocusResume() {
        resumeOnFocusGain = false;
    }

    private boolean prepareUserPlayback() {
        cancelFocusResume();
        if (requestAudioFocus())
            return true;
        focusDenialPending = true;
        return false;
    }

    private void pausePlaybackResources(boolean abandonFocus) {
        releaseWakeLock();
        if (abandonFocus)
            abandonAudioFocus();
    }

    private void leaveForeground(boolean abandonFocus, boolean removeNotification) {
        pausePlaybackResources(abandonFocus);
        if (foreground) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N)
                stopForeground(removeNotification ? STOP_FOREGROUND_REMOVE
                                                  : STOP_FOREGROUND_DETACH);
            else
                stopForeground(removeNotification);
            foreground = false;
        }
    }

    private boolean requestAudioFocus() {
        if (hasAudioFocus)
            return true;
        int result;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            if (focusRequest == null) {
                AudioAttributes attributes = new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build();
                focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                        .setAudioAttributes(attributes)
                        .setOnAudioFocusChangeListener(focusListener, handler)
                        .build();
            }
            result = audioManager.requestAudioFocus(focusRequest);
        } else {
            result = audioManager.requestAudioFocus(focusListener, AudioManager.STREAM_MUSIC,
                    AudioManager.AUDIOFOCUS_GAIN);
        }
        hasAudioFocus = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        return hasAudioFocus;
    }

    private void abandonAudioFocus() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && focusRequest != null)
            audioManager.abandonAudioFocusRequest(focusRequest);
        else
            audioManager.abandonAudioFocus(focusListener);
        hasAudioFocus = false;
        resumeOnFocusGain = false;
    }

    private final AudioManager.OnAudioFocusChangeListener focusListener = focusChange -> {
        if (focusChange == AudioManager.AUDIOFOCUS_GAIN) {
            hasAudioFocus = true;
            if (resumeOnFocusGain) {
                resumeOnFocusGain = false;
                nativeInterruptionEnded(true);
            }
        } else if (focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT ||
                focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) {
            resumeOnFocusGain = resumeOnFocusGain || desiredPlaying;
            hasAudioFocus = false;
            pausePlaybackResources(false);
            if (desiredPlaying)
                nativeInterruptionBegan(true);
        } else if (focusChange == AudioManager.AUDIOFOCUS_LOSS) {
            resumeOnFocusGain = false;
            pausePlaybackResources(true);
            nativeInterruptionBegan(false);
        }
    };

    private final BroadcastReceiver noisyReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction())) {
                pausePlaybackResources(true);
                nativeOutputDisconnected();
            }
        }
    };

    private Notification createNotification() {
        Intent launchIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent contentIntent = launchIntent == null ? null : PendingIntent.getActivity(
                this, 0, launchIntent, PendingIntent.FLAG_UPDATE_CURRENT |
                        PendingIntent.FLAG_IMMUTABLE);
        int icon = getResources().getIdentifier("ic_stat_music", "drawable", getPackageName());
        if (icon == 0)
            icon = android.R.drawable.ic_media_play;
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        builder.setSmallIcon(icon)
                .setContentTitle(title.isEmpty() ? "音乐" : title)
                .setContentText(artist)
                .setCategory(Notification.CATEGORY_TRANSPORT)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setOnlyAlertOnce(true)
                .setOngoing(desiredPlaying);
        int[] compactActions = new int[1 + (canSkipPrevious ? 1 : 0) +
                (canSkipNext ? 1 : 0)];
        int actionIndex = 0;
        if (canSkipPrevious) {
            builder.addAction(android.R.drawable.ic_media_previous, "上一首",
                    serviceAction(ACTION_PREVIOUS, 1));
            compactActions[actionIndex] = actionIndex;
            ++actionIndex;
        }
        builder.addAction(desiredPlaying ? android.R.drawable.ic_media_pause
                                      : android.R.drawable.ic_media_play,
                desiredPlaying ? "暂停" : "播放", serviceAction(ACTION_TOGGLE, 2));
        compactActions[actionIndex] = actionIndex;
        ++actionIndex;
        if (canSkipNext) {
            builder.addAction(android.R.drawable.ic_media_next, "下一首",
                    serviceAction(ACTION_NEXT, 3));
            compactActions[actionIndex] = actionIndex;
            ++actionIndex;
        }
        builder.setStyle(new Notification.MediaStyle()
                .setMediaSession(mediaSession.getSessionToken())
                .setShowActionsInCompactView(compactActions));
        if (artwork != null)
            builder.setLargeIcon(artwork);
        if (contentIntent != null)
            builder.setContentIntent(contentIntent);
        return builder.build();
    }

    private PendingIntent serviceAction(String action, int requestCode) {
        Intent intent = new Intent(this, PlaybackService.class).setAction(action);
        return PendingIntent.getService(this, requestCode, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O)
            return;
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "音乐播放", NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("播放控制");
        getSystemService(NotificationManager.class).createNotificationChannel(channel);
    }

    private void acquireWakeLock() {
        if (wakeLock != null && wakeLock.isHeld())
            return;
        PowerManager manager = (PowerManager)getSystemService(POWER_SERVICE);
        wakeLock = manager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,
                "MusicClient:Playback");
        wakeLock.acquire();
    }

    private void releaseWakeLock() {
        if (wakeLock != null && wakeLock.isHeld())
            wakeLock.release();
        wakeLock = null;
    }

    @Override
    public void onDestroy() {
        if (instance == this)
            instance = null;
        unregisterReceiver(noisyReceiver);
        releaseWakeLock();
        abandonAudioFocus();
        if (mediaSession != null) {
            mediaSession.setActive(false);
            mediaSession.release();
            mediaSession = null;
        }
        leaveForeground(false, true);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
