#pragma once
#include <QObject>
#include <QTimer>
class KuGouApi;
class SessionManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString qrImage READ qrImage NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool canRefresh READ canRefresh NOTIFY stateChanged)
    Q_PROPERTY(QString storageError READ storageError NOTIFY stateChanged)
    Q_PROPERTY(bool serviceConfigured READ serviceConfigured NOTIFY stateChanged)
  public:
    explicit SessionManager(KuGouApi *api, QObject *parent = nullptr);
    QString qrImage() const
    {
        return m_qrImage;
    }
    QString message() const
    {
        return m_message;
    }
    bool authenticated() const
    {
        return m_authenticated;
    }
    bool loading() const
    {
        return m_loading;
    }
    bool canRefresh() const
    {
        return serviceConfigured() && !m_loading && !m_timer.isActive() && !m_authenticated;
    }
    bool serviceConfigured() const;
    Q_INVOKABLE void startQrLogin();
    Q_INVOKABLE bool loginWithCookie(const QString &cookie);
    QString storageError() const;
    Q_INVOKABLE void cancelQrLogin();
    Q_INVOKABLE void logout();
  signals:
    void authenticatedChanged();
    void stateChanged();

  private:
    void poll();
    KuGouApi *m_api;
    QTimer m_timer;
    QString m_key, m_qrImage, m_message;
    bool m_authenticated = false;
    bool m_loading = false;
    bool m_pollInFlight = false;
    int m_pollFailures = 0;
    quint64 m_generation = 0;
};
