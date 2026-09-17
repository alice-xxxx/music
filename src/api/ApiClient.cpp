#include "ApiClient.h"
#include "platform/CredentialCookieJar.h"

#include <QJsonObject>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>
#include <memory>

namespace
{
QString stringValue(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys)
    {
        const QJsonValue value = object.value(QLatin1String(key));
        if (value.isString())
            return value.toString();
        if (value.isDouble())
            return QString::number(value.toInteger());
    }
    return {};
}
} // namespace

ApiClient::ApiClient(QUrl serviceBase, QObject *parent, bool persistentCookies)
    : QObject(parent), m_serviceBase(std::move(serviceBase)),
      m_persistentCookies(persistentCookies)
{
    m_network.setCookieJar(new CredentialCookieJar(m_serviceBase, &m_network, persistentCookies));
}

void ApiClient::setServiceBase(QUrl serviceBase)
{
    if (m_serviceBase == serviceBase)
        return;
    invalidateSession();
    auto *oldJar = m_network.cookieJar();
    m_serviceBase = std::move(serviceBase);
    m_network.clearAccessCache();
    m_network.setCookieJar(
        new CredentialCookieJar(m_serviceBase, &m_network, m_persistentCookies));
    if (oldJar)
        oldJar->deleteLater();
}

bool ApiClient::isAuthenticationEndpoint(const QString &path)
{
    return path.startsWith(QStringLiteral("/login/")) || path == QStringLiteral("/login") ||
           path.startsWith(QStringLiteral("/captcha/"));
}

void ApiClient::request(QString path, Method method, QUrlQuery query, QByteArray body,
                        QByteArray contentType, Callback callback,
                        bool explicitlyUserInitiatedAuthentication)
{
    if (m_serviceBase.host().isEmpty() || !m_serviceBase.isValid())
    {
        callback({0,
                  {},
                  {},
                  QStringLiteral("InvalidService"),
                  QStringLiteral("请先在设置中填写渠道后端地址"),
                  {}});
        return;
    }
    if (!path.startsWith(QLatin1Char('/')) || path.contains(QStringLiteral("..")) ||
        path.contains(QLatin1Char('?')) || path.contains(QLatin1Char('#')))
    {
        callback({0,
                  {},
                  {},
                  QStringLiteral("InvalidEndpoint"),
                  QStringLiteral("API 路径必须是不含查询串的绝对路径"),
                  {}});
        return;
    }
    if (isAuthenticationEndpoint(path) && !explicitlyUserInitiatedAuthentication)
    {
        callback({0,
                  {},
                  {},
                  QStringLiteral("ExplicitUserActionRequired"),
                  QStringLiteral("登录类接口只能由用户明确操作触发"),
                  {}});
        return;
    }

    QUrl url = m_serviceBase.resolved(QUrl(path.mid(1)));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(12000);
    if (auto *jar = cookieJar(); jar && jar->hasLoginSession())
    {
        const QByteArray authorization = jar->authorizationHeader(url);
        if (!authorization.isEmpty())
            request.setRawHeader(QByteArrayLiteral("Authorization"), authorization);
    }
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);

    QNetworkReply *reply = nullptr;
    if (method == Method::Get)
        reply = m_network.get(request);
    else
        reply = m_network.post(request, body);
    reply->setProperty("endpointPath", path);

    const quint64 generation = m_sessionGeneration;
    const QString requestId = QStringLiteral("req-%1").arg(++m_nextRequestId);
    // Bound wall-clock time even when a server keeps sending small chunks.
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply,
            [reply]
            {
                reply->setProperty("deadlineExceeded", true);
                reply->abort();
            });
    connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(12000);
    reply->setReadBufferSize(4 * 1024 * 1024 + 1);
    auto payload = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this,
            [reply, payload]
            {
                if (reply->isOpen())
                    payload->append(reply->readAll());
                if (payload->size() > 4 * 1024 * 1024)
                {
                    payload->clear();
                    reply->setProperty("responseTooLarge", true);
                    reply->abort();
                }
            });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [reply](qint64 received, qint64)
            {
                if (received > 4 * 1024 * 1024)
                {
                    reply->setProperty("responseTooLarge", true);
                    reply->abort();
                }
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, payload, generation, requestId, callback = std::move(callback)]() mutable
            {
                const auto cleanup = qScopeGuard(
                    [guard = QPointer<QNetworkReply>(reply)]
                    {
                        if (guard)
                            guard->deleteLater();
                    });
                if (generation != m_sessionGeneration ||
                    reply->property("explicitlyCancelled").toBool())
                    return;
                Response response;
                response.requestId = requestId;
                response.httpStatus =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->isOpen())
                    payload->append(reply->readAll());
                if (payload->size() > 4 * 1024 * 1024)
                {
                    reply->setProperty("responseTooLarge", true);
                    payload->clear();
                }
                response.body = std::move(*payload);
                QJsonParseError parseError;
                response.json = QJsonDocument::fromJson(response.body, &parseError);
                const QJsonObject root = response.json.object();
                if (reply->property("responseTooLarge").toBool())
                {
                    response.errorCode = QStringLiteral("ResponseTooLarge");
                    response.errorMessage = QStringLiteral("响应超过 4 MiB 上限");
                }
                else if (reply->property("deadlineExceeded").toBool())
                {
                    response.errorCode = QStringLiteral("Timeout");
                    response.errorMessage = QStringLiteral("请求超时，请重试");
                }
                else if (reply->error() != QNetworkReply::NoError || response.httpStatus >= 300)
                {
                    if (response.httpStatus == 429)
                        response.errorCode = QStringLiteral("RateLimited");
                    else if (reply->error() == QNetworkReply::TimeoutError)
                        response.errorCode = QStringLiteral("Timeout");
                    else
                        response.errorCode = QStringLiteral("Unavailable");
                    response.errorMessage = stringValue(root, {"message", "error", "msg"});
                    if (response.errorMessage.isEmpty())
                    {
                        response.errorMessage = QStringLiteral("服务未返回可用数据");
                    }
                    if (response.httpStatus > 0)
                        response.errorMessage +=
                            QStringLiteral("（HTTP %1）").arg(response.httpStatus);
                }
                else if (parseError.error != QJsonParseError::NoError &&
                         reply->header(QNetworkRequest::ContentTypeHeader)
                             .toString()
                             .startsWith(QStringLiteral("application/json"), Qt::CaseInsensitive))
                {
                    response.errorCode = QStringLiteral("SchemaMismatch");
                    response.errorMessage = QStringLiteral("服务返回的 JSON 无法解析");
                }
                callback(std::move(response));
            });
}

void ApiClient::cancelRequests(const QString &path)
{
    const auto replies = m_network.findChildren<QNetworkReply *>();
    for (auto *reply : replies)
        if (!reply->isFinished() && reply->property("endpointPath").toString() == path)
        {
            reply->setProperty("explicitlyCancelled", true);
            reply->abort();
        }
}
void ApiClient::invalidateSession()
{
    ++m_sessionGeneration;
    const auto replies = m_network.findChildren<QNetworkReply *>();
    for (QNetworkReply *reply : replies)
        reply->abort();
}

bool ApiClient::importCookies(const QList<QNetworkCookie> &cookies)
{
    return cookieJar() && cookieJar()->setCookiesFromUrl(cookies, m_serviceBase);
}

void ApiClient::get(QString path, QUrlQuery query, Callback callback,
                    bool explicitlyUserInitiatedAuthentication)
{
    request(std::move(path), Method::Get, std::move(query), {}, {}, std::move(callback),
            explicitlyUserInitiatedAuthentication);
}

void ApiClient::postJson(QString path, QUrlQuery query, QJsonDocument body, Callback callback,
                         bool explicitlyUserInitiatedAuthentication)
{
    request(std::move(path), Method::Post, std::move(query),
            body.toJson(QJsonDocument::Compact), QByteArrayLiteral("application/json"),
            std::move(callback), explicitlyUserInitiatedAuthentication);
}

CredentialCookieJar *ApiClient::cookieJar() const
{
    return qobject_cast<CredentialCookieJar *>(m_network.cookieJar());
}

bool ApiClient::storeLoginSession(const QString &token, const QString &userId)
{
    if (token.trimmed().isEmpty() || userId.trimmed().isEmpty() || userId == QStringLiteral("0"))
        return false;
    QNetworkCookie tokenCookie(QByteArrayLiteral("token"), token.toUtf8());
    QNetworkCookie userCookie(QByteArrayLiteral("userid"), userId.toUtf8());
    tokenCookie.setSecure(m_serviceBase.scheme() == QStringLiteral("https"));
    userCookie.setSecure(tokenCookie.isSecure());
    tokenCookie.setPath(QStringLiteral("/"));
    userCookie.setPath(QStringLiteral("/"));
    cookieJar()->setCookiesFromUrl({tokenCookie, userCookie}, m_serviceBase);
    return cookieJar()->hasLoginSession();
}

bool ApiClient::storeLoginCookies(const QString &cookieHeader, QString *error)
{
    return cookieJar() && cookieJar()->importCookieHeader(cookieHeader, error);
}
