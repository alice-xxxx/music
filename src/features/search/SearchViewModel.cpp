#include "SearchViewModel.h"
#include "api/KuGouApi.h"
#include "features/catalog/CatalogService.h"
#include <QPointer>
#include <QSettings>
#include <QSet>

SearchViewModel::SearchViewModel(KuGouApi *api, CatalogService *catalog, QObject *parent)
    : QAbstractListModel(parent), m_api(api), m_catalog(catalog)
{
    m_recentQueries = QSettings().value("search/recent").toStringList();
    connect(m_catalog, &CatalogService::trackUpdated, this,
            [this](const Track &track)
            {
                for (int row = 0; row < m_tracks.size(); ++row)
                    if (m_tracks.at(row).key == track.key)
                    {
                        m_tracks[row] = track;
                        emit dataChanged(index(row), index(row),
                                         {CoverUrlRole, AlbumRole, ArtistTextRole, TrackDataRole});
                    }
            });
    connect(m_api, &KuGouApi::sessionInvalidated, this, &SearchViewModel::invalidateSession);
    loadHotSearches();
}
int SearchViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tracks.size();
}
QVariant SearchViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size())
        return {};
    const auto &track = m_tracks.at(index.row());
    switch (role)
    {
    case TrackKeyRole:
        return track.key;
    case TitleRole:
        return track.title;
    case ArtistTextRole:
        return track.artist;
    case DurationTextRole:
        return track.durationText();
    case CoverUrlRole:
        return track.coverUrl;
    case AlbumRole:
        return track.album;
    case TrackDataRole:
        return track.toMap();
    default:
        return {};
    }
}
QHash<int, QByteArray> SearchViewModel::roleNames() const
{
    return {{TrackKeyRole, "trackKey"},     {TitleRole, "title"},
            {ArtistTextRole, "artistText"}, {DurationTextRole, "durationText"},
            {CoverUrlRole, "coverUrl"},     {AlbumRole, "albumText"},
            {TrackDataRole, "trackData"}};
}
QString SearchViewModel::query() const
{
    return m_query;
}
void SearchViewModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_api->cancelSearch();
    ++m_generation;
    m_loadingMore = false;
    m_query = query;
    emit queryChanged();
    ++m_suggestionGeneration;
    m_suggestionsLoading = false;
    m_suggestions.clear();
    emit suggestionsChanged();
}
void SearchViewModel::setCategory(const QString &category)
{
    if (m_category == category ||
        !QStringList{"song", "album", "artist", "playlist"}.contains(category))
        return;
    m_category = category;
    emit categoryChanged();
    submitSearch();
}
SearchViewModel::Status SearchViewModel::status() const
{
    return m_status;
}
QString SearchViewModel::errorMessage() const
{
    return m_errorMessage;
}
QString SearchViewModel::diagnosticId() const
{
    return m_diagnosticId;
}
void SearchViewModel::submitSearch()
{
    ++m_suggestionGeneration;
    m_suggestionsLoading = false;
    emit suggestionsChanged();
    requestPage(false);
}
void SearchViewModel::requestSuggestions()
{
    const QString normalized = m_query.trimmed();
    const quint64 generation = ++m_suggestionGeneration;
    if (normalized.isEmpty())
    {
        m_suggestionsLoading = false;
        m_suggestions.clear();
        emit suggestionsChanged();
        return;
    }
    m_suggestionsLoading = true;
    emit suggestionsChanged();
    m_api->searchSuggestions(
        normalized,
        [this, guard = QPointer<SearchViewModel>(this), generation,
         normalized](QStringList suggestions, QString)
        {
            if (!guard || generation != m_suggestionGeneration ||
                normalized != m_query.trimmed())
                return;
            m_suggestionsLoading = false;
            m_suggestions = std::move(suggestions);
            emit suggestionsChanged();
        });
}
void SearchViewModel::loadHotSearches()
{
    const quint64 generation = ++m_hotGeneration;
    m_api->hotSearches(
        [this, guard = QPointer<SearchViewModel>(this), generation](QStringList keywords,
                                                                   QString)
        {
            if (!guard || generation != m_hotGeneration)
                return;
            m_hotKeywords = std::move(keywords);
            emit suggestionsChanged();
        });
}
void SearchViewModel::loadMore()
{
    if (m_status != Ready || m_loadingMore || !m_hasMore || m_query.trimmed() != m_resultsQuery)
        return;
    requestPage(true);
}
void SearchViewModel::requestPage(bool more)
{
    m_api->cancelSearch();
    const QString normalized = m_query.trimmed();
    const quint64 generation = ++m_generation;
    if (normalized.isEmpty())
    {
        beginResetModel();
        m_tracks.clear();
        m_entries.clear();
        m_resultsCategory = m_category;
        endResetModel();
        m_resultsQuery.clear();
        m_hasMore = false;
        m_page = 0;
        m_loadingMore = false;
        setStatus(Idle);
        return;
    }
    const int page = more ? m_page + 1 : 1;
    m_pageError.clear();
    m_loadingMore = more;
    if (more)
        emit statusChanged();
    else
        setStatus(Loading);
    if (m_category != QStringLiteral("song"))
    {
        requestEntries(normalized, page, more, generation);
        return;
    }
    m_api->search(
        normalized,
        [this, guard = QPointer<SearchViewModel>(this), normalized, page, more,
         generation](QList<Track> rows, QString code, QString message)
        {
            if (!guard || generation != m_generation)
                return;
            m_loadingMore = false;
            if (!code.isEmpty())
            {
                if (more)
                {
                    m_pageError = message;
                    emit statusChanged();
                }
                else
                {
                    m_errorCode = code;
                    setStatus(Error, message, QStringLiteral("D-SEARCH-") + code);
                }
                return;
            }
            m_hasMore = rows.size() == 30;
            m_page = page;
            m_resultsQuery = normalized;
            m_resultsCategory = QStringLiteral("song");
            QList<Track> additions;
            QSet<QString> keys;
            if (more)
                for (const auto &track : m_tracks)
                    keys.insert(track.key);
            for (const auto &track : rows)
                if (!keys.contains(track.key))
                {
                    keys.insert(track.key);
                    additions.append(m_catalog->remember(track));
                }
            if (more && !additions.isEmpty())
            {
                beginInsertRows({}, m_tracks.size(), m_tracks.size() + additions.size() - 1);
                m_tracks.append(additions);
                endInsertRows();
            }
            else if (!more)
            {
                beginResetModel();
                m_tracks = additions;
                endResetModel();
                rememberQuery(normalized);
            }
            m_errorCode.clear();
            setStatus(m_tracks.isEmpty() ? Empty : Ready);
        },
        page);
}
void SearchViewModel::rememberQuery(const QString &query)
{
    m_recentQueries.removeAll(query);
    m_recentQueries.prepend(query);
    while (m_recentQueries.size() > 12)
        m_recentQueries.removeLast();
    QSettings().setValue("search/recent", m_recentQueries);
}
void SearchViewModel::clearRecentQueries()
{
    if (m_recentQueries.isEmpty())
        return;
    m_recentQueries.clear();
    QSettings().remove(QStringLiteral("search/recent"));
    emit statusChanged();
}
void SearchViewModel::requestEntries(const QString &query, int page, bool more, quint64 generation)
{
    const auto category = m_category;
    m_api->searchEntries(
        query, category,
        [this, guard = QPointer<SearchViewModel>(this), query, category, page, more,
         generation](QVariantList rows, QString code, QString message)
        {
            if (!guard || generation != m_generation)
                return;
            m_loadingMore = false;
            if (!code.isEmpty())
            {
                if (more)
                {
                    m_pageError = message;
                    emit statusChanged();
                }
                else
                {
                    m_errorCode = code;
                    setStatus(Error, message, QStringLiteral("D-SEARCH-") + code);
                }
                return;
            }
            emit entriesAboutToChange(more);
            m_hasMore = rows.size() == 30;
            m_page = page;
            m_resultsQuery = query;
            m_resultsCategory = category;
            if (!more)
                m_entries.clear();
            QSet<QString> ids;
            for (const auto &row : m_entries)
                ids.insert(row.toMap().value("id").toString());
            for (const auto &row : rows)
                if (!ids.contains(row.toMap().value("id").toString()))
                {
                    ids.insert(row.toMap().value("id").toString());
                    m_entries.append(row);
                }
            if (!more)
                rememberQuery(query);
            m_errorCode.clear();
            setStatus(m_entries.isEmpty() ? Empty : Ready);
        },
        page);
}
void SearchViewModel::retry()
{
    submitSearch();
}
void SearchViewModel::invalidateSession()
{
    m_entries.clear();
    ++m_generation;
    ++m_suggestionGeneration;
    m_suggestions.clear();
    m_suggestionsLoading = false;
    m_loadingMore = false;
    m_hasMore = false;
    m_page = 0;
    m_resultsQuery.clear();
    beginResetModel();
    m_tracks.clear();
    endResetModel();
    setStatus(Idle);
    emit suggestionsChanged();
    loadHotSearches();
}
void SearchViewModel::setStatus(Status status, QString message, QString diagnosticId)
{
    m_status = status;
    if (status != Error)
        m_errorCode.clear();
    m_errorMessage = std::move(message);
    m_diagnosticId = std::move(diagnosticId);
    emit statusChanged();
}
