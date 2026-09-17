#pragma once
#include "domain/Track.h"
#include <QObject>
#include <QList>
class KuGouApi;
class SessionManager;

class FavoritesController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool liked READ liked NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool uncertain READ uncertain NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
  public:
    FavoritesController(KuGouApi *api, SessionManager *session, QObject *parent = nullptr);
    bool liked() const;
    bool uncertain() const
    {
        return m_uncertain;
    }
    bool busy() const
    {
        return m_busy;
    }
    QString message() const
    {
        return m_message;
    }
    QVariantList tracks() const;
    void setCurrentTrack(const QVariantMap &track);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void cancelPending();
  signals:
    void changed();
    void loginRequested();
    void resumeTrackRequested(const QVariantMap &track);

  private:
    void discover(int page, bool afterCreate = false);
    void readTracks(int page, bool confirmWrite = false);
    void write();
    void fail(const QString &message);
    KuGouApi *m_api;
    SessionManager *m_session;
    Track m_current;
    Track m_pending;
    QList<Track> m_tracks;
    QList<Track> m_readTracks;
    QString m_listId;
    QString m_message;
    bool m_busy = false;
    bool m_uncertain = false;
    bool m_creationPending = false;
    bool m_wantLiked = false;
    bool m_waitingLogin = false;
    quint64 m_generation = 0;
};
