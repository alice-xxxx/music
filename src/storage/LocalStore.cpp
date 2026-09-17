#include "LocalStore.h"
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>

LocalStore::LocalStore(QString path, QObject *parent)
    : QObject(parent), m_path(std::move(path)), m_thread(new QThread), m_worker(new QObject)
{
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}
LocalStore::~LocalStore()
{
    // Drain previously queued writes before asking this worker to stop.
    QMetaObject::invokeMethod(m_worker, [thread = m_thread] { thread->quit(); });
    m_thread->wait(3000);
}
void LocalStore::load(const QString &scope)
{
    execute(scope, {}, false, ++m_loadGenerations[scope]);
}
void LocalStore::save(const QString &scope, const QByteArray &snapshot)
{
    execute(scope, snapshot, true);
}
void LocalStore::execute(QString scope, QByteArray snapshot, bool write, quint64 loadGeneration)
{
    QMetaObject::invokeMethod(
        m_worker,
        [guard = QPointer<LocalStore>(this), path = m_path, scope = std::move(scope),
         snapshot = std::move(snapshot), write, loadGeneration]() mutable
        {
            QString error;
            QByteArray result;
            const auto connection = QUuid::createUuid().toString();
            {
                QDir().mkpath(QFileInfo(path).absolutePath());
                auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
                db.setDatabaseName(path);
                db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=1000"));
                if (!db.open())
                    error = QStringLiteral("无法打开音乐数据，请检查磁盘和目录权限");
                else
                {
                    QSqlQuery query(db);
                    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next() ||
                        query.value(0).toInt() > 1)
                        error = QStringLiteral("音乐数据版本不兼容，已保留原文件");
                    else if (!query.exec(
                                 QStringLiteral("CREATE TABLE IF NOT EXISTS snapshots(scope TEXT "
                                                "PRIMARY KEY, data BLOB NOT NULL)")) ||
                             !query.exec(QStringLiteral("PRAGMA user_version=1")))
                        error = QStringLiteral("音乐数据初始化失败，已保留原文件");
                    else if (write)
                    {
                        if (!db.transaction())
                            error = QStringLiteral("无法开始保存音乐数据");
                        else
                        {
                            query.prepare(
                                QStringLiteral("INSERT INTO snapshots(scope,data) VALUES(?,?) ON "
                                               "CONFLICT(scope) DO UPDATE SET data=excluded.data"));
                            query.addBindValue(scope);
                            query.addBindValue(snapshot);
                            if (!query.exec() || !db.commit())
                            {
                                db.rollback();
                                error = QStringLiteral("音乐数据未保存，请检查剩余磁盘空间");
                            }
                        }
                    }
                    else
                    {
                        query.prepare(QStringLiteral("SELECT data FROM snapshots WHERE scope=?"));
                        query.addBindValue(scope);
                        if (!query.exec())
                            error = QStringLiteral("无法读取音乐数据，已保留原文件");
                        else if (query.next())
                            result = query.value(0).toByteArray();
                    }
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(connection);
            if (guard)
                QMetaObject::invokeMethod(guard,
                                          [guard, scope, result, error, write, loadGeneration]
                                          {
                                              if (!guard)
                                                  return;
                                              if (write)
                                                  emit guard->saved(scope, error);
                                              else if (loadGeneration ==
                                                       guard->m_loadGenerations.value(scope))
                                                  emit guard->loaded(scope, result, error);
                                          });
        });
}
