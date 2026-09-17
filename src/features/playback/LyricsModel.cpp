#include "LyricsModel.h"
#include <QRegularExpression>
#include <algorithm>

int LyricsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_lines.size();
}
QVariant LyricsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size())
        return {};
    const auto &line = m_lines.at(index.row());
    if (role == TextRole)
        return line.text;
    if (role == TimeRole)
        return line.time;
    return {};
}
QHash<int, QByteArray> LyricsModel::roleNames() const
{
    return {{TextRole, "lineText"}, {TimeRole, "timeMs"}};
}
void LyricsModel::setLyrics(const QString &text)
{
    static const QRegularExpression stamp(QStringLiteral(R"(\[(\d+):([0-5]\d)(?:\.(\d{1,3}))?\])"));
    static const QRegularExpression offsetTag(QStringLiteral(R"(\[offset:([+-]?\d+)\])"),
                                              QRegularExpression::CaseInsensitiveOption);
    const auto offsetMatch = offsetTag.match(text);
    const qint64 offset = offsetMatch.hasMatch() ? offsetMatch.captured(1).toLongLong() : 0;
    QList<Line> lines;
    for (const QString &raw : text.split(QLatin1Char('\n')))
    {
        auto matches = stamp.globalMatch(raw);
        QList<qint64> times;
        qsizetype end = 0;
        while (matches.hasNext())
        {
            const auto match = matches.next();
            const qint64 minutes = match.captured(1).toLongLong();
            if (minutes > 1000000)
                continue;
            const qint64 millis = match.captured(3).leftJustified(3, QLatin1Char('0')).toLongLong();
            times.append(minutes * 60000 + match.captured(2).toLongLong() * 1000 + millis -
                         qBound<qint64>(-86400000LL, offset, 86400000LL));
            end = match.capturedEnd();
        }
        const QString words = raw.mid(end).trimmed();
        for (qint64 time : times)
            lines.append({time, words});
    }
    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line &a, const Line &b) { return a.time < b.time; });
    beginResetModel();
    m_lines = std::move(lines);
    m_currentIndex = -1;
    endResetModel();
    emit currentIndexChanged();
}
void LyricsModel::setPosition(qint64 position)
{
    const auto next =
        std::upper_bound(m_lines.cbegin(), m_lines.cend(), position,
                         [](qint64 time, const Line &line) { return time < line.time; });
    const int index = static_cast<int>(std::distance(m_lines.cbegin(), next)) - 1;
    if (index == m_currentIndex)
        return;
    m_currentIndex = index;
    emit currentIndexChanged();
}
