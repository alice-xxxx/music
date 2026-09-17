#pragma once

#include <QObject>
#include <QUrl>

class AppSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY themeModeChanged)
    Q_PROPERTY(QString serviceUrl READ serviceUrl WRITE setServiceUrl NOTIFY serviceUrlChanged)
    Q_PROPERTY(QString activeServiceUrl READ activeServiceUrl NOTIFY serviceUrlChanged)
    Q_PROPERTY(bool environmentOverride READ environmentOverride CONSTANT)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(QString storageMessage READ storageMessage NOTIFY cacheChanged)
    Q_PROPERTY(int lastPage READ lastPage WRITE setLastPage NOTIFY lastPageChanged)
    Q_PROPERTY(QString preferredQuality READ preferredQuality WRITE setPreferredQuality NOTIFY
                   playbackPreferencesChanged)
    Q_PROPERTY(bool dataSaver READ dataSaver WRITE setDataSaver NOTIFY playbackPreferencesChanged)
  public:
    explicit AppSettings(QObject *parent = nullptr);
    QString themeMode() const;
    bool darkTheme() const;
    void setThemeMode(const QString &mode);
    QString serviceUrl() const;
    void setServiceUrl(const QString &url);
    QUrl effectiveServiceUrl() const;
    QString activeServiceUrl() const
    {
        return m_activeService.toString();
    }
    bool environmentOverride() const
    {
        return m_environmentOverride;
    }
    qint64 cacheBytes() const;
    QString storageMessage() const
    {
        return m_storageMessage;
    }
    int lastPage() const;
    void setLastPage(int page);
    QString preferredQuality() const
    {
        return m_preferredQuality;
    }
    void setPreferredQuality(const QString &quality);
    bool dataSaver() const
    {
        return m_dataSaver;
    }
    void setDataSaver(bool enabled);
    QString effectiveQuality() const
    {
        return m_dataSaver ? QStringLiteral("128") : m_preferredQuality;
    }
    Q_INVOKABLE void clearArtworkCache();
    Q_INVOKABLE void exportDiagnostics(const QUrl &file, const QString &playbackStatus,
                                        const QString &storageError);
    Q_INVOKABLE void reportDiagnosticsCancelled();
    Q_INVOKABLE QString validateServiceUrl(const QString &url) const;
  signals:
    void themeModeChanged();
    void serviceUrlChanged();
    void cacheChanged();
    void lastPageChanged();
    void playbackPreferencesChanged();

  private:
    QString m_themeMode;
    QString m_serviceUrl;
    QUrl m_activeService;
    bool m_environmentOverride = false;
    QString m_storageMessage;
    QString m_preferredQuality = QStringLiteral("128");
    bool m_dataSaver = false;
};
