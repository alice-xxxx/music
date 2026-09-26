#include "CommentsViewModel.h"

#include "api/KuGouApi.h"
#include <QPointer>

CommentsViewModel::CommentsViewModel(KuGouApi *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    connect(m_api, &KuGouApi::sessionInvalidated, this, &CommentsViewModel::invalidateSession);
}

void CommentsViewModel::open(const QString &kind, const QString &id, const QString &title)
{
    ++m_targetGeneration;
    ++m_readGeneration;
    m_kind = kind;
    m_id = id;
    m_title = title;
    m_comments.clear();
    m_error.clear();
    m_sendMessage.clear();
    m_loading = false;
    m_hasMore = false;
    m_sending = false;
    m_sendUncertain = false;
    m_page = 0;
    emit changed();
    reload();
}

void CommentsViewModel::reload()
{
    if (m_id.isEmpty())
        return;
    ++m_readGeneration;
    m_comments.clear();
    m_hasMore = false;
    m_page = 0;
    requestPage(1);
}

void CommentsViewModel::loadMore()
{
    if (!m_loading && m_hasMore)
        requestPage(m_page + 1);
}

void CommentsViewModel::requestPage(int page)
{
    m_loading = true;
    m_error.clear();
    emit changed();
    const quint64 targetGeneration = m_targetGeneration;
    const quint64 readGeneration = m_readGeneration;
    m_api->comments(
        m_kind, m_id,
        [this, guard = QPointer<CommentsViewModel>(this), targetGeneration, readGeneration,
         page](QVariantList rows, QString code, QString message)
        {
            if (!guard || targetGeneration != m_targetGeneration ||
                readGeneration != m_readGeneration)
                return;
            m_loading = false;
            if (!code.isEmpty())
            {
                m_error = message;
                emit changed();
                return;
            }
            m_page = page;
            m_hasMore = rows.size() == 30;
            m_comments.append(rows);
            emit changed();
        },
        page);
}

void CommentsViewModel::send(const QString &content)
{
    if (m_sending || m_sendUncertain || m_id.isEmpty())
        return;
    if (!m_api->authenticated())
    {
        emit loginRequired();
        return;
    }
    m_sending = true;
    m_sendMessage = QStringLiteral("正在发布…");
    emit changed();
    const quint64 generation = m_targetGeneration;
    m_api->sendComment(
        m_kind, m_id, m_title, content,
        [this, guard = QPointer<CommentsViewModel>(this), generation](QString code,
                                                                      QString message)
        {
            if (!guard || generation != m_targetGeneration)
                return;
            m_sending = false;
            if (code.isEmpty())
            {
                m_sendMessage = QStringLiteral("已提交，评论可能需要审核后显示");
                emit sent();
                reload();
            }
            else if (code == QStringLiteral("Timeout") ||
                     code == QStringLiteral("Unavailable") ||
                     code == QStringLiteral("UnknownResult"))
            {
                m_sendUncertain = true;
                m_sendMessage = QStringLiteral("提交结果未知，请先到酷狗核对；不会自动重发");
            }
            else
                m_sendMessage = message;
            emit changed();
        });
}

void CommentsViewModel::invalidateSession()
{
    ++m_targetGeneration;
    ++m_readGeneration;
    m_kind.clear();
    m_id.clear();
    m_title.clear();
    m_comments.clear();
    m_loading = false;
    m_hasMore = false;
    m_sending = false;
    m_sendUncertain = false;
    m_page = 0;
    m_error.clear();
    m_sendMessage.clear();
    emit changed();
}
