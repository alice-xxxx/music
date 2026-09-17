#pragma once

#include <QAbstractNativeEventFilter>
class PlaybackController;

// Handles commands delivered to this application's Windows message queue.
class WindowsMediaCommands final : public QAbstractNativeEventFilter
{
  public:
    explicit WindowsMediaCommands(PlaybackController *player) : m_player(player) {}
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

  private:
    PlaybackController *m_player;
};
