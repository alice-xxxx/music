#pragma once

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QUrlQuery>
#include <functional>

class CredentialCookieJar;

class ApiClient final : public QObject
{
  public:
    struct Response
    {
        int httpStatus = 0;
        QJsonDocument json;
        QByteArray body;
        QString errorCode;
        QString errorMessage;
        QString requestId;
    };
    using Callback = std::function<void(Response)>;

    explicit ApiClient(QUrl serviceBase, QObject *parent = nullptr, bool persistentCookies = true);
    void setServiceBase(QUrl serviceBase);

    void get(QString path, QUrlQuery query, Callback callback,
             bool explicitlyUserInitiatedAuthentication = false);
    void postJson(QString path, QUrlQuery query, QJsonDocument body, Callback callback,
                  bool explicitlyUserInitiatedAuthentication = false);
    CredentialCookieJar *cookieJar() const;
    bool storeLoginSession(const QString &token, const QString &userId);
    bool storeLoginCookies(const QString &cookieHeader, QString *error = nullptr);
    bool importCookies(const QList<QNetworkCookie> &cookies);
    void invalidateSession();
    // Explicit cancellation discards the callback; the caller owns its UI state.
    void cancelRequests(const QString &path);
  private:
    enum class Method
    {
        Get,
        Post
    };
    void request(QString path, Method method, QUrlQuery query, QByteArray body,
                 QByteArray contentType, Callback callback,
                 bool explicitlyUserInitiatedAuthentication = false);
    static bool isAuthenticationEndpoint(const QString &path);
    QUrl m_serviceBase;
    QNetworkAccessManager m_network;
    bool m_persistentCookies = true;
    quint64 m_sessionGeneration = 0;
    quint64 m_nextRequestId = 0;
};
