#pragma once
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QUrl>

class CredentialCookieJar final : public QNetworkCookieJar
{
    Q_OBJECT
  public:
    explicit CredentialCookieJar(const QUrl &serviceBase, QObject *parent = nullptr,
                                 bool persistent = true);
    bool setCookiesFromUrl(const QList<QNetworkCookie> &cookieList, const QUrl &url) override;
    QList<QNetworkCookie> cookiesForUrl(const QUrl &url) const override;
    QString storageError() const
    {
        return m_storageError;
    }
    void clearStoredCookies();
    bool hasLoginSession() const;
    bool hasDeviceRegistration() const;
    bool importCookieHeader(const QString &header, QString *error = nullptr);
    QString userId() const;
    QByteArray authorizationHeader(const QUrl &url) const;
    QList<QNetworkCookie> cookies() const
    {
        return allCookies();
    }

  private:
    void load();
    void save();
    QUrl m_serviceBase;
    QString m_storageError;
    QString m_storageKey;
    bool m_canImportLegacyStorage = false;
    bool m_persistent = true;
    bool m_unreadableStorage = false;
};
