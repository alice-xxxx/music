#include "CredentialCookieJar.h"
#include <QCryptographicHash>
#include <QNetworkCookie>
#include <QSettings>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>

namespace
{
QByteArray protect(const QByteArray &plain)
{
    DATA_BLOB input{static_cast<DWORD>(plain.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Music session", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output))
        return {};
    const QByteArray encrypted(reinterpret_cast<const char *>(output.pbData),
                               static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return encrypted;
}
QByteArray unprotect(const QByteArray &encrypted, bool *success)
{
    *success = false;
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.data()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &output))
        return {};
    const QByteArray plain(reinterpret_cast<const char *>(output.pbData),
                           static_cast<qsizetype>(output.cbData));
    *success = true;
    LocalFree(output.pbData);
    return plain;
}
} // namespace
#endif

CredentialCookieJar::CredentialCookieJar(const QUrl &serviceBase, QObject *parent, bool persistent)
    : QNetworkCookieJar(parent), m_serviceBase(serviceBase), m_persistent(persistent)
{
    const QUrl serviceIdentity = serviceBase.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment);
    const QByteArray identity = serviceIdentity.toString(QUrl::FullyEncoded).toUtf8();
    m_storageKey =
        QStringLiteral("auth/cookies.%1.dpapi")
            .arg(QString::fromLatin1(
                QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(16)));
    // Releases before service-scoped credential storage used one global key.
    // Only the original built-in service may import it; custom services must
    // never receive credentials that were issued for another origin.
    m_canImportLegacyStorage =
        serviceIdentity.scheme() == QStringLiteral("https") && serviceIdentity.port(443) == 443 &&
        serviceIdentity.userInfo().isEmpty() &&
        (serviceIdentity.path().isEmpty() || serviceIdentity.path() == QStringLiteral("/")) &&
        serviceIdentity.host().compare(QStringLiteral("ku-gou-music-api-gold-beta.vercel.app"),
                                       Qt::CaseInsensitive) == 0;
    if (m_persistent)
        load();
}

bool CredentialCookieJar::setCookiesFromUrl(const QList<QNetworkCookie> &cookies, const QUrl &url)
{
    if (url.scheme() != m_serviceBase.scheme() || url.host() != m_serviceBase.host() ||
        url.port() != m_serviceBase.port())
        return false;
    QList<QNetworkCookie> validCookies;
    validCookies.reserve(cookies.size());
    for (const QNetworkCookie &cookie : cookies)
    {
        const QByteArray value = cookie.value().trimmed();
        // Some deployments return dfid=undefined when device registration fails.
        // Persisting it overrides the server's random fallback on every request.
        // Empty values carry RFC cookie deletion semantics and must reach Qt's jar.
        if (!value.isEmpty() &&
            (value == QByteArrayLiteral("undefined") || value == QByteArrayLiteral("null")))
            continue;
        validCookies.push_back(cookie);
    }
    const bool changed = QNetworkCookieJar::setCookiesFromUrl(validCookies, url);
    if (changed && m_persistent)
        save();
    return changed;
}
QList<QNetworkCookie> CredentialCookieJar::cookiesForUrl(const QUrl &url) const
{
    if (url.scheme() != m_serviceBase.scheme() || url.host() != m_serviceBase.host() ||
        url.port() != m_serviceBase.port())
        return {};
    const auto basePath =
        m_serviceBase.path().isEmpty() ? QStringLiteral("/") : m_serviceBase.path();
    if (!url.path().startsWith(basePath))
        return {};
    return QNetworkCookieJar::cookiesForUrl(url);
}

void CredentialCookieJar::clearStoredCookies()
{
    setAllCookies({});
    m_unreadableStorage = false;
    m_storageError.clear();
    if (!m_persistent)
        return;
    QSettings settings;
    settings.remove(m_storageKey);
    if (m_canImportLegacyStorage)
    {
        settings.remove(QStringLiteral("auth/cookies.dpapi"));
        settings.setValue(QStringLiteral("auth/legacyImportDisabled"), true);
    }
}

bool CredentialCookieJar::hasLoginSession() const
{
    bool hasToken = false;
    bool hasUserId = false;
    for (const QNetworkCookie &cookie : allCookies())
    {
        if (!cookie.isSessionCookie() && cookie.expirationDate() <= QDateTime::currentDateTimeUtc())
            continue;
        if (cookie.name() == QByteArrayLiteral("token") && !cookie.value().isEmpty() &&
            cookie.value() != QByteArrayLiteral("undefined"))
            hasToken = true;
        if (cookie.name() == QByteArrayLiteral("userid") && !cookie.value().isEmpty() &&
            cookie.value() != QByteArrayLiteral("0") &&
            cookie.value() != QByteArrayLiteral("undefined"))
            hasUserId = true;
    }
    return hasToken && hasUserId;
}

bool CredentialCookieJar::hasDeviceRegistration() const
{
    for (const QNetworkCookie &cookie : allCookies())
    {
        if (!cookie.isSessionCookie() && cookie.expirationDate() <= QDateTime::currentDateTimeUtc())
            continue;
        const QByteArray value = cookie.value().trimmed();
        if (cookie.name() == QByteArrayLiteral("dfid") && !value.isEmpty() &&
            value != QByteArrayLiteral("undefined") && value != QByteArrayLiteral("null"))
            return true;
    }
    return false;
}

bool CredentialCookieJar::importCookieHeader(const QString &header, QString *error)
{
    auto fail = [error](const QString &message)
    {
        if (error)
            *error = message;
        return false;
    };
    if (m_serviceBase.host().isEmpty())
        return fail(QStringLiteral("请先填写渠道后端地址"));

    QString value = header.trimmed();
    if (value.startsWith(QStringLiteral("Cookie:"), Qt::CaseInsensitive))
        value = value.mid(7).trimmed();
    if (value.isEmpty())
        return fail(QStringLiteral("请输入 Cookie"));
    if (value.size() > 64 * 1024)
        return fail(QStringLiteral("Cookie 内容过长"));
    for (const QChar character : value)
        if (character.unicode() < 0x20 || character.unicode() == 0x7f)
            return fail(QStringLiteral("Cookie 必须是单行 name=value 格式"));

    static const QRegularExpression validName(
        QStringLiteral("^[!#$%&'*+\\-.^_`|~0-9A-Za-z]+$"));
    QList<QNetworkCookie> cookies;
    QSet<QByteArray> names;
    QByteArray token;
    QByteArray userId;
    for (const QString &part : value.split(QLatin1Char(';'), Qt::SkipEmptyParts))
    {
        const QString item = part.trimmed();
        const qsizetype separator = item.indexOf(QLatin1Char('='));
        if (separator <= 0)
            return fail(QStringLiteral("Cookie 中存在无效字段"));
        const QString name = item.left(separator).trimmed();
        const QString cookieValue = item.mid(separator + 1).trimmed();
        if (!validName.match(name).hasMatch() || cookieValue.isEmpty())
            return fail(QStringLiteral("Cookie 中存在无效的名称或空值"));
        const QByteArray encodedName = name.toUtf8();
        if (names.contains(encodedName))
            return fail(QStringLiteral("Cookie 中存在重复名称"));
        names.insert(encodedName);
        const QByteArray encodedValue = cookieValue.toUtf8();
        if (encodedName == QByteArrayLiteral("token"))
            token = encodedValue;
        else if (encodedName == QByteArrayLiteral("userid"))
            userId = encodedValue;
        QNetworkCookie cookie(encodedName, encodedValue);
        cookie.setSecure(m_serviceBase.scheme() == QStringLiteral("https"));
        cookie.setPath(QStringLiteral("/"));
        cookies.push_back(cookie);
    }
    if (token.isEmpty() || userId.isEmpty())
        return fail(QStringLiteral("Cookie 必须包含 token 和 userid"));
    if (token == QByteArrayLiteral("undefined") || token == QByteArrayLiteral("null") ||
        userId == QByteArrayLiteral("0") || userId == QByteArrayLiteral("undefined") ||
        userId == QByteArrayLiteral("null"))
        return fail(QStringLiteral("Cookie 中的 token 或 userid 无效"));

    setAllCookies({});
    if (!QNetworkCookieJar::setCookiesFromUrl(cookies, m_serviceBase) || !hasLoginSession())
        return fail(QStringLiteral("Cookie 无法建立登录会话"));
    if (m_persistent)
        save();
    if (error)
        error->clear();
    return true;
}

QString CredentialCookieJar::userId() const
{
    for (const QNetworkCookie &cookie : allCookies())
        if (cookie.name() == QByteArrayLiteral("userid") &&
            (cookie.isSessionCookie() || cookie.expirationDate() > QDateTime::currentDateTimeUtc()))
            return QString::fromUtf8(cookie.value());
    return {};
}

QByteArray CredentialCookieJar::authorizationHeader(const QUrl &url) const
{
    QByteArray header;
    for (const QNetworkCookie &cookie : cookiesForUrl(url))
    {
        if (!cookie.isSessionCookie() && cookie.expirationDate() <= QDateTime::currentDateTimeUtc())
            continue;
        if (!header.isEmpty())
            header += QByteArrayLiteral("; ");
        header += cookie.toRawForm(QNetworkCookie::NameAndValueOnly);
    }
    return header;
}

void CredentialCookieJar::load()
{
#ifdef Q_OS_WIN
    QSettings settings;
    QByteArray encoded = settings.value(m_storageKey).toByteArray();
    const bool importingLegacy =
        encoded.isEmpty() && m_canImportLegacyStorage &&
        !settings.value(QStringLiteral("auth/legacyImportDisabled"), false).toBool();
    if (importingLegacy)
        encoded = settings.value(QStringLiteral("auth/cookies.dpapi")).toByteArray();
    const QByteArray encrypted = QByteArray::fromBase64(encoded);
    bool decrypted = false;
    const QByteArray payload = unprotect(encrypted, &decrypted);
    if (!encoded.isEmpty() && !decrypted)
    {
        m_unreadableStorage = true;
        m_storageError = QStringLiteral(
            "无法解密已有登录数据，原数据已保留；新登录仅用于本次会话。主动退出登录可清除旧数据");
        return;
    }
    QList<QNetworkCookie> cookies;
    for (const QByteArray &line : payload.split('\n'))
    {
        for (const QNetworkCookie &cookie : QNetworkCookie::parseCookies(line))
        {
            const QByteArray value = cookie.value().trimmed();
            if (!value.isEmpty() && value != QByteArrayLiteral("undefined") &&
                value != QByteArrayLiteral("null"))
                cookies.append(cookie);
        }
    }
    setAllCookies(cookies);
    // Copy, rather than move, so an interrupted upgrade cannot destroy the
    // only recoverable login. The legacy key is never consulted by custom services.
    if (importingLegacy && !cookies.isEmpty())
        save();
#endif
}

void CredentialCookieJar::save()
{
    if (m_unreadableStorage)
        return;
#ifdef Q_OS_WIN
    QByteArray payload;
    for (const QNetworkCookie &cookie : allCookies())
    {
        // RawForm excludes no values, so encryption is mandatory before persistent storage.
        payload += cookie.toRawForm(QNetworkCookie::Full);
        payload += '\n';
    }
    const auto encrypted = protect(payload);
    if (encrypted.isEmpty())
    {
        m_storageError = QStringLiteral("无法加密保存登录状态，本次会话仍可使用");
        return;
    }
    QSettings settings;
    settings.setValue(m_storageKey, encrypted.toBase64());
    settings.sync();
    m_storageError = settings.status() == QSettings::NoError
                         ? QString{}
                         : QStringLiteral("无法保存登录状态，本次会话仍可使用");
#else
    m_storageError = QStringLiteral("此平台尚未接入安全存储，仅保留本次登录");
#endif
}
