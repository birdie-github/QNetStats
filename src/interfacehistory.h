#ifndef INTERFACEHISTORY_H
#define INTERFACEHISTORY_H

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QString>
#include <deque>

class QDialog;
class QWidget;

class InterfaceHistory final : public QAbstractListModel {
public:
    explicit InterfaceHistory(QWidget *parent);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void observe(bool up);
    void showWindow(const QString &name);

private:
    static constexpr int MaximumEvents = 65535;
    std::deque<QString> mEvents;
    bool mKnown = false;
    bool mUp = false;
    bool mFirstUp = false;
    bool mDiscarded = false;
    QElapsedTimer mUptime;
    QDialog *mDialog = nullptr;
};
#endif
