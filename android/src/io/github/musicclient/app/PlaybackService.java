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
import android.util.Log;

public final class PlaybackService extends Service {
    private static final String TAG = "MusicPlayback";
    private static final String CHANNEL_ID = "music_playback";
    private static final int NOTIFICATION_ID = 1;
    private static final String ACTION_START = "io.github.musicclient.app.action.START";
    private static final String ACTION_TOGGLE = "io.github.musicclient.app.action.TOGGLE";
    private static final String ACTION_NEXT = "io.github.musicclient.app.action.NEXT";
    private static final String ACTION_PREVIOUS = "io.github.musicclient.app.action.PREVIOUS";
    private static final String EXTRA_HAS_TRACK = "hasTrack";
    private static final String EXTRA_DESIRED = "desired";
    private static final String EXTRA_PLAYING = "playing";
    private static final String EXTRA_POSITION = "position";
    private static final String EXTRA_DURATION = "duration";
    private static final String EXTRA_TITLE = "title";
    private static final String EXTRA_ARTIST = "artist";

    private static volatile PlaybackService instance;
    private static volatile byte[] pendingArtwork;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private MediaSession mediaSession;
    private AudioManager audioManager;
    private AudioFocusRequest focusRequest;
    private PowerManager.WakeLock wakeLock;
    private boolean hasAudioFocus;
    private boolean foreground;
    private boolean resumeOnFocusGain;
    private boolean hasTrack;
    private boolean desiredPlaying;
    private boolean playing;
    private long position;
    private long duration;
    private String title = "";
    private String artist = "";
    private Bitmap artwork;

    private static native void nativePlay();
    private static native void nativePause();
    private static native void nativeToggle();
    private static native void nativeNext();
    private static native void nativePrevious();
    private static native void nativeSeek(long position);
    private static native void nativeInterruptionBegan(boolean resumable);
    private static native void nativeInterruptionEnded(boolean shouldResume);
    private static native void nativeOutputDisconnected();

    public static void setPlaybackActive(Context context, boolean active) {
        Context application = context.getApplicationContext();
        PlaybackService service = instance;
        if (!active && service != null) {
            service.handler.post(() -> {
                service.leaveForeground(true, true);
                service.stopSelf();
            });
            return;
        }
        if (!active)
            return;
        Intent intent = new Intent(application, PlaybackService.class).setAction(ACTION_START);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                application.startForegroundService(intent);
            else
                application.startService(intent);
        } catch (RuntimeException error) {
            Log.e(TAG, "Unable to start the playback foreground service", error);
        }
    }

    public static void updateSession(Context context, boolean hasTrack, boolean desired,
            boolean playing, long position, long duration, String title, String artist) {
        PlaybackService service = instance;
        if (service != null) {
            service.handler.post(() -> service.applyState(hasTrack, desired, playing, position,
                    duration, title, artist));
            return;
        }
        Context application = context.getApplicationContext();
        if (!hasTrack) {
            application.stopService(new Intent(application, PlaybackService.class));
            return;
        }
        Intent intent = stateIntent(application, hasTrack, desired, playing, position, duration,
                title, artist);
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

    private static Intent stateIntent(Context context, boolean hasTrack, boolean desired,
            boolean playing, long position, long duration, String title, String artist) {
        return new Intent(context, PlaybackService.class)
                .putExtra(EXTRA_HAS_TRACK, hasTrack)
                .putExtra(EXTRA_DESIRED, desired)
                .putExtra(EXTRA_PLAYING, playing)
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
            @Override public void onPlay() { nativePlay(); }
            @Override public void onPause() { nativePause(); }
            @Override public void onStop() { nativePause(); }
            @Override public void onSkipToNext() { nativeNext(); }
            @Override public void onSkipToPrevious() { nativePrevious(); }
            @Override public void onSeekTo(long nextPosition) { nativeSeek(nextPosition); }
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
        if (ACTION_TOGGLE.equals(action))
            nativeToggle();
        else if (ACTION_NEXT.equals(action))
            nativeNext();
        else if (ACTION_PREVIOUS.equals(action))
            nativePrevious();

        if (intent != null && intent.hasExtra(EXTRA_HAS_TRACK))
            applyState(intent.getBooleanExtra(EXTRA_HAS_TRACK, false),
                    intent.getBooleanExtra(EXTRA_DESIRED, false),
                    intent.getBooleanExtra(EXTRA_PLAYING, false),
                    intent.getLongExtra(EXTRA_POSITION, 0),
                    intent.getLongExtra(EXTRA_DURATION, 0),
                    intent.getStringExtra(EXTRA_TITLE), intent.getStringExtra(EXTRA_ARTIST));
        if (ACTION_START.equals(action) || hasTrack)
            enterForeground();
        return START_NOT_STICKY;
    }

    private void applyState(boolean hasTrack, boolean desired, boolean playing, long position,
            long duration, String title, String artist) {
        this.hasTrack = hasTrack;
        this.desiredPlaying = desired;
        this.playing = playing;
        this.position = Math.max(0, position);
        this.duration = Math.max(0, duration);
        this.title = title == null ? "" : title;
        this.artist = artist == null ? "" : artist;
        updateMediaSession();
        if (hasTrack)
            enterForeground();
        else
            leaveForeground(true, true);
        if (!hasTrack)
            stopSelf();
    }

    private void applyArtwork(byte[] encoded) {
        artwork = encoded == null || encoded.length == 0 ? null
                : BitmapFactory.decodeByteArray(encoded, 0, encoded.length);
        updateMediaSession();
    }

    private void updateMediaSession() {
        if (mediaSession == null)
            return;
        long actions = PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE |
                PlaybackState.ACTION_PLAY_PAUSE | PlaybackState.ACTION_SKIP_TO_NEXT |
                PlaybackState.ACTION_SKIP_TO_PREVIOUS | PlaybackState.ACTION_SEEK_TO;
        int state = !hasTrack ? PlaybackState.STATE_NONE
                : playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED;
        mediaSession.setPlaybackState(new PlaybackState.Builder()
                .setActions(actions)
                .setState(state, position, playing ? 1.0f : 0.0f)
                .build());
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
        if (hasTrack && foreground)
            getSystemService(NotificationManager.class).notify(NOTIFICATION_ID,
                    createNotification());
    }

    private void enterForeground() {
        Notification notification = createNotification();
        if (!foreground) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
                startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
            else
                startForeground(NOTIFICATION_ID, notification);
            foreground = true;
        }
        if (!desiredPlaying) {
            pausePlaybackResources(!resumeOnFocusGain);
            return;
        }
        acquireWakeLock();
        if (!requestAudioFocus()) {
            nativeInterruptionBegan(false);
            pausePlaybackResources(true);
        }
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
            resumeOnFocusGain = desiredPlaying;
            hasAudioFocus = false;
            pausePlaybackResources(false);
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
        int icon = getApplicationInfo().icon;
        if (icon == 0)
            icon = android.R.drawable.ic_media_play;
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        builder.setSmallIcon(icon)
                .setContentTitle(title.isEmpty() ? "Music" : title)
                .setContentText(artist.isEmpty() ? "Playing audio" : artist)
                .setCategory(Notification.CATEGORY_TRANSPORT)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setOnlyAlertOnce(true)
                .setOngoing(playing)
                .setStyle(new Notification.MediaStyle()
                        .setMediaSession(mediaSession.getSessionToken())
                        .setShowActionsInCompactView(0, 1, 2))
                .addAction(android.R.drawable.ic_media_previous, "Previous",
                        serviceAction(ACTION_PREVIOUS, 1))
                .addAction(playing ? android.R.drawable.ic_media_pause
                                   : android.R.drawable.ic_media_play,
                        playing ? "Pause" : "Play", serviceAction(ACTION_TOGGLE, 2))
                .addAction(android.R.drawable.ic_media_next, "Next",
                        serviceAction(ACTION_NEXT, 3));
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
                CHANNEL_ID, "Music playback", NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Music playback controls");
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
