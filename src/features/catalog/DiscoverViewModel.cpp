#include "DiscoverViewModel.h"

#include "api/KuGouApi.h"
#include "features/catalog/CatalogService.h"
#include <QPointer>

DiscoverViewModel::DiscoverViewModel(KuGouApi *api, CatalogService *catalog, QObject *parent)
    : QObject(parent), m_api(api), m_catalog(catalog)
{
    connect(m_api, &KuGouApi::sessionInvalidated, this, [this] { load(); });
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

void DiscoverViewModel::load(bool forceRefresh)
{
    const quint64 generation = ++m_generation;
    m_pending = forceRefresh ? 1 : 3;
    m_loading = true;
    m_errors.clear();
    m_errorMessage.clear();
    m_refreshMessage = forceRefresh ? QStringLiteral("正在重新获取今日推荐…") : QString{};
    emit changed();

    const auto guard = QPointer<DiscoverViewModel>(this);
    m_api->dailyRecommendations(
        [this, guard, generation, forceRefresh](QList<Track> tracks, QString, QString error)
        {
            if (!guard || generation != m_generation)
                return;
            if (error.isEmpty())
            {
                QVariantList updated;
                for (const auto &track : tracks)
                    updated.append(m_catalog->remember(track).toMap());
                const bool unchanged = updated.size() == m_dailyTracks.size() &&
                    [&]
                    {
                        for (qsizetype i = 0; i < updated.size(); ++i)
                            if (updated.at(i).toMap().value(QStringLiteral("key")) !=
                                m_dailyTracks.at(i).toMap().value(QStringLiteral("key")))
                                return false;
                        return true;
                    }();
                m_dailyTracks = std::move(updated);
                if (forceRefresh)
                    m_refreshMessage = m_dailyTracks.isEmpty()
                        ? QStringLiteral("已重新获取；今日暂无推荐")
                        : unchanged ? QStringLiteral("已重新获取；每日推荐当天可能保持不变")
                                    : QStringLiteral("今日推荐已更新");
            }
            else if (forceRefresh)
                m_refreshMessage = QStringLiteral("刷新失败：") + error;
            finishRequest(generation, error);
        }, forceRefresh);
    if (forceRefresh)
        return;
    m_api->rankEntries(
        [this, guard, generation](QVariantList rows, QString error)
        {
            if (!guard || generation != m_generation)
                return;
            if (error.isEmpty())
                m_ranks = std::move(rows);
            finishRequest(generation, error);
        });
    m_api->popularPlaylists(
        [this, guard, generation](QVariantList rows, QString error)
        {
            if (!guard || generation != m_generation)
                return;
            if (error.isEmpty())
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
