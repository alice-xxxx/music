#include "api/KuGouApi.h"
#include "features/catalog/CatalogService.h"
#include "features/catalog/CollectionViewModel.h"
#include "features/catalog/CommentsViewModel.h"
#include "features/catalog/DiscoverViewModel.h"
#include "features/account/SessionManager.h"
#include "features/library/LibraryViewModel.h"
#include "features/library/FavoritesController.h"
#include "features/playback/PlaybackController.h"
#include "features/search/SearchViewModel.h"
#include "features/settings/AppSettings.h"
#include "platform/BackgroundPlayback.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQuickStyle>
#include <QStandardPaths>
#include "storage/LocalStore.h"
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

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCoreApplication::setApplicationName(QStringLiteral("Music"));
    QCoreApplication::setOrganizationName(QStringLiteral("MusicClient"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/app-icon.png")));

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
    BackgroundPlayback backgroundPlayback;
    QObject::connect(&playbackController, &PlaybackController::playbackTransitionStarted,
                     &backgroundPlayback, &BackgroundPlayback::beginPlaybackTransition);
    QObject::connect(&playbackController, &PlaybackController::playbackStarting,
                     &backgroundPlayback, &BackgroundPlayback::preparePlayback);
    const auto synchronizeMediaSession = [&]
    {
        backgroundPlayback.update(
            playbackController.hasCurrentTrack(), playbackController.desiredPlaying(),
            playbackController.playing(), playbackController.preparing(),
            !playbackController.errorMessage().isEmpty(), playbackController.seekable(),
            playbackController.canSkipNext(), playbackController.canSkipPrevious(),
            playbackController.position(),
            playbackController.duration(), playbackController.title(), playbackController.artist(),
            playbackController.coverUrl());
    };
    QObject::connect(&playbackController, &PlaybackController::snapshotChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&playbackController, &PlaybackController::positionChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&playbackController, &PlaybackController::durationChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&playbackController, &PlaybackController::audioMetadataChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&playbackController, &PlaybackController::seekableChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&playbackController, &PlaybackController::errorChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&playbackController, &PlaybackController::queueChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&playbackController, &PlaybackController::repeatModeChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&playbackController, &PlaybackController::shuffleChanged,
                     &backgroundPlayback, synchronizeMediaSession);
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::playRequested,
                     &playbackController,
                     [&]
                     {
                         if (!playbackController.desiredPlaying())
                             playbackController.togglePlayback();
                     });
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::pauseRequested,
                     &playbackController,
                     [&]
                     {
                         if (playbackController.desiredPlaying())
                             playbackController.togglePlayback();
                         else
                             playbackController.pauseForInterruption(false);
                     });
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::toggleRequested,
                     &playbackController, &PlaybackController::togglePlayback);
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::nextRequested,
                     &playbackController, &PlaybackController::next);
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::previousRequested,
                     &playbackController, &PlaybackController::previous);
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::seekRequested,
                     &playbackController, &PlaybackController::seek);
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::interruptionBegan,
                     &playbackController, &PlaybackController::pauseForInterruption);
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::interruptionEnded,
                     &playbackController, &PlaybackController::resumeAfterInterruption);
    QObject::connect(&backgroundPlayback, &BackgroundPlayback::outputDisconnected,
                     &playbackController, &PlaybackController::pauseForOutputLoss);
    synchronizeMediaSession();
    playbackController.setRequestedQuality(appSettings.effectiveQuality());
    QObject::connect(&appSettings, &AppSettings::playbackPreferencesChanged, &playbackController,
                     [&]
                     { playbackController.setRequestedQuality(appSettings.effectiveQuality()); });
    LibraryViewModel libraryViewModel(&kugouApi, &catalog);
    CollectionViewModel collection(&kugouApi, &catalog);
    CommentsViewModel comments(&kugouApi);
    DiscoverViewModel discover(&kugouApi, &catalog);
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
         {QStringLiteral("commentsViewModel"), QVariant::fromValue(&comments)},
         {QStringLiteral("favorites"), QVariant::fromValue(&favorites)},
         {QStringLiteral("searchViewModel"), QVariant::fromValue(&searchViewModel)},
          {QStringLiteral("discoverViewModel"), QVariant::fromValue(&discover)},
         {QStringLiteral("playbackController"), QVariant::fromValue(&playbackController)},
         {QStringLiteral("sessionManager"), QVariant::fromValue(&sessionManager)},
         {QStringLiteral("appSettings"), QVariant::fromValue(&appSettings)},
         {QStringLiteral("libraryViewModel"), QVariant::fromValue(&libraryViewModel)}});
    engine.loadFromModule(QStringLiteral("MusicApp"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;
    synchronizeMediaSession();
    return app.exec();
}
