#pragma once
#include <QAbstractListModel>

class LyricsModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
  public:
    enum Role
    {
        TextRole = Qt::UserRole + 1,
        TimeRole
    };
    explicit LyricsModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int currentIndex() const
    {
        return m_currentIndex;
    }
    void setLyrics(const QString &text);
    void setPosition(qint64 position);
  signals:
    void currentIndexChanged();

  private:
    struct Line
    {
        qint64 time;
        QString text;
    };
    QList<Line> m_lines;
    int m_currentIndex = -1;
};
