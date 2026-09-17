#include "WindowsMediaCommands.h"
#include "features/playback/PlaybackController.h"
#include <windows.h>

bool WindowsMediaCommands::nativeEventFilter(const QByteArray &eventType, void *message,
                                             qintptr *result)
{
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const auto *native = static_cast<MSG *>(message);
    if (native->message != WM_APPCOMMAND)
        return false;
    switch (GET_APPCOMMAND_LPARAM(native->lParam))
    {
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
        m_player->togglePlayback();
        break;
    case APPCOMMAND_MEDIA_NEXTTRACK:
        m_player->next();
        break;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
        m_player->previous();
        break;
    case APPCOMMAND_MEDIA_PLAY:
        if (!m_player->desiredPlaying())
            m_player->togglePlayback();
        break;
    case APPCOMMAND_MEDIA_PAUSE:
        if (m_player->desiredPlaying())
            m_player->togglePlayback();
        break;
    default:
        return false;
    }
    *result = 1;
    return true;
}
