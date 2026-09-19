#include "DiscoverViewModel.h"

#include "api/KuGouApi.h"
#include "features/catalog/CatalogService.h"
#include <QPointer>

DiscoverViewModel::DiscoverViewModel(KuGouApi *api, CatalogService *catalog, QObject *parent)
    : QObject(parent), m_api(api), m_catalog(catalog)
{
    connect(m_api, &KuGouApi::sessionInvalidated, this, &DiscoverViewModel::load);
    connect(m_catalog, &CatalogService::trackUpdated, this,
            [this](const Track &track)
            {
                bool updated = false;
                for (auto &row : m_dailyTracks)
                    if (row.toMap().value(QStringLiteral("key")).toString() == track.key)
                    {
                        row = track.toMap();
                        updated = true;
                    }
                if (updated)
                    emit changed();
            });
}

void DiscoverViewModel::load()
{
    const quint64 generation = ++m_generation;
    m_pending = 3;
    m_loading = true;
    m_errors.clear();
    m_errorMessage.clear();
    emit changed();

    const auto guard = QPointer<DiscoverViewModel>(this);
    m_api->dailyRecommendations(
        [this, guard, generation](QList<Track> tracks, QString, QString error)
        {
            if (!guard || generation != m_generation)
                return;
            m_dailyTracks.clear();
            for (const auto &track : tracks)
                m_dailyTracks.append(m_catalog->remember(track).toMap());
            finishRequest(generation, error);
        });
    m_api->rankEntries(
        [this, guard, generation](QVariantList rows, QString error)
        {
            if (!guard || generation != m_generation)
                return;
            m_ranks = std::move(rows);
            finishRequest(generation, error);
        });
    m_api->popularPlaylists(
        [this, guard, generation](QVariantList rows, QString error)
        {
            if (!guard || generation != m_generation)
                return;
            m_playlists = std::move(rows);
            finishRequest(generation, error);
        });
}

void DiscoverViewModel::finishRequest(quint64 generation, const QString &error)
{
    if (generation != m_generation)
        return;
    if (!error.isEmpty() && !m_errors.contains(error))
        m_errors.append(error);
    if (--m_pending > 0)
    {
        emit changed();
        return;
    }
    m_loading = false;
    m_errorMessage = m_errors.join(QStringLiteral("\n"));
    emit changed();
}
