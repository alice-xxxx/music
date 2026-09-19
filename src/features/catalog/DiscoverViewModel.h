#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

class CatalogService;
class KuGouApi;

class DiscoverViewModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList dailyTracks READ dailyTracks NOTIFY changed)
    Q_PROPERTY(QVariantList ranks READ ranks NOTIFY changed)
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)

  public:
    explicit DiscoverViewModel(KuGouApi *api, CatalogService *catalog,
                               QObject *parent = nullptr);

    QVariantList dailyTracks() const { return m_dailyTracks; }
    QVariantList ranks() const { return m_ranks; }
    QVariantList playlists() const { return m_playlists; }
    bool loading() const { return m_loading; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void load();

  signals:
    void changed();

  private:
    void finishRequest(quint64 generation, const QString &error);

    KuGouApi *m_api;
    CatalogService *m_catalog;
    QVariantList m_dailyTracks;
    QVariantList m_ranks;
    QVariantList m_playlists;
    QStringList m_errors;
    QString m_errorMessage;
    bool m_loading = false;
    int m_pending = 0;
    quint64 m_generation = 0;
};
