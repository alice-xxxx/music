#include "QueueModel.h"

int QueueModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}
QVariant QueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() ||
        role != Qt::UserRole + 1)
        return {};
    return m_rows.at(index.row());
}
QHash<int, QByteArray> QueueModel::roleNames() const
{
    return {{Qt::UserRole + 1, "trackData"}};
}
void QueueModel::synchronize(const QVariantList &rows)
{
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto identity = rows.at(i).toMap().value("queueItemId");
        int existing = i;
        while (existing < m_rows.size() &&
               m_rows.at(existing).toMap().value("queueItemId") != identity)
            ++existing;
        if (existing == m_rows.size())
        {
            beginInsertRows({}, i, i);
            m_rows.insert(i, rows.at(i));
            endInsertRows();
        }
        else if (existing != i)
        {
            beginMoveRows({}, existing, existing, {}, i);
            m_rows.move(existing, i);
            endMoveRows();
        }
        if (m_rows.at(i) != rows.at(i))
        {
            m_rows[i] = rows.at(i);
            emit dataChanged(index(i), index(i), {Qt::UserRole + 1});
        }
    }
    if (m_rows.size() > rows.size())
    {
        beginRemoveRows({}, rows.size(), m_rows.size() - 1);
        m_rows.erase(m_rows.begin() + rows.size(), m_rows.end());
        endRemoveRows();
    }
}
