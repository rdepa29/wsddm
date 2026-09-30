#pragma once

#include <QAbstractListModel>

/// The `userModel` context object, a list of the accounts that can unlock the screen.
///
/// The theme reads `count`, `lastIndex` and a "name" role. Windows greets one
/// interactive session, so the signed-in account is the only entry, but the model is a
/// real list model rather than a single hard-coded string so the picker behaves the same
/// way the theme expects.
class UserModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int lastIndex READ lastIndex NOTIFY countChanged)

public:
    explicit UserModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int lastIndex() const { return m_users.size() - 1; }

signals:
    void countChanged();

private:
    QStringList m_users;
};
