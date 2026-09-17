#pragma once
#include <QObject>
#include <QHash>
class QThread;

class LocalStore final : public QObject
{
    Q_OBJECT
  public:
    explicit LocalStore(QString path, QObject *parent = nullptr);
    ~LocalStore() override;
    void load(const QString &scope);
    void save(const QString &scope, const QByteArray &snapshot);
  signals:
    void loaded(const QString &scope, const QByteArray &snapshot, const QString &error);
    void saved(const QString &scope, const QString &error);

  private:
    void execute(QString scope, QByteArray snapshot, bool write, quint64 loadGeneration = 0);
    QHash<QString, quint64> m_loadGenerations;
    QString m_path;
    QThread *m_thread;
    QObject *m_worker;
};
