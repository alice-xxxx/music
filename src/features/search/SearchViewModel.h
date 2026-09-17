#pragma once

#include <QAbstractListModel>
#include "domain/Track.h"
class KuGouApi;
class CatalogService;

class SearchViewModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY statusChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY statusChanged)
    Q_PROPERTY(QString pageError READ pageError NOTIFY statusChanged)
    Q_PROPERTY(QString resultsQuery READ resultsQuery NOTIFY statusChanged)
    Q_PROPERTY(QStringList recentQueries READ recentQueries NOTIFY statusChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
    Q_PROPERTY(QString diagnosticId READ diagnosticId NOTIFY statusChanged)
    Q_PROPERTY(bool authRequired READ authRequired NOTIFY statusChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QString resultsCategory READ resultsCategory NOTIFY statusChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY statusChanged)

  public:
    enum Status
    {
        Idle,
        Loading,
        Ready,
        Empty,
        Error
    };
    Q_ENUM(Status)
    enum Role
    {
        TrackKeyRole = Qt::UserRole + 1,
        TitleRole,
        ArtistTextRole,
        CoverUrlRole,
        AlbumRole,
        TrackDataRole,
        DurationTextRole
    };

    explicit SearchViewModel(KuGouApi *api, CatalogService *catalog,
                             QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString query() const;
    QString category() const
    {
        return m_category;
    }
    QString resultsCategory() const
    {
        return m_resultsCategory;
    }
    QVariantList entries() const
    {
        return m_entries;
    }
    void setCategory(const QString &category);
    void setQuery(const QString &query);
    Status status() const;
    QString errorMessage() const;
    QString diagnosticId() const;
    bool authRequired() const
    {
        return m_errorCode == QStringLiteral("AuthRequired");
    }
    bool loadingMore() const
    {
        return m_loadingMore;
    }
    bool hasMore() const
    {
        return m_hasMore;
    }
    QString pageError() const
    {
        return m_pageError;
    }
    QString resultsQuery() const
    {
        return m_resultsQuery;
    }
    QStringList recentQueries() const
    {
        return m_recentQueries;
    }
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void submitSearch();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void clearRecentQueries();

  signals:
    void queryChanged();
    void categoryChanged();
    void entriesAboutToChange(bool append);
    void statusChanged();

  private:
    void requestPage(bool more);
    void requestEntries(const QString &query, int page, bool more, quint64 generation);
    void rememberQuery(const QString &query);
    void invalidateSession();
    QString m_category = QStringLiteral("song"), m_resultsCategory = QStringLiteral("song");
    QVariantList m_entries;
    bool m_loadingMore = false;
    bool m_hasMore = false;
    int m_page = 0;
    QString m_pageError;
    QString m_resultsQuery;
    QStringList m_recentQueries;
    void setStatus(Status status, QString message = {}, QString diagnosticId = {});
    QString m_query;
    QList<Track> m_tracks;
    Status m_status = Idle;
    QString m_errorMessage;
    QString m_diagnosticId;
    QString m_errorCode;
    quint64 m_generation = 0;
    KuGouApi *m_api;
    CatalogService *m_catalog;
};
