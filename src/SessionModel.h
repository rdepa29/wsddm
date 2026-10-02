#pragma once

#include <QAbstractListModel>

/// The `sessionModel` context object.
///
/// SDDM lists all installable desktop sessions so the user can pick one at the greeter.
/// A Windows lock overlay has no session concept to switch to, so the theme gets a single
/// entry ("Windows") to keep its picker happy; the index is otherwise unused by the host.
class SessionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit SessionModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QStringList m_sessions;
};