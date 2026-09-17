#pragma once
#include <QAbstractListModel>
#include <QVariantList>

class QueueModel final : public QAbstractListModel
{
    Q_OBJECT
  public:
    explicit QueueModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void synchronize(const QVariantList &rows);

  private:
    QVariantList m_rows;
};
