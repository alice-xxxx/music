#include "CredentialCookieJar.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QNetworkCookie>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

namespace
{
constexpr auto storageMagic = "MUSIC_COOKIE_V1";
constexpr qsizetype nonceSize = 16;
constexpr qsizetype tagSize = 16;

QByteArray deriveKey(const QByteArray &context)
{
    return QCryptographicHash::hash(
        QByteArrayLiteral("MusicClient credential storage v1|") + context,
        QCryptographicHash::Sha256);
}

QByteArray randomBytes(qsizetype size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    for (qsizetype offset = 0; offset < size; offset += 4)
    {
        const quint32 value = QRandomGenerator::system()->generate();
        const qsizetype count = qMin<qsizetype>(4, size - offset);
        for (qsizetype index = 0; index < count; ++index)
            bytes[offset + index] = static_cast<char>(value >> (index * 8));
    }
    return bytes;
}

QByteArray applyKeyStream(const QByteArray &input, const QByteArray &key, const QByteArray &nonce)
{
    QByteArray output(input.size(), Qt::Uninitialized);
    for (qsizetype offset = 0, block = 0; offset < input.size(); offset += 32, ++block)
    {
        QByteArray seed = key + nonce;
        for (int shift = 56; shift >= 0; shift -= 8)
            seed.append(static_cast<char>(static_cast<quint64>(block) >> shift));
        const QByteArray stream = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
        const qsizetype count = qMin<qsizetype>(stream.size(), input.size() - offset);
        for (qsizetype index = 0; index < count; ++index)
            output[offset + index] = input[offset + index] ^ stream[index];
    }
    return output;
}

bool equalTags(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size())
        return false;
    unsigned char difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index)
        difference |= static_cast<unsigned char>(left[index] ^ right[index]);
    return difference == 0;
}

QByteArray encrypt(const QByteArray &plain, const QByteArray &context)
{
    const QByteArray magic(storageMagic);
    const QByteArray nonce = randomBytes(nonceSize);
    const QByteArray key = deriveKey(context);
    const QByteArray encrypted = applyKeyStream(plain, key, nonce);
    const QByteArray tag = QMessageAuthenticationCode::hash(magic + nonce + encrypted, key,
                                                             QCryptographicHash::Sha256)
                               .left(tagSize);
    return magic + nonce + encrypted + tag;
}

QByteArray decrypt(const QByteArray &stored, const QByteArray &context, bool *success)
{
    *success = false;
    const QByteArray magic(storageMagic);
    if (stored.size() < magic.size() + nonceSize + tagSize || !stored.startsWith(magic))
        return {};

    const QByteArray nonce = stored.mid(magic.size(), nonceSize);
    const QByteArray encrypted = stored.mid(magic.size() + nonceSize,
                                            stored.size() - magic.size() - nonceSize - tagSize);
    const QByteArray actualTag = stored.right(tagSize);
    const QByteArray key = deriveKey(context);
    const QByteArray expectedTag =
        QMessageAuthenticationCode::hash(magic + nonce + encrypted, key,
                                         QCryptographicHash::Sha256)
            .left(tagSize);
    if (!equalTags(actualTag, expectedTag))
        return {};

    *success = true;
    return applyKeyStream(encrypted, key, nonce);
}
} // namespace

CredentialCookieJar::CredentialCookieJar(const QUrl &serviceBase, QObject *parent, bool persistent)
    : QNetworkCookieJar(parent), m_serviceBase(serviceBase), m_persistent(persistent)
{
    const QUrl serviceIdentity = serviceBase.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment);
    const QByteArray identity = serviceIdentity.toString(QUrl::FullyEncoded).toUtf8();
    m_storageKey =
        QStringLiteral("auth/cookies.%1.encrypted")
            .arg(QString::fromLatin1(
                QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(16)));
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
    QSettings settings;
    const QByteArray encoded = settings.value(m_storageKey).toByteArray();
    const QByteArray encrypted = QByteArray::fromBase64(encoded);
    bool decrypted = false;
    const QByteArray payload = decrypt(encrypted, m_storageKey.toUtf8(), &decrypted);
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
}

void CredentialCookieJar::save()
{
    if (m_unreadableStorage)
        return;
    QByteArray payload;
    for (const QNetworkCookie &cookie : allCookies())
    {
        // RawForm excludes no values, so encryption is mandatory before persistent storage.
        payload += cookie.toRawForm(QNetworkCookie::Full);
        payload += '\n';
    }
    const auto encrypted = encrypt(payload, m_storageKey.toUtf8());
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
}
