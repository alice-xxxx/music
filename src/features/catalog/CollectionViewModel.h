#pragma once
#include <QObject>
#include <QVariantList>
class KuGouApi;
class CatalogService;
class CollectionViewModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(QVariantMap source READ source NOTIFY changed)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    Q_PROPERTY(qreal scrollPosition READ scrollPosition NOTIFY changed)
    Q_PROPERTY(bool favoriteBusy READ favoriteBusy NOTIFY changed)
    Q_PROPERTY(bool favorited READ favorited NOTIFY changed)
    Q_PROPERTY(bool canUnfavorite READ canUnfavorite NOTIFY changed)
    Q_PROPERTY(QString favoriteMessage READ favoriteMessage NOTIFY changed)
  public:
    CollectionViewModel(KuGouApi *api, CatalogService *catalog, QObject *parent = nullptr);
    QVariantList tracks() const
    {
        return m_tracks;
    }
    QString title() const
    {
        return m_title;
    }
    QString coverUrl() const
    {
        return m_cover;
    }
    QString errorMessage() const
    {
        return m_error.isEmpty() ? m_detailError : m_error;
    }
    bool loading() const
    {
        return m_loading;
    }
    bool detailLoading() const
    {
        return m_detailLoading;
    }
    bool hasMore() const
    {
        return m_hasMore;
    }
    QVariantMap source() const;
    QString kind() const
    {
        return m_kind;
    }
    QString description() const
    {
        return m_description;
    }
    Q_INVOKABLE void open(const QVariantMap &track);
    Q_INVOKABLE void openArtist(const QVariantMap &artist);
    Q_INVOKABLE void openPlaylist(const QVariantMap &playlist);
    Q_INVOKABLE void openRank(const QVariantMap &rank);
    Q_INVOKABLE void openNestedAlbum(const QVariantMap &track, qreal scrollPosition);
    Q_INVOKABLE bool goBack();
    Q_INVOKABLE QVariantMap captureState() const;
    Q_INVOKABLE void restoreState(const QVariantMap &state);
    Q_INVOKABLE void saveScrollPosition(qreal position)
    {
        m_scrollPosition = position;
    }
    qreal scrollPosition() const
    {
        return m_scrollPosition;
    }
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void toggleFavorite();
    bool favoriteBusy() const { return m_favoriteBusy; }
    bool favorited() const { return m_favorited; }
    bool canUnfavorite() const { return m_canUnfavorite; }
    QString favoriteMessage() const { return m_favoriteMessage; }
  signals:
    void changed();
    void opened();
    void scopeReset();
    void loginRequired();

  private:
    void request(int page);
    void openCollection(const QString &kind, const QString &id, const QString &title,
                        const QString &cover, const QVariantMap &metadata = {});
    void requestArtistDetail();
    void requestPlaylistDetail();
    void refreshFavoriteState(int page = 1);
    KuGouApi *m_api;
    CatalogService *m_catalog;
    QString m_id, m_title, m_cover, m_error, m_detailError;
    QString m_kind = QStringLiteral("album"), m_description;
    QVariantMap m_metadata;
    QVariantList m_backStack;
    qreal m_scrollPosition = 0;
    QVariantList m_tracks;
    int m_page = 0;
    bool m_loading = false;
    bool m_detailLoading = false;
    bool m_hasMore = false;
    bool m_favoriteBusy = false;
    bool m_favorited = false;
    bool m_canUnfavorite = false;
    QString m_favoriteListId, m_favoriteMessage;
    quint64 m_generation = 0;
};
