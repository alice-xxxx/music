#pragma once
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QUrl>

// A per-source, memory-bounded byte transport. It owns no playback or account state.
class MediaStreamRelay final : public QObject
{
    Q_OBJECT
  public:
    explicit MediaStreamRelay(QObject *parent = nullptr);
    ~MediaStreamRelay() override;
    void open(const QUrl &source);
  signals:
    void ready(const QUrl &playableUrl);
    void failed();

  private:
    friend class MediaStreamConnection;
    QNetworkRequest request(qint64 start, qint64 end) const;
    QNetworkAccessManager m_network;
    QTcpServer m_server;
    QUrl m_source;
    QByteArray m_path;
    QByteArray m_validator;
    QByteArray m_validatorHeader;
    qint64 m_size = -1;
    int m_connections = 0;
};
