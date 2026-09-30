#include "UserModel.h"

#include <QStringList>

namespace {
/// Environment::USERNAME can be a bare name or DOMAIN\name; the theme only wants the
/// part a person recognises.
QString displayName()
{
    const QString name =
#ifdef Q_OS_WIN
        qEnvironmentVariable("USERNAME");
#else
        qEnvironmentVariable("USER");
#endif

    const int slash = name.lastIndexOf(QLatin1Char('\\'));
    return slash >= 0 ? name.mid(slash + 1) : name;
}
} // namespace

UserModel::UserModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const QString name = displayName();
    if (!name.isEmpty())
        m_users.append(name);
}

int UserModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_users.size();
}

QVariant UserModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_users.size())
        return {};

    if (role == Qt::DisplayRole)
        return m_users.at(index.row());

    return {};
}

QHash<int, QByteArray> UserModel::roleNames() const
{
    // userPicker declares textRole: "name", so the display role must be named to match.
    return {{Qt::DisplayRole, "name"}};
}
