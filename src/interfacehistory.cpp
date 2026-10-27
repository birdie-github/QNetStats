#include "interfacehistory.h"

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QListView>
#include <QScrollBar>
#include <QVBoxLayout>

InterfaceHistory::InterfaceHistory(QWidget *parent) : QAbstractListModel(parent) {}

int InterfaceHistory::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(mEvents.size());
}

QVariant InterfaceHistory::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    return role == Qt::DisplayRole ? QVariant(mEvents[index.row()]) : QVariant();
}

void InterfaceHistory::observe(bool up) {
    if (mKnown && up == mUp)
        return;
    QString event = up ? tr("Up") : tr("Down");
    if (!mKnown) {
        event += tr(" (first observed)");
    } else if (!up && mUptime.isValid()) {
        const qint64 seconds = mUptime.elapsed() / 1000;
        const QString duration = QStringLiteral("%1d %2:%3:%4")
            .arg(seconds / 86400).arg((seconds / 3600) % 24, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
        event += mFirstUp ? tr(" (observed up for %1)").arg(duration)
                         : tr(" (ran for %1)").arg(duration);
    }
    mFirstUp = !mKnown && up;
    mKnown = true;
    mUp = up;
    if (up)
        mUptime.start();
    else
        mUptime.invalidate();

    if (rowCount() == MaximumEvents) {
        beginRemoveRows(QModelIndex(), 0, 0);
        mEvents.pop_front();
        mDiscarded = true;
        endRemoveRows();
    }
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    mEvents.push_back(QDateTime::currentDateTime().toString(Qt::ISODate) + QStringLiteral("  ") + event);
    endInsertRows();
}

void InterfaceHistory::showWindow(const QString &name) {
    if (!mDialog) {
        mDialog = new QDialog(qobject_cast<QWidget *>(parent()));
        mDialog->setWindowTitle(tr("Interface History"));
        mDialog->resize(600, 360);
        auto *layout = new QVBoxLayout(mDialog);
        auto *box = new QGroupBox(mDialog);
        box->setObjectName(QStringLiteral("interfaceName"));
        auto *boxLayout = new QVBoxLayout(box);
        auto *discarded = new QLabel(tr("Earlier events were discarded (65,535 event limit)."), box);
        discarded->setVisible(mDiscarded);
        boxLayout->addWidget(discarded);
        auto *list = new QListView(box);
        list->setModel(this);
        list->setUniformItemSizes(true);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        boxLayout->addWidget(list);
        layout->addWidget(box);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, mDialog);
        connect(buttons, &QDialogButtonBox::rejected, mDialog, &QDialog::hide);
        layout->addWidget(buttons);
        connect(this, &QAbstractItemModel::rowsAboutToBeInserted, list, [list]() {
            const auto *bar = list->verticalScrollBar();
            list->setProperty("followHistory", bar->value() == bar->maximum());
        });
        connect(this, &QAbstractItemModel::rowsInserted, list, [list]() {
            if (list->property("followHistory").toBool())
                list->scrollToBottom();
        });
        connect(this, &QAbstractItemModel::rowsRemoved, discarded, [discarded]() { discarded->show(); });
    }
    mDialog->findChild<QGroupBox *>(QStringLiteral("interfaceName"))->setTitle(tr("Interface: %1").arg(name));
    const bool wasVisible = mDialog->isVisible();
    mDialog->showNormal();
    if (!wasVisible)
        mDialog->findChild<QListView *>()->scrollToBottom();
    mDialog->raise();
    mDialog->activateWindow();
}
