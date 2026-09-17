#include <QPointer>
#include "SessionManager.h"
#include "api/KuGouApi.h"
QString SessionManager::storageError() const
{
    return m_api->storageError();
}
SessionManager::SessionManager(KuGouApi *api, QObject *parent) : QObject(parent), m_api(api)
{
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &SessionManager::poll);
    // Restoring encrypted local cookies must not call a login/refresh endpoint on every startup.
    m_authenticated = m_api->restoreAuthenticatedSession();
    m_message = !serviceConfigured() ? QStringLiteral("请先填写渠道后端地址")
                                     : m_authenticated
                                           ? QStringLiteral("已恢复本地登录状态")
                                           : QStringLiteral("未登录");
    connect(m_api, &KuGouApi::sessionInvalidated, this,
            [this]
            {
                ++m_generation;
                m_timer.stop();
                m_key.clear();
                m_qrImage.clear();
                m_loading = false;
                m_pollInFlight = false;
                m_pollFailures = 0;
                const bool authenticated = m_api->restoreAuthenticatedSession();
                if (m_authenticated != authenticated)
                {
                    m_authenticated = authenticated;
                    emit authenticatedChanged();
                }
                m_message = !serviceConfigured()
                                ? QStringLiteral("请先填写渠道后端地址")
                                : m_authenticated ? QStringLiteral("已恢复本地登录状态")
                                                  : QStringLiteral("未登录");
                emit stateChanged();
            });
}
bool SessionManager::serviceConfigured() const
{
    return m_api->serviceConfigured();
}
bool SessionManager::loginWithCookie(const QString &cookie)
{
    cancelQrLogin();
    QString error;
    const bool authenticated = m_api->loginWithCookies(cookie, &error);
    if (!authenticated)
    {
        m_message = error.isEmpty() ? QStringLiteral("Cookie 无法建立登录会话") : error;
        emit stateChanged();
        return false;
    }
    if (!m_authenticated)
    {
        m_authenticated = true;
        emit authenticatedChanged();
    }
    m_message = QStringLiteral("Cookie 登录成功");
    emit stateChanged();
    return true;
}
void SessionManager::startQrLogin()
{
    cancelQrLogin();
    if (!serviceConfigured())
    {
        m_message = QStringLiteral("请先填写渠道后端地址");
        emit stateChanged();
        return;
    }
    const quint64 generation = m_generation;
    m_loading = true;
    m_message = QStringLiteral("正在生成二维码…");
    emit stateChanged();
    m_api->createLoginQr(
        [this, guard = QPointer<SessionManager>(this), generation](QString key, QString image,
                                                                   QString error)
        {
            if (!guard || generation != m_generation)
                return;
            m_loading = false;
            if (!error.isEmpty())
            {
                m_message = error;
                emit stateChanged();
                return;
            }
            m_key = key;
            m_qrImage = image;
            m_message = QStringLiteral("请使用酷狗 App 扫码登录");
            m_timer.start();
            emit stateChanged();
        });
}
void SessionManager::cancelQrLogin()
{
    ++m_generation;
    m_api->cancelLogin();
    m_timer.stop();
    m_key.clear();
    m_qrImage.clear();
    m_loading = false;
    m_pollInFlight = false;
    m_pollFailures = 0;
    if (!m_authenticated)
        m_message = serviceConfigured() ? QStringLiteral("未登录")
                                        : QStringLiteral("请先填写渠道后端地址");
    emit stateChanged();
}
void SessionManager::poll()
{
    if (m_key.isEmpty() || m_pollInFlight)
        return;
    const quint64 generation = m_generation;
    m_pollInFlight = true;
    m_api->checkLoginQr(
        m_key,
        [this, guard = QPointer<SessionManager>(this), generation](int status, QString error)
        {
            if (!guard || generation != m_generation)
                return;
            m_pollInFlight = false;
            if (!error.isEmpty())
            {
                m_message = error;
                if (++m_pollFailures >= 3)
                {
                    m_timer.stop();
                    m_message = QStringLiteral("暂时无法确认登录，请检查网络后刷新二维码");
                }
            }
            else if (status == 0)
            {
                m_timer.stop();
                m_message = QStringLiteral("二维码已过期，请刷新");
            }
            else if (status == 1)
                m_message = QStringLiteral("等待扫码…");
            else if (status == 2)
                m_message = QStringLiteral("已扫码，请在手机确认");
            else if (status == 4)
            {
                m_timer.stop();
                const bool authenticated = m_api->beginAuthenticatedSession();
                if (m_authenticated != authenticated)
                {
                    m_authenticated = authenticated;
                    emit authenticatedChanged();
                }
                m_message = m_authenticated ? QStringLiteral("登录成功，可以播放推荐和搜索结果")
                                            : QStringLiteral("登录凭据未保存，请刷新二维码重试");
            }
            if (error.isEmpty())
                m_pollFailures = 0;
            emit stateChanged();
        });
}
void SessionManager::logout()
{
    cancelQrLogin();
    m_api->logout();
    if (m_authenticated)
    {
        m_authenticated = false;
        emit authenticatedChanged();
    }
    m_message = QStringLiteral("已退出登录");
    emit stateChanged();
}
