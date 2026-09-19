#pragma once

#include <QObject>
#include <QVariantList>

class KuGouApi;

class CommentsViewModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QVariantList comments READ comments NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool sending READ sending NOTIFY changed)
    Q_PROPERTY(bool sendUncertain READ sendUncertain NOTIFY changed)
    Q_PROPERTY(QString sendMessage READ sendMessage NOTIFY changed)

  public:
    explicit CommentsViewModel(KuGouApi *api, QObject *parent = nullptr);
    QString title() const { return m_title; }
    QVariantList comments() const { return m_comments; }
    bool loading() const { return m_loading; }
    bool hasMore() const { return m_hasMore; }
    QString errorMessage() const { return m_error; }
    bool sending() const { return m_sending; }
    bool sendUncertain() const { return m_sendUncertain; }
    QString sendMessage() const { return m_sendMessage; }

    Q_INVOKABLE void open(const QString &kind, const QString &id, const QString &title);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void send(const QString &content);

  signals:
    void changed();
    void sent();
    void loginRequired();

  private:
    void requestPage(int page);
    void invalidateSession();

    KuGouApi *m_api;
    QString m_kind, m_id, m_title;
    QVariantList m_comments;
    QString m_error, m_sendMessage;
    bool m_loading = false;
    bool m_hasMore = false;
    bool m_sending = false;
    bool m_sendUncertain = false;
    int m_page = 0;
    quint64 m_targetGeneration = 0;
    quint64 m_readGeneration = 0;
};
