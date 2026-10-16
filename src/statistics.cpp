#include "statistics.h"
#include "chart.h"
#include "qnetstatsview.h"
#include <QNetworkInterface>
#include <QHostAddress>
#include <QStringList>
#include <QTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWindow>
#include <QFontMetrics>

namespace {
QPoint mouseGlobalPosition(const QMouseEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return event->globalPosition().toPoint();
#else
	return event->globalPos();
#endif
}
}

Statistics::Statistics(QNetStatsView *parent)
		: QDialog(parent), Ui::StatisticsBase(), mParent(parent) {

	setupUi(this);
	this->setWindowTitle(QString("Monitoring Interface %1 - QNetStats").arg(parent->displayName()));

	auto *chart = new Chart(parent->getViewOptions(), parent->mSpeedHistoryTx, parent->mSpeedHistoryRx,
							&parent->mMaxSpeed,
							&parent->mSpeedHistoryPtr, QNetStatsView::HISTORY_SIZE);
	mChart->addWidget(chart);
	mChartWidget = chart;
	chart->installEventFilter(this);
	chart->setToolTip(tr("Double-click for compact chart mode"));

	// Keep the existing UI and its margins together. A hidden container also
	// removes all of its spacers and minimum-size requirements from the dialog.
	mNormalContent = new QWidget(this);
	mNormalContent->setLayout(gridLayout_3);
	mRootLayout = new QVBoxLayout(this);
	mRootLayout->setContentsMargins(0, 0, 0, 0);
	mRootLayout->setSpacing(0);
	mRootLayout->addWidget(mNormalContent);

	mInterfaceLabel = new QLabel(parent->displayName(), chart);
	mInterfaceLabel->setFont(font());
	mInterfaceLabel->setAutoFillBackground(true);
	mInterfaceLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	mInterfaceLabel->adjustSize();
	mInterfaceLabel->hide();
	mCompactMaxSpeedLabel = new QLabel(chart);
	mCompactMaxSpeedLabel->setFont(font());
	mCompactMaxSpeedLabel->setAutoFillBackground(true);
	mCompactMaxSpeedLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	mCompactMaxSpeedLabel->hide();
	this->update();

	mDetailsTimer = new QTimer(this);
	mDetailsTimer->setSingleShot(true);
	mDetailsTimer->setTimerType(Qt::PreciseTimer);
	connect(tabWidget, &QTabWidget::currentChanged, this, &Statistics::updateTabSize);
	updateTabSize(tabWidget->currentIndex());
	connect(mDetailsTimer, &QTimer::timeout, this, &Statistics::updateInterfaceDetails);
	connect(parent, &QNetStatsView::chartHistoryChanged, this, [this]() {
		if (isVisible())
			updateStatistics();
	});
	connect(parent, &QNetStatsView::chartHistoryChanged, chart, &Chart::sampleUpdated);
	connect(chart, &Chart::maximumSpeedChanged, this, [this](double speed) {
		mMaxSpeed->setText(locale().formattedDataSize(speed) + "/s");
		updateCompactLabels();
	});
	connect(mOk, &QPushButton::clicked, this, &Statistics::hideWindow);
}

void Statistics::updateStatistics() {
	mMaxSpeed->setText(this->locale().formattedDataSize(mChartWidget->displayedMaximumSpeed()) + "/s");
	if (mCompact)
		updateCompactLabels();
	mBRx->setText(this->locale().formattedDataSize(mParent->mTotalBytesRx));
	mBTx->setText(this->locale().formattedDataSize(mParent->mTotalBytesTx));
	mByteSpeedRx->setText(this->locale().formattedDataSize(mParent->mSpeedHistoryRx[mParent->mSpeedHistoryPtr]) + "/s");
	mByteSpeedTx->setText(this->locale().formattedDataSize(mParent->mSpeedHistoryTx[mParent->mSpeedHistoryPtr]) + "/s");

	mPRx->setText(QString::number(mParent->mTotalPktRx));
	mPTx->setText(QString::number(mParent->mTotalPktTx));
	mPktSpeedRx->setText(QString::number(mParent->calcSpeed(mParent->mDeltaBufferPRx), 'f', 1) + " pkts/s");
	mPktSpeedTx->setText(QString::number(mParent->calcSpeed(mParent->mDeltaBufferPTx), 'f', 1) + " pkts/s");
}

void Statistics::updateInterfaceDetails() {
	const int interval = qMax(2000, mParent->updateInterval());
	if (mDetailsClock.isValid()) {
		const qint64 remaining = interval - mDetailsClock.elapsed();
		if (remaining > 0) {
			mDetailsTimer->start(int(remaining));
			return;
		}
	}
	// Reopening the window or applying options also respects the last refresh.
	const auto interface = QNetworkInterface::interfaceFromName(mParent->mInterface);
	mDetailsClock.start();
	mDetailsTimer->start(interval);
	setWindowTitle(QString("Monitoring Interface %1 - QNetStats").arg(mParent->displayName()));
	mMTU->setNum(interface.maximumTransmissionUnit());
	mMAC->setText(interface.hardwareAddress());
	if (interface.flags() & QNetworkInterface::IsRunning) {
		QStringList ips, netmasks;
		for (const QNetworkAddressEntry &addr: interface.addressEntries()) {
			ips.append(addr.ip().toString());
			netmasks.append(addr.netmask().toString());
		}
		mIP->setText(ips.join(QLatin1Char('\n')));
		mNetmask->setText(netmasks.join(QLatin1Char('\n')));
		return;
	}
	mIP->setText("Not Connected");
	mNetmask->setText("Not Connected");
	if (!interface.isValid()) {
		mMTU->setText("N/A");
		mMAC->setText("N/A");
	}
}

void Statistics::updateCompactLabels() {
	// Use exactly the same scale value and formatting as the normal view.
	mCompactMaxSpeedLabel->setText(mMaxSpeed->text());
	const int availableWidth = qMax(0, mChartWidget->width() - 12);
	const QSize speedSize = mCompactMaxSpeedLabel->sizeHint();
	mCompactMaxSpeedLabel->resize(qMin(speedSize.width(), availableWidth), speedSize.height());
	mCompactMaxSpeedLabel->move(6, 4);

	// Keep the scale readable when space is tight; shorten the interface name
	// rather than allowing the two labels to overlap.
	const int nameWidth = qMax(0, availableWidth - mCompactMaxSpeedLabel->width() - 6);
	mInterfaceLabel->setText(mInterfaceLabel->fontMetrics().elidedText(mParent->displayName(), Qt::ElideRight, nameWidth));
	const QSize nameSize = mInterfaceLabel->sizeHint();
	mInterfaceLabel->resize(qMin(nameSize.width(), nameWidth), nameSize.height());
	mInterfaceLabel->move(qMax(6, mChartWidget->width() - mInterfaceLabel->width() - 6), 4);
}

void Statistics::updateTabSize(int tabIndex) {
	if (tabIndex == -1)
			return;
	for (int i = 0; i < tabWidget->count(); i++) {
		if (i != tabIndex)
			tabWidget->widget(i)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	}

	tabWidget->widget(tabIndex)->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	tabWidget->widget(tabIndex)->adjustSize();
}

void Statistics::updateDetailsInterval() {
	if (isVisible())
		updateInterfaceDetails();
}

void Statistics::showEvent(QShowEvent *event) {
	updateStatistics();
	updateInterfaceDetails();
	QDialog::showEvent(event);
}

void Statistics::hideEvent(QHideEvent *event) {
	// Window-flag changes also hide the dialog. They are mode transitions,
	// not a request to remember the intermediate layout or window state.
	if (!event->spontaneous() && !mChangingMode)
		mHiddenGeometry = saveGeometry();
	mDetailsTimer->stop();
	mDragPending = false;
	mManualDrag = false;
	QDialog::hideEvent(event);
}

void Statistics::showWindow() {
	if (isVisible())
		return;
	if (mHiddenGeometry.isEmpty()) {
		show();
		return;
	}
	restoreGeometry(mHiddenGeometry);
	showAtCurrentPosition();
}

void Statistics::showAtCurrentPosition() {
	const QPoint framePosition = pos();
	// A move performed by the window manager does not mark the QWidget as
	// explicitly positioned. Without this, QDialog may reposition on show.
	setAttribute(Qt::WA_Moved);
	show();
	// Send an explicit placement request after mapping as well. Use frame
	// coordinates so the classic window's title bar does not introduce drift.
	if (windowHandle() && !(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)))
		windowHandle()->setFramePosition(framePosition);
}

void Statistics::hideWindow() {
	this->hide();
}

void Statistics::setCompact(bool compact) {
	if (mCompact == compact)
		return;

	const bool wasVisible = isVisible();
	mChangingMode = true;
	mHiddenGeometry.clear();
	mDragPending = false;
	mManualDrag = false;
	if (compact) {
		mNormalGeometry = saveGeometry();
		mNormalFlags = windowFlags();
		const QRect chartGeometry(mChartWidget->mapToGlobal(QPoint(0, 0)), mChartWidget->size());
		mCompact = true;
		mChart->removeWidget(mChartWidget);
		mNormalContent->hide();
		mRootLayout->addWidget(mChartWidget);
		updateCompactLabels();
		mInterfaceLabel->show();
		mInterfaceLabel->raise();
		mCompactMaxSpeedLabel->show();
		mCompactMaxSpeedLabel->raise();
		setWindowFlags(mNormalFlags | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
		setWindowState(Qt::WindowNoState);
		mRootLayout->activate();
		setGeometry(chartGeometry);
		mChartWidget->setToolTip(tr("Drag to move; double-click or press Escape to restore"));
	} else {
		mCompact = false;
		mInterfaceLabel->hide();
		mCompactMaxSpeedLabel->hide();
		mRootLayout->removeWidget(mChartWidget);
		mChart->addWidget(mChartWidget);
		mNormalContent->show();
		setWindowFlags(mNormalFlags);
		mRootLayout->activate();
		restoreGeometry(mNormalGeometry);
		mChartWidget->setToolTip(tr("Double-click for compact chart mode"));
	}
	// Changing window flags hides the dialog, and reparenting hides the chart.
	mChartWidget->show();
	if (wasVisible)
		showAtCurrentPosition();
	mChangingMode = false;
}

void Statistics::keyPressEvent(QKeyEvent *event) {
	if (mCompact && event->key() == Qt::Key_Escape) {
		setCompact(false);
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

bool Statistics::eventFilter(QObject *object, QEvent *event) {
	if (object != mChartWidget)
		return QDialog::eventFilter(object, event);

	if (mCompact && event->type() == QEvent::Resize)
		updateCompactLabels();

	if (event->type() == QEvent::MouseButtonDblClick) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() == Qt::LeftButton) {
			setCompact(!mCompact);
			return true;
		}
	}
	if (!mCompact)
		return QDialog::eventFilter(object, event);

	if (event->type() == QEvent::MouseButtonPress) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() == Qt::LeftButton) {
			mDragPending = true;
			mManualDrag = false;
			mDragStart = mouseGlobalPosition(mouse);
			mDragWindowStart = pos();
			return true;
		}
	} else if (event->type() == QEvent::MouseMove) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if ((mDragPending || mManualDrag) && (mouse->buttons() & Qt::LeftButton)) {
			const QPoint delta = mouseGlobalPosition(mouse) - mDragStart;
			if (mDragPending && delta.manhattanLength() >= QApplication::startDragDistance()) {
				mDragPending = false;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
				if (windowHandle() && windowHandle()->startSystemMove())
					return true;
#endif
				mManualDrag = true;
			}
			if (mManualDrag)
				move(mDragWindowStart + delta);
			return true;
		}
	} else if (event->type() == QEvent::MouseButtonRelease) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() == Qt::LeftButton) {
			mDragPending = false;
			mManualDrag = false;
			return true;
		}
	}
	return QDialog::eventFilter(object, event);
}
