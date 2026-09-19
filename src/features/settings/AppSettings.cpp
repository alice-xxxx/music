#include "AppSettings.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#include <QStandardPaths>
#include <QNetworkDiskCache>
#include <QFile>
#include <QSysInfo>
#include <QCoreApplication>

AppSettings::AppSettings(QObject *parent) : QObject(parent)
{
    QSettings settings;
    const auto quality = settings.value(QStringLiteral("playback/quality"), "128").toString();
    const QStringList qualities = {"128", "320", "flac", "high", "piano", "acappella",
                                   "subwoofer", "ancient", "surnay", "dj", "viper_atmos",
                                   "viper_clear", "viper_tape", "super"};
    if (qualities.contains(quality))
        m_preferredQuality = quality;
    if (!settings.contains(QStringLiteral("navigation/version")))
    {
        if (settings.contains(QStringLiteral("navigation/page")))
            settings.setValue(QStringLiteral("navigation/page"),
                              qBound(0, settings.value(QStringLiteral("navigation/page")).toInt() + 1, 2));
        settings.setValue(QStringLiteral("navigation/version"), 2);
    }
    m_dataSaver = settings.value(QStringLiteral("playback/dataSaver"), false).toBool();
    m_themeMode =
        settings.value(QStringLiteral("appearance/theme"), QStringLiteral("system")).toString();
    m_activeService = m_serviceUrl = settings.value(QStringLiteral("service/url")).toString().trimmed();
    if (!validateServiceUrl(m_activeService.toString()).isEmpty())
    {
        m_activeService = QUrl();
        m_storageMessage = QStringLiteral("服务配置无效，请在设置中填写 HTTPS 地址");
    }
    if (!m_activeService.isEmpty() && !m_activeService.path().endsWith(QLatin1Char('/')))
        m_activeService.setPath(m_activeService.path() + QLatin1Char('/'));
    if (qGuiApp && qGuiApp->styleHints())
        connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this]
                {
                    if (m_themeMode == QStringLiteral("system"))
                        emit themeModeChanged();
                });
}
QString AppSettings::themeMode() const
{
    return m_themeMode;
}
void AppSettings::setPreferredQuality(const QString &quality)
{
    const QStringList qualities = {"128", "320", "flac", "high", "piano", "acappella",
                                   "subwoofer", "ancient", "surnay", "dj", "viper_atmos",
                                   "viper_clear", "viper_tape", "super"};
    if (!qualities.contains(quality) || quality == m_preferredQuality)
        return;
    m_preferredQuality = quality;
    QSettings().setValue(QStringLiteral("playback/quality"), quality);
    emit playbackPreferencesChanged();
}
void AppSettings::setDataSaver(bool enabled)
{
    if (m_dataSaver == enabled)
        return;
    m_dataSaver = enabled;
    QSettings().setValue(QStringLiteral("playback/dataSaver"), enabled);
    emit playbackPreferencesChanged();
}
bool AppSettings::darkTheme() const
{
    if (m_themeMode == QStringLiteral("dark"))
        return true;
    if (m_themeMode == QStringLiteral("light"))
        return false;
    return qGuiApp && qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}
void AppSettings::setThemeMode(const QString &mode)
{
    if (mode != QStringLiteral("system") && mode != QStringLiteral("light") &&
        mode != QStringLiteral("dark"))
        return;
    if (m_themeMode == mode)
        return;
    m_themeMode = mode;
    QSettings().setValue(QStringLiteral("appearance/theme"), mode);
    emit themeModeChanged();
}
QString AppSettings::serviceUrl() const
{
    return m_serviceUrl;
}
void AppSettings::setServiceUrl(const QString &url)
{
    QString normalized = url.trimmed();
    if (!validateServiceUrl(normalized).isEmpty())
        return;
    if (!normalized.isEmpty())
    {
        QUrl parsed(normalized);
        if (!parsed.path().endsWith(QLatin1Char('/')))
            parsed.setPath(parsed.path() + QLatin1Char('/'));
        normalized = parsed.toString(QUrl::FullyEncoded);
    }
    if (m_serviceUrl == normalized)
        return;
    m_serviceUrl = normalized;
    QSettings().setValue(QStringLiteral("service/url"), normalized);
    if (!m_environmentOverride)
        m_activeService = QUrl(normalized);
    emit serviceUrlChanged();
}
QUrl AppSettings::effectiveServiceUrl() const
{
    return m_activeService;
}
QString AppSettings::validateServiceUrl(const QString &url) const
{
    const QString value = url.trimmed();
    if (value.isEmpty())
        return {};
    const QUrl parsed(value);
    if (!parsed.isValid() || parsed.scheme() != QStringLiteral("https") || parsed.host().isEmpty())
        return QStringLiteral("请输入有效的 HTTPS 服务地址");
    if (!parsed.userInfo().isEmpty() || !parsed.query().isEmpty() || !parsed.fragment().isEmpty())
        return QStringLiteral("服务地址不能包含账号、查询参数或片段");
    return {};
}
qint64 AppSettings::cacheBytes() const
{
    QNetworkDiskCache cache;
    cache.setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                            QStringLiteral("/artwork"));
    return cache.cacheSize();
}
void AppSettings::clearArtworkCache()
{
    QNetworkDiskCache cache;
    cache.setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                            QStringLiteral("/artwork"));
    cache.clear();
    m_storageMessage = QStringLiteral("封面磁盘缓存已清理，历史与队列已保留");
    emit cacheChanged();
}
int AppSettings::lastPage() const
{
    return qBound(0, QSettings().value("navigation/page", 0).toInt(), 2);
}
void AppSettings::setLastPage(int page)
{
    page = qBound(0, page, 2);
    if (page == lastPage())
        return;
    QSettings().setValue("navigation/page", page);
    emit lastPageChanged();
}
void AppSettings::exportDiagnostics(const QUrl &file, const QString &playbackStatus,
                                    const QString &storageError)
{
    if (!file.isLocalFile())
        return;
    QFile output(file.toLocalFile());
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        m_storageMessage = QStringLiteral("无法导出诊断，请选择可写位置");
    else
    {
        const QString text =
            QStringLiteral("Music %1\nQt %2\nOS %3\nService host: %4\nPlayback: %5\nStorage: %6\n")
                .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()),
                     QSysInfo::prettyProductName(), m_activeService.host(), playbackStatus,
                     storageError);
        const auto bytes = text.toUtf8();
        m_storageMessage = output.write(bytes) == bytes.size()
                               ? QStringLiteral("诊断已导出，不含账号、歌曲或媒体地址")
                               : QStringLiteral("诊断文件未完整写入");
    }
    emit cacheChanged();
}
void AppSettings::reportDiagnosticsCancelled()
{
    m_storageMessage = QStringLiteral("已取消导出诊断");
    emit cacheChanged();
}
