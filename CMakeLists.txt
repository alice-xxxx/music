cmake_minimum_required(VERSION 3.21)

project(MusicApp VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Multimedia Network Sql)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

if(ANDROID)
    foreach(variable QT_ANDROID_KEYSTORE_PATH QT_ANDROID_KEYSTORE_ALIAS
                     QT_ANDROID_KEYSTORE_STORE_PASS QT_ANDROID_KEYSTORE_KEY_PASS)
        if("$ENV{${variable}}" STREQUAL "")
            message(FATAL_ERROR "Android Release signing requires environment variable ${variable}")
        endif()
        set(${variable} "$ENV{${variable}}")
    endforeach()
    set(QT_ANDROID_SIGN_APK ON)
endif()

qt_add_executable(MusicApp
    src/app/main.cpp
    src/api/ApiClient.cpp src/api/ApiClient.h
    src/api/KuGouApi.cpp src/api/KuGouApi.h
    src/domain/Track.cpp src/domain/Track.h
    src/features/account/SessionManager.cpp src/features/account/SessionManager.h
    src/features/catalog/CatalogService.cpp src/features/catalog/CatalogService.h
    src/features/catalog/CollectionViewModel.cpp src/features/catalog/CollectionViewModel.h
    src/features/library/FavoritesController.cpp src/features/library/FavoritesController.h
    src/features/library/LibraryViewModel.cpp src/features/library/LibraryViewModel.h
    src/features/playback/AudioEngine.cpp src/features/playback/AudioEngine.h
    src/features/playback/LyricsModel.cpp src/features/playback/LyricsModel.h
    src/features/playback/MediaStreamRelay.cpp src/features/playback/MediaStreamRelay.h
    src/features/playback/PlaybackController.cpp src/features/playback/PlaybackController.h
    src/features/playback/QueueModel.cpp src/features/playback/QueueModel.h
    src/features/search/SearchViewModel.cpp src/features/search/SearchViewModel.h
    src/features/settings/AppSettings.cpp src/features/settings/AppSettings.h
    src/platform/BackgroundPlayback.cpp src/platform/BackgroundPlayback.h
    src/platform/CredentialCookieJar.cpp src/platform/CredentialCookieJar.h
    src/storage/LocalStore.cpp src/storage/LocalStore.h)
set_target_properties(MusicApp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_include_directories(MusicApp PRIVATE src)
target_link_libraries(MusicApp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Network Qt6::Sql
    Qt6::Quick Qt6::QuickControls2)

if(WIN32)
    file(GLOB music_app_cppwinrt_paths
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Include/*/cppwinrt"
        "$ENV{ProgramFiles}/Windows Kits/10/Include/*/cppwinrt")
    list(SORT music_app_cppwinrt_paths COMPARE NATURAL ORDER DESCENDING)
    if(music_app_cppwinrt_paths)
        list(GET music_app_cppwinrt_paths 0 music_app_cppwinrt_path)
        target_sources(MusicApp PRIVATE src/platform/BackgroundPlayback_windows.cpp)
        target_include_directories(MusicApp PRIVATE "${music_app_cppwinrt_path}")
        target_link_libraries(MusicApp PRIVATE runtimeobject ole32)
        target_compile_definitions(MusicApp PRIVATE WINRT_LEAN_AND_MEAN NOMINMAX)
    else()
        message(WARNING "Windows SDK C++/WinRT headers were not found; Windows SMTC is disabled")
    endif()
endif()

set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(MusicApp
    URI MusicApp
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/IconButton.qml
        qml/components/NavigationTab.qml
        qml/components/CoverImage.qml
        qml/components/PlayerBar.qml
        qml/components/SongRow.qml
        qml/components/QueueDrawer.qml
        qml/pages/PlaylistPicker.qml
        qml/pages/CollectionPage.qml
        qml/pages/LibraryPage.qml
        qml/pages/SearchPage.qml
        qml/theme/Theme.qml
        qml/pages/LoginDialog.qml
        qml/pages/NowPlayingPage.qml
        qml/pages/SettingsPage.qml
    RESOURCES
        qml/icons/back.svg
        qml/icons/close.svg
        qml/icons/heart.svg
        qml/icons/lyrics.svg
        qml/icons/more.svg
        qml/icons/next.svg
        qml/icons/pause.svg
        qml/icons/play.svg
        qml/icons/previous.svg
        qml/icons/queue.svg
        qml/icons/repeat-one.svg
        qml/icons/repeat.svg
        qml/icons/retry.svg
        qml/icons/shuffle.svg)

# Keep the build and packaging contract in this single project entrypoint.
option(MUSIC_APP_OPTIMIZE_RELEASE "Use balanced O2 release optimization" ON)
option(MUSIC_APP_DEPLOY_RUNTIME "Install desktop Qt/QML runtime dependencies" OFF)
if(MUSIC_APP_OPTIMIZE_RELEASE)
    target_compile_options(MusicApp PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O2>"
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>")
endif()

if(APPLE)
    enable_language(OBJCXX)
    find_library(MUSIC_APP_AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(MUSIC_APP_MEDIAPLAYER_FRAMEWORK MediaPlayer REQUIRED)
    target_sources(MusicApp PRIVATE src/platform/BackgroundPlayback_ios.mm)
    target_link_libraries(MusicApp PRIVATE
        "${MUSIC_APP_AVFOUNDATION_FRAMEWORK}" "${MUSIC_APP_MEDIAPLAYER_FRAMEWORK}")
    set_target_properties(MusicApp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.musicclient.app"
        MACOSX_BUNDLE_BUNDLE_NAME "Music"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(Qt6 QUIET COMPONENTS DBus)
    if(TARGET Qt6::DBus)
        target_sources(MusicApp PRIVATE src/platform/BackgroundPlayback_linux.cpp)
        target_link_libraries(MusicApp PRIVATE Qt6::DBus)
        target_compile_definitions(MusicApp PRIVATE MUSIC_APP_HAS_DBUS)
    else()
        message(WARNING "Qt DBus was not found; Linux MPRIS media controls are disabled")
    endif()
endif()

if(ANDROID)
    set(MUSIC_APP_ANDROID_OPENSSL_ROOT "" CACHE PATH "Pinned KDAB android_openssl checkout")
    if(NOT EXISTS "${MUSIC_APP_ANDROID_OPENSSL_ROOT}/android_openssl.cmake")
        message(FATAL_ERROR "Android HTTPS requires MUSIC_APP_ANDROID_OPENSSL_ROOT")
    endif()
    include("${MUSIC_APP_ANDROID_OPENSSL_ROOT}/android_openssl.cmake")
    add_android_openssl_libraries(MusicApp)
    set(MUSIC_APP_BUILD_NUMBER 1 CACHE STRING "Monotonic Android version code")
    set_target_properties(MusicApp PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "io.github.musicclient.app"
        QT_ANDROID_APP_NAME "音乐"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_COMPILE_SDK_VERSION 36
        QT_ANDROID_SDK_BUILD_TOOLS_REVISION "36.0.0"
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_VERSION_CODE "${MUSIC_APP_BUILD_NUMBER}"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}")
elseif(UNIX AND NOT APPLE)
    set_target_properties(MusicApp PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()

if(IOS)
    set_target_properties(MusicApp PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
        QT_IOS_LAUNCH_SCREEN "${CMAKE_CURRENT_SOURCE_DIR}/ios/LaunchScreen.storyboard")
    qt6_add_ios_ffmpeg_libraries(MusicApp)
    install(TARGETS MusicApp BUNDLE DESTINATION Payload)
else()
    install(TARGETS MusicApp BUNDLE DESTINATION . LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()

if(MUSIC_APP_DEPLOY_RUNTIME AND NOT ANDROID AND NOT IOS)
    set(music_app_deploy_options "")
    if(APPLE)
        list(APPEND music_app_deploy_options DEPLOY_TOOL_OPTIONS -no-codesign)
    endif()
    qt_generate_deploy_qml_app_script(TARGET MusicApp OUTPUT_SCRIPT music_app_deploy_script
        ${music_app_deploy_options})
    install(SCRIPT "${music_app_deploy_script}")
endif()

if(NOT ANDROID)
    set(MUSIC_BUILD_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
        CACHE STRING "Release artifact platform name")
    if(IOS)
        set(music_ios_stage "${CMAKE_BINARY_DIR}/ios-package")
        set(music_ios_artifact
            "${CMAKE_BINARY_DIR}/artifacts/Music-${MUSIC_BUILD_NAME}-unsigned.ipa")
        add_custom_target(package
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${music_ios_stage}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${music_ios_stage}/Payload" "${CMAKE_BINARY_DIR}/artifacts"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:MusicApp>"
                    "${music_ios_stage}/Payload/MusicApp.app"
            COMMAND "${CMAKE_COMMAND}" -E chdir "${music_ios_stage}"
                    "${CMAKE_COMMAND}" -E tar cf "${music_ios_artifact}" --format=zip -- Payload
            DEPENDS MusicApp
            BYPRODUCTS "${music_ios_artifact}"
            VERBATIM)
    else()
        set(CPACK_PACKAGE_NAME "Music")
        set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
        set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/artifacts")
        set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
        if(WIN32 OR APPLE)
            set(CPACK_GENERATOR ZIP)
        else()
            set(CPACK_GENERATOR TGZ)
        endif()
        if(APPLE)
            set(CPACK_PACKAGE_FILE_NAME "Music-${MUSIC_BUILD_NAME}-unsigned")
        else()
            set(CPACK_PACKAGE_FILE_NAME "Music-${MUSIC_BUILD_NAME}")
        endif()
        include(CPack)
    endif()
endif()
