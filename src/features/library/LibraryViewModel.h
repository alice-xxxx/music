#pragma once
#include <QObject>
#include <QVariantList>
#include "domain/Track.h"
class KuGouApi;
class CatalogService;
class LibraryViewModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY changed)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(QVariantMap source READ source NOTIFY changed)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    Q_PROPERTY(bool actionBusy READ actionBusy NOTIFY actionChanged)
    Q_PROPERTY(bool actionUncertain READ actionUncertain NOTIFY actionChanged)
    Q_PROPERTY(QString actionMessage READ actionMessage NOTIFY actionChanged)
    Q_PROPERTY(QString actionKind READ actionKind NOTIFY actionChanged)
    Q_PROPERTY(bool actionSucceeded READ actionSucceeded NOTIFY actionChanged)
  public:
    explicit LibraryViewModel(KuGouApi *api, CatalogService *catalog,
                              QObject *parent = nullptr);
    QVariantList playlists() const
    {
        return m_playlists;
    }
    QVariantList tracks() const
    {
        return m_tracks;
    }
    QString title() const
    {
        return m_title;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    bool loading() const
    {
        return m_loading;
    }
    Q_INVOKABLE void load();
    bool hasMore() const
    {
        return m_title.isEmpty() ? m_morePlaylists : m_moreTracks;
    }
    QVariantMap source() const;
    QString coverUrl() const
    {
        return m_selected.value("cover").toString();
    }
    QString description() const
    {
        return m_selected.value("description").toString();
    }
    bool actionBusy() const
    {
        return m_actionBusy;
    }
    bool actionUncertain() const
    {
        return m_actionUncertain;
    }
    QString actionMessage() const
    {
        return m_actionMessage;
    }
    QString actionKind() const { return m_actionKind; }
    bool actionSucceeded() const { return m_actionSucceeded; }
    Q_INVOKABLE void loadMorePlaylists()
    {
        if (!m_loading && m_morePlaylists)
            requestPlaylists(m_playlistPage + 1);
    }
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void createPlaylist(const QString &name);
    Q_INVOKABLE void deleteSelectedPlaylist();
    Q_INVOKABLE void updateSelectedPlaylist(const QString &name, const QString &description);
    Q_INVOKABLE void addTrack(int playlistIndex, const QVariantMap &track);
    Q_INVOKABLE void removeTrack(int trackIndex);
    Q_INVOKABLE void confirmAction();
    Q_INVOKABLE void openPlaylist(int index);
    Q_INVOKABLE void closePlaylist();
  signals:
    void changed();
    void actionChanged();

  private:
    KuGouApi *m_api;
    CatalogService *m_catalog;
    QVariantList m_playlists, m_tracks;
    QString m_title, m_error;
    bool m_loading = false;
    quint64 m_generation = 0;
    QVariantMap m_selected;
    int m_playlistPage = 0, m_trackPage = 0;
    bool m_morePlaylists = false, m_moreTracks = false;
    void requestPlaylists(int page);
    void requestTracks(int page);
    void invalidateSession();
    void finishWrite(QString code, QString message);
    void confirmPage(int page);
    void completeAction(bool confirmed);
    void writeAddedTrack();
    Track m_addedTrack;
    bool m_actionBusy = false, m_actionUncertain = false;
    bool m_actionSucceeded = false;
    QString m_actionMessage, m_actionKind, m_actionTarget, m_actionName, m_actionDescription,
        m_actionTrackKey;
    QString m_actionFileId;
    quint64 m_actionGeneration = 0;
};
