#include "api/KuGouApi.h"
#include "features/catalog/CatalogService.h"
#include "features/catalog/CollectionViewModel.h"
#include "features/account/SessionManager.h"
#include "features/library/LibraryViewModel.h"
#include "features/library/FavoritesController.h"
#include "features/playback/PlaybackController.h"
#include "features/search/SearchViewModel.h"
#include "features/settings/AppSettings.h"

#include <QGuiApplication>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSettings>
#include "storage/LocalStore.h"
#ifdef Q_OS_WIN
#include "platform/WindowsMediaCommands.h"
#endif
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlNetworkAccessManagerFactory>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace
{
class ArtworkNetworkFactory final : public QQmlNetworkAccessManagerFactory
{
  public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *network = new QNetworkAccessManager(parent);
        auto *cache = new QNetworkDiskCache(network);
        cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                                 QStringLiteral("/artwork"));
        cache->setMaximumCacheSize(200 * 1024 * 1024);
        network->setCache(cache);
        return network;
    }
};

QString legacyProductName()
{
    // Constructed only for one-time migration; this is never shown as the product name.
    return QStringLiteral("Ku") + QStringLiteral("Gou");
}

void migrateLegacyState()
{
    const QString legacyOrganization = legacyProductName() + QStringLiteral("Qt");
    const QString legacyApplication = legacyProductName();
    QSettings current;
    if (!current.value(QStringLiteral("migration/legacyProductIdentityImported"), false).toBool())
    {
        QSettings legacy(legacyOrganization, legacyApplication);
        for (const QString &key : legacy.allKeys())
            if (!current.contains(key))
                current.setValue(key, legacy.value(key));
        current.setValue(QStringLiteral("migration/legacyProductIdentityImported"), true);
        current.sync();
    }

    const QString currentDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QCoreApplication::setOrganizationName(legacyOrganization);
    QCoreApplication::setApplicationName(legacyApplication);
    const QString legacyDatabase =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
        QStringLiteral("/music.sqlite");
    QCoreApplication::setOrganizationName(QStringLiteral("MusicClient"));
    QCoreApplication::setApplicationName(QStringLiteral("Music"));
    const QString currentDatabase = currentDirectory + QStringLiteral("/music.sqlite");
    if (!QFile::exists(currentDatabase) && QFile::exists(legacyDatabase))
    {
        QDir().mkpath(currentDirectory);
        if (!QFile::copy(legacyDatabase, currentDatabase))
            qWarning("Could not migrate the existing music library database");
    }
}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCoreApplication::setApplicationName(QStringLiteral("Music"));
    QCoreApplication::setOrganizationName(QStringLiteral("MusicClient"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    migrateLegacyState();

    // App-scoped state is created here so navigation never recreates playback.
    AppSettings appSettings;
    KuGouApi kugouApi(appSettings.effectiveServiceUrl());
    QObject::connect(&appSettings, &AppSettings::serviceUrlChanged, &kugouApi,
                     [&] { kugouApi.setServiceBase(appSettings.effectiveServiceUrl()); });
    SessionManager sessionManager(&kugouApi);
    CatalogService catalog(&kugouApi);
    SearchViewModel searchViewModel(&kugouApi, &catalog);
    LocalStore store(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                     QStringLiteral("/music.sqlite"));
    PlaybackController playbackController(&kugouApi, &catalog, &store);
    playbackController.setRequestedQuality(appSettings.effectiveQuality());
    QObject::connect(&appSettings, &AppSettings::playbackPreferencesChanged, &playbackController,
                     [&]
                     { playbackController.setRequestedQuality(appSettings.effectiveQuality()); });
#ifdef Q_OS_WIN
    WindowsMediaCommands mediaCommands(&playbackController);
    app.installNativeEventFilter(&mediaCommands);
#endif
    LibraryViewModel libraryViewModel(&kugouApi, &catalog);
    CollectionViewModel collection(&kugouApi, &catalog);
    FavoritesController favorites(&kugouApi, &sessionManager);
    QObject::connect(&favorites, &FavoritesController::resumeTrackRequested, &playbackController,
                     [&](const QVariantMap &track)
                     { playbackController.enqueueSong(track, false); });
    QObject::connect(&playbackController, &PlaybackController::snapshotChanged, &favorites,
                     [&] { favorites.setCurrentTrack(playbackController.currentTrack()); });
    QObject::connect(&sessionManager, &SessionManager::authenticatedChanged, &searchViewModel,
                     [&]
                     {
                         if (sessionManager.authenticated() &&
                             !searchViewModel.query().trimmed().isEmpty())
                             searchViewModel.submitSearch();
                     });
    ArtworkNetworkFactory artworkNetwork;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&artworkNetwork);
    engine.setInitialProperties(
        {{QStringLiteral("collectionViewModel"), QVariant::fromValue(&collection)},
         {QStringLiteral("favorites"), QVariant::fromValue(&favorites)},
         {QStringLiteral("searchViewModel"), QVariant::fromValue(&searchViewModel)},
         {QStringLiteral("playbackController"), QVariant::fromValue(&playbackController)},
         {QStringLiteral("sessionManager"), QVariant::fromValue(&sessionManager)},
         {QStringLiteral("appSettings"), QVariant::fromValue(&appSettings)},
         {QStringLiteral("libraryViewModel"), QVariant::fromValue(&libraryViewModel)}});
    engine.loadFromModule(QStringLiteral("MusicApp"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;
    return app.exec();
}
