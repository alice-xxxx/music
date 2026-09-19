#pragma once
#include "ApiClient.h"
#include "domain/Track.h"
#include <QStringList>
#include <QVariantMap>
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
        QString description;
        QString creatorUserId;
        QString creatorListId;
        int trackCount = 0;
        qint64 totalVersion = 0;
        int type = 0;
        int sort = 0;
    };
    explicit KuGouApi(QUrl serviceBase, QObject *parent = nullptr);
    void setServiceBase(QUrl serviceBase);
    bool serviceConfigured() const
    {
        return !m_serviceBase.host().isEmpty();
    }
    QString scopeKey() const;
    QString storageError() const;
    bool authenticated() const
    {
        return m_authenticated;
    }
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
    void hotSearches(std::function<void(QStringList, QString)> callback);
    void searchSuggestions(const QString &keywords,
                           std::function<void(QStringList, QString)> callback);
    void dailyRecommendations(SearchCallback callback);
    void rankEntries(std::function<void(QVariantList, QString)> callback);
    void rankTracks(const QString &rankId, SearchCallback callback, int page = 1);
    void popularPlaylists(std::function<void(QVariantList, QString)> callback, int page = 1);
    void comments(const QString &kind, const QString &id,
                  std::function<void(QVariantList, QString, QString)> callback, int page = 1);
    void sendComment(const QString &kind, const QString &id, const QString &name,
                     const QString &content, std::function<void(QString, QString)> callback);
    void resolveSong(const QString &hash, const QString &albumAudioId,
                     std::function<void(QUrl, QString)> callback,
                     const QString &quality = QStringLiteral("128"));
    void lyrics(const QString &hash, std::function<void(QString, QString)> callback);
    void userPlaylists(std::function<void(QList<Playlist>, QString, QString)> callback,
                       int page = 1);
    void playlistTracks(const QString &globalCollectionId, const QString &listId,
                        SearchCallback callback, int page = 1);
    void playlistDetail(const QString &globalCollectionId,
                        std::function<void(QVariantMap, QString)> callback);
    using WriteCallback = std::function<void(QString, QString)>;
    void createPlaylist(const QString &name, WriteCallback callback);
    void favoritePlaylist(const QVariantMap &playlist, WriteCallback callback);
    void deletePlaylist(const QString &listId, WriteCallback callback);
    void updatePlaylist(const QString &listId, qint64 totalVersion, int type,
                        const QString &name, const QString &description,
                        WriteCallback callback);
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
    bool m_userAuthAttempted = false;
    std::unique_ptr<ApiClient> m_loginClient;
};
