#include "SessionModel.h"

SessionModel::SessionModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_sessions = {QStringLiteral("Windows")};
}

int SessionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_sessions.size();
}

QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sessions.size())
        return {};

    if (role == Qt::DisplayRole || role == Qt::UserRole)
        return m_sessions.at(index.row());

    return {};
}

QHash<int, QByteArray> SessionModel::roleNames() const
{
    return {{Qt::DisplayRole, "name"}, {Qt::UserRole, "key"}};
}