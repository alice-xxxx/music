#include "MediaStreamRelay.h"
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(mediaTransport, "music.media.transport")

namespace
{
constexpr qint64 chunkSize = 256 * 1024;
constexpr qint64 socketBudget = 512 * 1024;
constexpr qint64 maximumSize = 512LL * 1024 * 1024;

bool contentRange(const QByteArray &header, qint64 &start, qint64 &end, qint64 &total)
{
    static const QRegularExpression pattern(QStringLiteral("^bytes (\\d+)-(\\d+)/(\\d+)$"));
    const auto match = pattern.match(QString::fromLatin1(header));
    if (!match.hasMatch())
        return false;
    bool okStart, okEnd, okTotal;
    start = match.captured(1).toLongLong(&okStart);
    end = match.captured(2).toLongLong(&okEnd);
    total = match.captured(3).toLongLong(&okTotal);
    return okStart && okEnd && okTotal && start <= end && end < total && total <= maximumSize;
}
} // namespace

class MediaStreamConnection final : public QObject
{
  public:
    MediaStreamConnection(QTcpSocket *socket, MediaStreamRelay *relay)
        : QObject(relay), m_socket(socket), m_relay(relay)
    {
        socket->setParent(this);
        socket->setReadBufferSize(8192);
        connect(socket, &QTcpSocket::readyRead, this, [this] { readRequest(); });
        connect(socket, &QTcpSocket::bytesWritten, this,
                [this]
                {
                    collect();
                    finishFetch();
                    pump();
                });
        connect(socket, &QTcpSocket::disconnected, this,
                [this]
                {
                    stopRequest();
                    deleteLater();
                });
        QTimer::singleShot(5000, this,
                           [this]
                           {
                               if (!m_parsed)
                                   m_socket->abort();
                           });
        readRequest();
    }
    ~MediaStreamConnection() override
    {
        stopRequest();
    }

  private:
    void stopRequest()
    {
        if (m_reply)
        {
            disconnect(m_reply, nullptr, this, nullptr);
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
    }
    void reject(int code)
    {
        m_parsed = false;
        stopRequest();
        m_socket->write("HTTP/1.1 " + QByteArray::number(code) +
                        " Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        m_socket->disconnectFromHost();
    }
    void readRequest()
    {
        if (m_parsed)
            return;
        m_header += m_socket->readAll();
        if (m_header.size() > 8192)
        {
            reject(431);
            return;
        }
        if (!m_header.contains("\r\n\r\n"))
            return;
        const auto lines = m_header.split('\n');
        const auto first = lines.first().trimmed().split(' ');
        if (first.size() != 3 || first[1] != m_relay->m_path ||
            (first[0] != "GET" && first[0] != "HEAD"))
        {
            reject(404);
            return;
        }
        m_end = m_relay->m_size - 1;
        for (const auto &line : lines)
        {
            if (!line.toLower().startsWith("range:"))
                continue;
            static const QRegularExpression range(QStringLiteral("^bytes=(\\d+)-(\\d*)$"));
            const auto match = range.match(QString::fromLatin1(line.mid(6).trimmed()));
            bool validStart = false, validEnd = true;
            if (match.hasMatch())
            {
                m_offset = match.captured(1).toLongLong(&validStart);
                if (!match.captured(2).isEmpty())
                    m_end = qMin(m_end, match.captured(2).toLongLong(&validEnd));
            }
            if (!validStart || !validEnd || m_offset > m_end || m_offset < 0)
            {
                reject(416);
                return;
            }
            m_partial = true;
        }
        m_parsed = true;
        QByteArray response =
            m_partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        response += "Content-Length: " + QByteArray::number(m_end - m_offset + 1) +
                    "\r\nAccept-Ranges: bytes\r\nContent-Type: application/octet-stream\r\n";
        if (m_partial)
            response += "Content-Range: bytes " + QByteArray::number(m_offset) + "-" +
                        QByteArray::number(m_end) + "/" + QByteArray::number(m_relay->m_size) +
                        "\r\n";
        response += "Connection: close\r\n\r\n";
        m_socket->write(response);
        if (first[0] == "HEAD")
        {
            m_parsed = false;
            m_socket->disconnectFromHost();
            return;
        }
        pump();
    }
    void pump()
    {
        if (!m_parsed || m_reply || m_retryPending ||
            m_socket->state() != QAbstractSocket::ConnectedState)
            return;
        if (m_offset > m_end)
        {
            m_parsed = false;
            m_socket->disconnectFromHost();
            return;
        }
        if (m_socket->bytesToWrite() >= socketBudget)
            return;
        m_fetchEnd = qMin(m_offset + chunkSize - 1, m_end);
        fetch();
    }
    void fetch()
    {
        if (m_socket->state() != QAbstractSocket::ConnectedState)
            return;
        m_fetchStart = m_offset;
        m_received = 0;
        m_checked = false;
        m_invalid = false;
        m_replyFinished = false;
        auto *reply = m_relay->m_network.get(m_relay->request(m_fetchStart, m_fetchEnd));
        m_reply = reply;
        reply->setReadBufferSize(chunkSize);
        QTimer::singleShot(10000, reply,
                           [reply]
                           {
                               if (!reply->isFinished())
                                   reply->abort();
                           });
        connect(reply, &QNetworkReply::readyRead, this, [this] { collect(); });
        connect(reply, &QNetworkReply::finished, this,
                [this, reply]
                {
                    collect();
                    m_replyFinished = true;
                    finishFetch();
                });
    }
    void collect()
    {
        if (!m_reply || m_invalid)
            return;
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200)
        {
            // If-Range returning the full entity indicates a changed/unsupported resource.
            m_invalid = true;
            m_reply->abort();
            return;
        }
        if (status != 206)
            return;
        if (!m_checked)
        {
            qint64 start, end, total;
            if (!contentRange(m_reply->rawHeader("Content-Range"), start, end, total) ||
                start != m_fetchStart || end != m_fetchEnd || total != m_relay->m_size ||
                (!m_reply->rawHeader("Content-Encoding").isEmpty() &&
                 m_reply->rawHeader("Content-Encoding") != "identity") ||
                (m_reply->hasRawHeader("Content-Length") &&
                 m_reply->rawHeader("Content-Length").toLongLong() != end - start + 1) ||
                (!m_relay->m_validator.isEmpty() &&
                 m_reply->rawHeader(m_relay->m_validatorHeader) != m_relay->m_validator))
            {
                m_invalid = true;
                m_reply->abort();
                return;
            }
            m_checked = true;
        }
        const qint64 capacity = socketBudget - m_socket->bytesToWrite();
        if (capacity <= 0)
            return;
        const qint64 expected = m_fetchEnd - m_fetchStart + 1;
        const auto data = m_reply->read(qMin(capacity, expected - m_received + 1));
        if (data.isEmpty())
            return;
        m_received += data.size();
        if (m_received > expected || m_socket->write(data) != data.size())
        {
            m_invalid = true;
            m_reply->abort();
            return;
        }
        m_offset += data.size();
    }
    void finishFetch()
    {
        if (!m_reply || !m_replyFinished)
            return;
        collect();
        if (m_reply->bytesAvailable() > 0 && !m_invalid)
            return;
        const int status =
            m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = m_reply->error();
        auto *reply = m_reply.data();
        m_reply = nullptr;
        reply->deleteLater();
        if (m_invalid)
        {
            m_socket->abort();
            return;
        }
        if (m_checked && m_received == m_fetchEnd - m_fetchStart + 1)
        {
            m_attempts = 0;
            pump();
            return;
        }
        const bool transient = status == 0 || status == 206 || status == 408 || status == 429 ||
                               status >= 500;
        if (!transient || (error == QNetworkReply::NoError && !m_checked) || ++m_attempts > 3)
        {
            m_socket->abort();
            return;
        }
        m_retryPending = true;
        qCInfo(mediaTransport) << "Resuming interrupted media segment, attempt" << m_attempts;
        QTimer::singleShot(100 * m_attempts, this,
                           [this]
                           {
                               m_retryPending = false;
                               fetch();
                           });
    }

    QTcpSocket *m_socket;
    MediaStreamRelay *m_relay;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_header;
    qint64 m_offset = 0, m_end = -1, m_fetchStart = 0, m_fetchEnd = -1;
    qint64 m_received = 0;
    int m_attempts = 0;
    bool m_parsed = false, m_partial = false, m_checked = false;
    bool m_invalid = false, m_retryPending = false, m_replyFinished = false;
};

MediaStreamRelay::MediaStreamRelay(QObject *parent) : QObject(parent)
{
    m_server.setProxy(QNetworkProxy::NoProxy);
    connect(&m_server, &QTcpServer::newConnection, this,
            [this]
            {
                while (auto *socket = m_server.nextPendingConnection())
                {
                    if (m_connections >= 4)
                    {
                        socket->abort();
                        socket->deleteLater();
                        continue;
                    }
                    ++m_connections;
                    auto *connection = new MediaStreamConnection(socket, this);
                    connect(connection, &QObject::destroyed, this, [this] { --m_connections; });
                }
            });
}
MediaStreamRelay::~MediaStreamRelay()
{
    // Connections use m_network: destroy them before member destruction.
    const auto connections = children();
    for (auto *connection : connections)
        delete connection;
}
QNetworkRequest MediaStreamRelay::request(qint64 start, qint64 end) const
{
    QNetworkRequest result(m_source);
    result.setRawHeader("Range",
                        "bytes=" + QByteArray::number(start) + "-" + QByteArray::number(end));
    result.setRawHeader("Accept-Encoding", "identity");
    result.setTransferTimeout(5000);
    result.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    result.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    result.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_validator.isEmpty())
        result.setRawHeader("If-Range", m_validator);
    return result;
}
void MediaStreamRelay::open(const QUrl &source)
{
    m_source = source;
    m_path = "/" + QUuid::createUuid().toByteArray(QUuid::Id128);
    if (!m_server.listen(QHostAddress::LocalHost, 0))
    {
        emit failed();
        return;
    }
    auto *probe = m_network.get(request(0, 0));
    probe->setReadBufferSize(1024);
    QTimer::singleShot(10000, probe,
                       [probe]
                       {
                           if (!probe->isFinished())
                               probe->abort();
                       });
    connect(probe, &QNetworkReply::readyRead, this,
            [probe]
            {
                // An origin ignoring Range must not cause a whole-song pre-download.
                if (probe->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 ||
                    probe->bytesAvailable() > 1)
                    probe->abort();
            });
    connect(probe, &QNetworkReply::finished, this,
            [this, probe]
            {
                probe->deleteLater();
                const int status =
                    probe->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (status == 200)
                {
                    m_server.close();
                    qCInfo(mediaTransport) << "Origin has no range support; using direct playback";
                    emit ready(m_source);
                    return;
                }
                qint64 start, end, total;
                if (status != 206 || probe->error() != QNetworkReply::NoError ||
                    probe->readAll().size() != 1 ||
                    !contentRange(probe->rawHeader("Content-Range"), start, end, total) ||
                    start != 0 || end != 0)
                {
                    emit failed();
                    return;
                }
                m_size = total;
                m_validator = probe->rawHeader("ETag");
                m_validatorHeader = "ETag";
                if (m_validator.isEmpty() || m_validator.startsWith("W/"))
                {
                    m_validator = probe->rawHeader("Last-Modified");
                    m_validatorHeader = "Last-Modified";
                }
                QUrl local;
                local.setScheme(QStringLiteral("http"));
                local.setHost(QStringLiteral("127.0.0.1"));
                local.setPort(m_server.serverPort());
                local.setPath(QString::fromLatin1(m_path));
                qCInfo(mediaTransport) << "Using segmented HTTP media transport";
                emit ready(local);
            });
}
