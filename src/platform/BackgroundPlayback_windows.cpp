#include "BackgroundPlayback.h"
#include <QGuiApplication>
#include <QMetaObject>
#include <QPointer>
#include <QWindow>
#include <chrono>
#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>

namespace
{
using namespace winrt::Windows::Media;

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

class WindowsMediaState final
{
  public:
    explicit WindowsMediaState(BackgroundPlayback *owner) : m_owner(owner)
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            m_apartmentInitialized = true;
        }
        catch (const winrt::hresult_error &error)
        {
            if (error.code() != RPC_E_CHANGED_MODE)
                qWarning("Could not initialize Windows Runtime media controls: 0x%08lx",
                         static_cast<unsigned long>(error.code().value));
        }
    }

    ~WindowsMediaState()
    {
        if (m_controls)
        {
            if (m_buttonToken.value)
                m_controls.ButtonPressed(m_buttonToken);
            if (m_positionToken.value)
                m_controls.PlaybackPositionChangeRequested(m_positionToken);
            m_controls.IsEnabled(false);
        }
        if (m_apartmentInitialized)
            winrt::uninit_apartment();
    }

    void update(bool hasTrack, bool playing, qint64 position, qint64 duration,
                const QString &title, const QString &artist)
    {
        if (!ensureInitialized())
            return;
        try
        {
            m_controls.IsEnabled(hasTrack);
            m_controls.IsPlayEnabled(hasTrack);
            m_controls.IsPauseEnabled(hasTrack);
            m_controls.IsStopEnabled(hasTrack);
            m_controls.IsNextEnabled(hasTrack);
            m_controls.IsPreviousEnabled(hasTrack);
            m_controls.PlaybackStatus(!hasTrack ? MediaPlaybackStatus::Stopped
                                      : playing ? MediaPlaybackStatus::Playing
                                                : MediaPlaybackStatus::Paused);
            if (!hasTrack)
            {
                m_controls.DisplayUpdater().ClearAll();
                return;
            }

            auto updater = m_controls.DisplayUpdater();
            updater.Type(MediaPlaybackType::Music);
            auto properties = updater.MusicProperties();
            properties.Title(title.toStdWString());
            properties.Artist(artist.toStdWString());
            updater.Update();

            SystemMediaTransportControlsTimelineProperties timeline;
            const auto toTimeSpan = [](qint64 milliseconds)
            {
                return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
                    std::chrono::milliseconds(qMax<qint64>(0, milliseconds)));
            };
            timeline.StartTime(toTimeSpan(0));
            timeline.MinSeekTime(toTimeSpan(0));
            timeline.Position(toTimeSpan(position));
            timeline.MaxSeekTime(toTimeSpan(duration));
            timeline.EndTime(toTimeSpan(duration));
            m_controls.UpdateTimelineProperties(timeline);
        }
        catch (const winrt::hresult_error &error)
        {
            qWarning("Could not update Windows media controls: 0x%08lx",
                     static_cast<unsigned long>(error.code().value));
        }
    }

    bool handlesCommands() const { return static_cast<bool>(m_controls); }

  private:
    bool ensureInitialized()
    {
        if (m_controls)
            return true;
        QWindow *window = QGuiApplication::focusWindow();
        if (!window)
        {
            const auto windows = QGuiApplication::topLevelWindows();
            if (!windows.isEmpty())
                window = windows.constFirst();
        }
        if (!window)
            return false;

        try
        {
            const auto activationFactory =
                winrt::get_activation_factory<SystemMediaTransportControls>();
            const auto interop = activationFactory.as<ISystemMediaTransportControlsInterop>();
            winrt::check_hresult(interop->GetForWindow(
                reinterpret_cast<HWND>(window->winId()),
                winrt::guid_of<SystemMediaTransportControls>(), winrt::put_abi(m_controls)));
            const QPointer<BackgroundPlayback> ownerGuard(m_owner);
            m_buttonToken = m_controls.ButtonPressed(
                [ownerGuard](const SystemMediaTransportControls &,
                             const SystemMediaTransportControlsButtonPressedEventArgs &event)
                {
                    if (!ownerGuard)
                        return;
                    switch (event.Button())
                    {
                    case SystemMediaTransportControlsButton::Play:
                        dispatch(ownerGuard.data(),
                                 [](BackgroundPlayback *owner) { owner->dispatchPlay(); });
                        break;
                    case SystemMediaTransportControlsButton::Pause:
                    case SystemMediaTransportControlsButton::Stop:
                        dispatch(ownerGuard.data(),
                                 [](BackgroundPlayback *owner) { owner->dispatchPause(); });
                        break;
                    case SystemMediaTransportControlsButton::Next:
                        dispatch(ownerGuard.data(),
                                 [](BackgroundPlayback *owner) { owner->dispatchNext(); });
                        break;
                    case SystemMediaTransportControlsButton::Previous:
                        dispatch(ownerGuard.data(),
                                 [](BackgroundPlayback *owner) { owner->dispatchPrevious(); });
                        break;
                    default:
                        break;
                    }
                });
            m_positionToken = m_controls.PlaybackPositionChangeRequested(
                [ownerGuard](const SystemMediaTransportControls &,
                             const PlaybackPositionChangeRequestedEventArgs &event)
                {
                    if (!ownerGuard)
                        return;
                    const auto position = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              event.RequestedPlaybackPosition())
                                              .count();
                    dispatch(ownerGuard.data(), [position](BackgroundPlayback *owner)
                             { owner->dispatchSeek(position); });
                });
            return true;
        }
        catch (const winrt::hresult_error &error)
        {
            qWarning("Could not create Windows media controls: 0x%08lx",
                     static_cast<unsigned long>(error.code().value));
            return false;
        }
    }

    BackgroundPlayback *m_owner;
    SystemMediaTransportControls m_controls{nullptr};
    winrt::event_token m_buttonToken{};
    winrt::event_token m_positionToken{};
    bool m_apartmentInitialized = false;
};
} // namespace

void *createWindowsMediaIntegration(BackgroundPlayback *owner)
{
    return new WindowsMediaState(owner);
}

void destroyWindowsMediaIntegration(void *opaque)
{
    delete static_cast<WindowsMediaState *>(opaque);
}

bool windowsMediaIntegrationHandlesCommands(void *opaque)
{
    const auto *state = static_cast<const WindowsMediaState *>(opaque);
    return state && state->handlesCommands();
}

void updateWindowsMediaIntegration(void *opaque, bool hasTrack, bool playing, qint64 position,
                                   qint64 duration, const QString &title, const QString &artist)
{
    auto *state = static_cast<WindowsMediaState *>(opaque);
    if (state)
        state->update(hasTrack, playing, position, duration, title, artist);
}
