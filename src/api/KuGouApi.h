#pragma once
#include "ApiClient.h"
#include "domain/Track.h"
#include <functional>
#include <memory>

class KuGouApi final : public QObject
{
    Q_OBJECT
  public:
    using SearchCallback = std::function<void(QList<Track>, QString, QString)>;
    struct Playlist
    {
        QString globalCollectionId;
        QString listId;
        QString title;
        QString cover;
        int trackCount = 0;
    };
    explicit KuGouApi(QUrl serviceBase, QObject *parent = nullptr);
    void setServiceBase(QUrl serviceBase);
    bool serviceConfigured() const
    {
        return !m_serviceBase.host().isEmpty();
    }
    QString scopeKey() const;
    QString storageError() const;
    void search(const QString &keywords, SearchCallback callback, int page = 1);
    void cancelSearch()
    {
        m_client.cancelRequests(QStringLiteral("/search"));
    }
    void trackMetadata(Track track, std::function<void(Track, QString)> callback);
    void albumTracks(const QString &albumId, SearchCallback callback, int page = 1);
    void artistTracks(const QString &artistId, SearchCallback callback, int page = 1);
    void artistDetail(const QString &artistId, std::function<void(QVariantMap, QString)> callback);
    void searchEntries(const QString &keywords, const QString &kind,
                       std::function<void(QVariantList, QString, QString)> callback, int page = 1);
    void resolveSong(const QString &hash, const QString &albumAudioId,
                     std::function<void(QUrl, QString)> callback,
                     const QString &quality = QStringLiteral("128"));
    void lyrics(const QString &hash, std::function<void(QString, QString)> callback);
    void userPlaylists(std::function<void(QList<Playlist>, QString, QString)> callback,
                       int page = 1);
    void playlistTracks(const QString &globalCollectionId, const QString &listId,
                        SearchCallback callback, int page = 1);
    using WriteCallback = std::function<void(QString, QString)>;
    void createPlaylist(const QString &name, WriteCallback callback);
    void deletePlaylist(const QString &listId, WriteCallback callback);
    void addPlaylistTrack(const QString &listId, const Track &track, WriteCallback callback);
    void removePlaylistTrack(const QString &listId, const QString &fileId, WriteCallback callback);
    void createLoginQr(std::function<void(QString, QString, QString)> callback);
    void checkLoginQr(const QString &key, std::function<void(int, QString)> callback);
    bool restoreAuthenticatedSession();
    bool beginAuthenticatedSession();
    bool loginWithCookies(const QString &cookieHeader, QString *error = nullptr);
    void cancelLogin();
    void logout();
  signals:
    void sessionInvalidated();

  private:
    enum class RegistrationState
    {
        Unregistered,
        Registering,
        Registered,
        Failed
    };
    void resolveSongAfterRegistration(const QString &hash, const QString &albumAudioId,
                                      std::function<void(QUrl, QString)> callback,
                                      const QString &quality);
    ApiClient m_client;
    QUrl m_serviceBase;
    RegistrationState m_registrationState = RegistrationState::Unregistered;
    struct PendingResolve
    {
        QString hash;
        QString albumAudioId;
        std::function<void(QUrl, QString)> callback;
        QString quality;
    };
    QList<PendingResolve> m_pendingResolves;
    bool m_authenticated = false;
    std::unique_ptr<ApiClient> m_loginClient;
};
