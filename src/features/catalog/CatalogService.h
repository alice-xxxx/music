#pragma once
#include "domain/Track.h"
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>
class KuGouApi;

class CatalogService final : public QObject
{
    Q_OBJECT
  public:
    explicit CatalogService(KuGouApi *api, QObject *parent = nullptr);
    Track remember(Track track);
    void ensureMetadata(const QString &key, bool current = false);
    void cancelQueuedMetadata(const QString &key);
    void clear();
  signals:
    void trackUpdated(const Track &track);

  private:
    void pump();
    KuGouApi *m_api;
    QHash<QString, Track> m_tracks;
    QQueue<QString> m_order;
    QQueue<QString> m_waiting;
    QSet<QString> m_requested;
    QString m_currentKey;
    QHash<QString, qint64> m_retryAfter;
    QHash<QString, int> m_failures;
    quint64 m_generation = 0;
    int m_active = 0;
};
