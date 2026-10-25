#include <QtGlobal>
#ifdef Q_OS_WIN
#include <winsock2.h>
// MinGW exposes MIB_IF_ROW2 and GetIfEntry2 only with these definitions.
#include <ws2ipdef.h>
#include <iphlpapi.h>
// The Windows COM headers define this common C++ identifier as a macro.
#ifdef interface
#undef interface
#endif
#include <QUuid>
#elif defined(Q_OS_MACOS)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/route.h>
#include <net/if_media.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>
#elif defined(Q_OS_LINUX)
#include <fstream>
#include <cstdio>
#include <QFileInfo>
#endif
#include <algorithm>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include "qnetstatsview.h"
#include "qnetstats.h"
#include "statistics.h"
#include "textstatistics.h"
#include <QLocale>

extern const char *programName;

namespace {
QIcon disconnectIcon(bool warning) {
	QIcon icon;
	for (int size : {16, 22, 32, 44, 48, 64, 96, 128}) {
		QPixmap image(size, size);
		image.fill(Qt::transparent);
		if (warning) {
			QPainter painter(&image);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setWindow(0, 0, 64, 64);
			QPolygonF triangle;
			triangle << QPointF(32, 4) << QPointF(61, 58) << QPointF(3, 58);
			painter.setPen(QPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(QColor(255, 210, 0));
			painter.drawPolygon(triangle);
			painter.setPen(Qt::NoPen);
			painter.setBrush(Qt::black);
			painter.drawRoundedRect(QRectF(29, 22, 6, 20), 2, 2);
			painter.drawEllipse(QRectF(29, 47, 6, 6));
		}
		icon.addPixmap(image);
	}
	return icon;
}
}

#ifdef Q_OS_WIN
namespace {
bool readWindowsInterface(const QString &name, MIB_IF_ROW2 &row) {
	row = {};
	// Qt can return a Windows interface name or fall back to the adapter
	// GUID. Resolve both to a LUID, avoiding mutable aliases.
	if (ConvertInterfaceNameToLuidW(reinterpret_cast<const wchar_t *>(name.utf16()),
								   &row.InterfaceLuid) != NO_ERROR) {
		const QUuid uuid(name);
		if (uuid.isNull())
			return false;
		GUID guid{};
		guid.Data1 = uuid.data1;
		guid.Data2 = uuid.data2;
		guid.Data3 = uuid.data3;
		std::copy_n(uuid.data4, 8, guid.Data4);
		if (ConvertInterfaceGuidToLuid(&guid, &row.InterfaceLuid) != NO_ERROR)
			return false;
	}
	return GetIfEntry2(&row) == NO_ERROR;
}
}
#endif

#ifdef Q_OS_MACOS
namespace {
bool readMacInterface(unsigned int index, if_msghdr2 &result) {
	int mib[] = {CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, static_cast<int>(index)};
	// Address changes can grow the result between the size query and read.
	for (int attempt = 0; attempt < 3; ++attempt) {
		size_t size = 0;
		if (sysctl(mib, 6, nullptr, &size, nullptr, 0) != 0 || size == 0)
			return false;
		std::vector<unsigned char> buffer(size);
		if (sysctl(mib, 6, buffer.data(), &size, nullptr, 0) != 0) {
			if (errno == ENOMEM)
				continue;
			return false;
		}
		if (size > buffer.size())
			return false;
		for (size_t offset = 0; size - offset >= 4;) {
			const unsigned char *message = buffer.data() + offset;
			unsigned short length = 0;
			std::memcpy(&length, message, sizeof(length));
			if (length < 4 || length > size - offset)
				return false;
			if (message[2] == RTM_VERSION && message[3] == RTM_IFINFO2) {
				if (length < sizeof(if_msghdr2))
					return false;
				// Routing messages need not be aligned for direct struct access.
				if_msghdr2 row{};
				std::memcpy(&row, message, sizeof(row));
				if (row.ifm_index == index) {
					result = row;
					return true;
				}
			}
			offset += length;
		}
		return false;
	}
	return false;
}
}
#endif

QNetStatsView::QNetStatsView(QNetStats *parent, const QString &interface)
		: QWidget(parent), mParent(parent)
#ifdef Q_OS_LINUX
		, mSysDevPath("/sys/class/net/" + interface + "/")
#endif
		{
	mInterface = interface;
	mCarrier = false;

	QNetStats::readInterfaceConfig(interface, &mOptions);
	mTimer = new QTimer(this);
	mTrayIcon = new QSystemTrayIcon(this);
	mTextTrayIcon = new QSystemTrayIcon(this);
	mContextMenu = new QMenu(this);
	mWarningTimer = new QTimer(this);
	mWarningTimer->setSingleShot(true);
	mWarningTimer->setTimerType(Qt::PreciseTimer);
	connect(mWarningTimer, &QTimer::timeout, this, [this]() {
		mWarningVisible = !mWarningVisible;
		updateWarningPresentation();
		mWarningTimer->start(mWarningVisible ? 2000 : 1000);
	});
	// The menu also provides acknowledgement on macOS, where a click opens it.
	mWarningAction = mContextMenu->addAction(tr("Review disconnect warning"),
		this, &QNetStatsView::showDisconnectWarning);
	mWarningAction->setVisible(false);
#ifdef Q_OS_MACOS
	mContextMenu->addAction(tr("Statistics"), this, &QNetStatsView::showStatistics);
#endif
	mContextMenu->addAction("Configure Interfaces", parent, &QNetStats::showConfigure);
	mContextMenu->addAction("Quit QNetStats", parent, []() { QApplication::quit(); });

	setupView();
	mStatistics = new Statistics(this);
	setupTrayIcon();

	mTimer->start(mOptions.mUpdateInterval);
	connect(mTrayIcon, &QSystemTrayIcon::activated, this, &QNetStatsView::iconActivated);
	connect(mTextTrayIcon, &QSystemTrayIcon::activated, this, &QNetStatsView::iconActivated);
}

void QNetStatsView::setupView() {
	const InterfaceSample sample = readInterfaceSample();
	mInterfaceExists = sample.exists;
	mCarrier = sample.identity != 0 && sample.carrier;
	if (!sample.exists) {
		connect(mTimer, &QTimer::timeout, this, &QNetStatsView::checkMissingInterface);
		return;
	}
	connect(mTimer, &QTimer::timeout, this, &QNetStatsView::updateStats);
}

void QNetStatsView::setupTrayIcon() {
	// Load Icons
	mIconNone = QIcon(":/img/theme" + QString::number(mOptions.mTheme) + "_none.png");
	mIconTx = QIcon(":/img/theme" + QString::number(mOptions.mTheme) + "_tx.png");
	mIconRx = QIcon(":/img/theme" + QString::number(mOptions.mTheme) + "_rx.png");
	mIconBoth = QIcon(":/img/theme" + QString::number(mOptions.mTheme) + "_both.png");
	mCurrentIcon = &mIconNone;
	mTrayIcon->setContextMenu(mContextMenu);
	const bool activityWarning = !mDisconnectedAt.isNull() && !warningUsesTextIcon();
	if (!activityWarning) {
		mTrayIcon->setToolTip(QString("Monitoring %1").arg(displayName()));
		mTrayIcon->setIcon(*mCurrentIcon);
	}
	mTrayIcon->setVisible(activityWarning || (mOptions.mDisplayTrayIcon && mCarrier && mInterfaceExists));
	mTextTrayIcon->setContextMenu(mContextMenu);
	updateTextTrayIcon(true);
	mTextTrayIcon->setVisible(mOptions.mDisplayTextStatistics);
	updateWarningPresentation();
}

bool QNetStatsView::warningUsesTextIcon() const {
	return !mOptions.mDisplayTrayIcon && mOptions.mDisplayTextStatistics;
}

void QNetStatsView::recordDisconnect() {
	// Only a transition from an observed connected state arms the warning.
	// Keep the first detection time until acknowledged, even across reconnects.
	if (!mOptions.mFlashOnDisconnect || !mCarrier || !mDisconnectedAt.isNull())
		return;
	if (mWarningIcon.isNull()) {
		mWarningIcon = disconnectIcon(true);
		mBlankIcon = disconnectIcon(false);
	}
	mDisconnectedAt = QDateTime::currentDateTime();
	mWarningVisible = true;
	mWarningAction->setVisible(true);
	mWarningTimer->start(2000);
	updateWarningPresentation();
	mParent->checkTrayIconsAvailable();
}

QString QNetStatsView::disconnectDescription() const {
	const QString status = !mInterfaceExists ? tr("Missing")
		: mCarrier ? tr("Connected") : tr("Disconnected");
	return tr("Interface: %1\nDetected: %2\nCurrent status: %3")
		.arg(displayName(), QLocale().toString(mDisconnectedAt, QLocale::ShortFormat), status);
}

void QNetStatsView::updateWarningPresentation() {
	if (mDisconnectedAt.isNull())
		return;
	QSystemTrayIcon *target = warningUsesTextIcon() ? mTextTrayIcon : mTrayIcon;
	const QIcon &icon = mWarningVisible ? mWarningIcon : mBlankIcon;
	if (target->icon().cacheKey() != icon.cacheKey())
		target->setIcon(icon);
	const QString description = disconnectDescription();
	const QString tooltip = tr("Unacknowledged disconnect\n%1").arg(description);
	if (target->toolTip() != tooltip)
		target->setToolTip(tooltip);
	// Leave the icon registered during the blank phase, including while down.
	target->show();
	if (mWarningDialog)
		mWarningDialog->setInformativeText(description);
}

void QNetStatsView::showDisconnectWarning() {
	if (mDisconnectedAt.isNull())
		return;
	if (mWarningDialog) {
		mWarningDialog->showNormal();
		mWarningDialog->raise();
		mWarningDialog->activateWindow();
		return;
	}
	auto *dialog = new QMessageBox(QMessageBox::Warning, tr("Interface disconnected"),
		tr("A disconnect was detected."), QMessageBox::NoButton, this);
	mWarningDialog = dialog;
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowModality(Qt::NonModal);
	dialog->setTextFormat(Qt::PlainText);
	dialog->setInformativeText(disconnectDescription());
	auto *acknowledge = dialog->addButton(tr("Acknowledge"), QMessageBox::AcceptRole);
	dialog->addButton(QMessageBox::Close);
	connect(dialog, &QDialog::finished, this, [this, dialog, acknowledge](int) {
		mWarningDialog = nullptr;
		if (dialog->clickedButton() == acknowledge)
			clearDisconnectWarning();
	});
	// No nested event loop: monitoring can be disabled and the view destroyed.
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void QNetStatsView::clearDisconnectWarning() {
	if (mDisconnectedAt.isNull())
		return;
	mDisconnectedAt = QDateTime();
	mWarningTimer->stop();
	mWarningAction->setVisible(false);
	if (mWarningDialog)
		mWarningDialog->close();
	setupTrayIcon();
	mParent->checkTrayIconsAvailable();
}

void QNetStatsView::checkMissingInterface() {
	const InterfaceSample sample = readInterfaceSample();
	mInterfaceExists = sample.exists;
	if (sample.exists) {
		mCarrier = sample.identity != 0 && sample.carrier;
		if (mDisconnectedAt.isNull() || warningUsesTextIcon())
			mTrayIcon->setVisible(mCarrier && mOptions.mDisplayTrayIcon);
		updateWarningPresentation();
		if (mOptions.mNotifications)
			mParent->showInterfaceNotification(QString("Interface %1 reappeared!").arg(displayName()),
								   QSystemTrayIcon::Information,
								   3000);
		disconnect(mTimer, &QTimer::timeout, this, &QNetStatsView::checkMissingInterface);
		connect(mTimer, &QTimer::timeout, this, &QNetStatsView::updateStats);
		mParent->checkTrayIconsAvailable();
	}
}

void QNetStatsView::interfaceMissing() {
	recordDisconnect();
	mInterfaceExists = false;
	mCarrier = false;
	mInterfaceIdentity = 0;
	resetSampling();
	if (mOptions.mNotifications)
		mParent->showInterfaceNotification(QString("Interface %1 disappeared!").arg(displayName()),
							   QSystemTrayIcon::Information,
							   3000);
	if (mDisconnectedAt.isNull() || warningUsesTextIcon())
		mTrayIcon->hide();
	updateWarningPresentation();
	disconnect(mTimer, &QTimer::timeout, this, &QNetStatsView::updateStats);
	connect(mTimer, &QTimer::timeout, this, &QNetStatsView::checkMissingInterface);
	mParent->checkTrayIconsAvailable();
}

void QNetStatsView::updateViewOptions() {
	QNetStats::readInterfaceConfig(mInterface, &mOptions);
	mTimer->setInterval(mOptions.mUpdateInterval);
	mStatistics->updateDetailsInterval();
	if (!mOptions.mFlashOnDisconnect)
		clearDisconnectWarning();
	setupTrayIcon();
}

void QNetStatsView::updateStats() {
	const InterfaceSample sample = readInterfaceSample();
	mInterfaceExists = sample.exists;
	if (!sample.exists) {
		interfaceMissing();
		return;
	}

	if (sample.identity == 0) {
		resetSampling();
		return;
	}

	if (!sample.carrier) { // carrier down
		resetSampling();
		if (mCarrier) {
			recordDisconnect();
			mCarrier = false;
			if (mOptions.mNotifications)
				mParent->showInterfaceNotification(QString("Interface %1 is down!").arg(displayName()),
									   QSystemTrayIcon::Information,
									   3000);
		}
		if (mTrayIcon->isVisible() && (mDisconnectedAt.isNull() || warningUsesTextIcon())) {
			mTrayIcon->hide();
			mParent->checkTrayIconsAvailable();
		}
		updateWarningPresentation();
		return;
	}
	const bool carrierWasDown = !mCarrier;
	mCarrier = true;
	if (mOptions.mDisplayTrayIcon && !mTrayIcon->isVisible()) {
		mTrayIcon->show();
		mParent->checkTrayIconsAvailable();
	}
	if (carrierWasDown)
		updateWarningPresentation();
	if (carrierWasDown && mOptions.mNotifications)
		mParent->showInterfaceNotification(QString("Interface %1 is up!").arg(displayName()),
							   QSystemTrayIcon::Information,
							   3000);

	if (!sample.countersAvailable) {
		resetSampling();
		return;
	}
	const quint64 interfaceIdentity = sample.identity;
	const auto brx = sample.bytesRx, btx = sample.bytesTx;
	const auto prx = sample.packetsRx, ptx = sample.packetsTx;
	const bool newInterface = interfaceIdentity != mInterfaceIdentity;
	const bool countersReset = brx < mBRx || btx < mBTx || prx < mPRx || ptx < mPTx;
	if (newInterface || countersReset)
		resetSampling();

	const bool hadPreviousSample = mSampleClock.isValid();
	if (hadPreviousSample) {
		const qint64 elapsedNs = mSampleClock.nsecsElapsed();
		if (elapsedNs <= 0)
			return;
		const double elapsedSeconds = double(elapsedNs) / 1000000000.0;
		if (++mDeltaBufferPtr == SPEED_BUFFER_SIZE)
			mDeltaBufferPtr = 0;
		if (++mSpeedHistoryPtr == HISTORY_SIZE)
			mSpeedHistoryPtr = 0;

		mSampleSeconds[mDeltaBufferPtr] = elapsedSeconds;
		mDeltaBufferTx[mDeltaBufferPtr] = btx - mBTx;
		mDeltaBufferRx[mDeltaBufferPtr] = brx - mBRx;
		mDeltaBufferPTx[mDeltaBufferPtr] = ptx - mPTx;
		mDeltaBufferPRx[mDeltaBufferPtr] = prx - mPRx;
		mRatesAvailable = true;
		mSpeedHistoryRx[mSpeedHistoryPtr] = calcSpeed(mDeltaBufferRx);
		mSpeedHistoryTx[mSpeedHistoryPtr] = calcSpeed(mDeltaBufferTx);
		calcMaxSpeed();
	}
	mSampleClock.start();

	QIcon *newIcon;
	if (newInterface) {
		newIcon = &mIconNone;
	} else if (brx == mBRx) {
		if (btx == mBTx)
			newIcon = &mIconNone;
		else
			newIcon = &mIconTx;
	} else {
		if (btx == mBTx)
			newIcon = &mIconRx;
		else
			newIcon = &mIconBoth;
	}

	if (newIcon != mCurrentIcon) {
		mCurrentIcon = newIcon;
		if (mDisconnectedAt.isNull() || warningUsesTextIcon())
			mTrayIcon->setIcon(*mCurrentIcon);
	}

	// Include the first counters of each interface lifetime, then safe deltas.
	mTotalBytesRx += newInterface || brx < mBRx ? brx : brx - mBRx;
	mTotalBytesTx += newInterface || btx < mBTx ? btx : btx - mBTx;
	mTotalPktRx += newInterface || prx < mPRx ? prx : prx - mPRx;
	mTotalPktTx += newInterface || ptx < mPTx ? ptx : ptx - mPTx;
	mInterfaceIdentity = interfaceIdentity;

	mBRx = brx;
	mBTx = btx;
	mPRx = prx;
	mPTx = ptx;
	updateTextTrayIcon();
	emit chartHistoryChanged(!hadPreviousSample);
}

QString QNetStatsView::displayName() const {
	return mDisplayName.isEmpty() ? mInterface : mDisplayName;
}

QNetStatsView::InterfaceSample QNetStatsView::readInterfaceSample() {
	InterfaceSample sample;
#ifdef Q_OS_WIN
	MIB_IF_ROW2 row{};
	if (!readWindowsInterface(mInterface, row))
		return sample;
	// One native row supplies existence, identity, link state and all counters.
	sample.exists = true;
	// Reuse the alias in this row instead of querying Qt from each tooltip
	// update and every animated compact-chart label update.
	mDisplayName = QString::fromWCharArray(row.Alias);
	sample.identity = row.InterfaceLuid.Value;
	sample.carrier = row.OperStatus == IfOperStatusUp;
	sample.bytesRx = row.InOctets;
	sample.bytesTx = row.OutOctets;
	sample.packetsRx = row.InUcastPkts + row.InNUcastPkts;
	sample.packetsTx = row.OutUcastPkts + row.OutNUcastPkts;
	sample.countersAvailable = true;
#elif defined(Q_OS_MACOS)
	const QByteArray encoded = mInterface.toLocal8Bit();
	const unsigned int index = if_nametoindex(encoded.constData());
	sample.exists = index != 0;
	if (!sample.exists)
		return sample;
	if_msghdr2 row{};
	if (!readMacInterface(index, row))
		return sample; // Query failure is not evidence of a down or missing link.
	sample.carrier = (row.ifm_flags & IFF_UP) && (row.ifm_flags & IFF_RUNNING);
	// Physical links can retain up/running flags while disconnected. Virtual
	// interfaces without media status retain the flags from the native row.
	if (sample.carrier && !encoded.isEmpty() && encoded.size() < IFNAMSIZ) {
		const int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd >= 0) {
			struct ifmediareq request{};
			std::memcpy(request.ifm_name, encoded.constData(), encoded.size());
			if (ioctl(fd, SIOCGIFMEDIA, &request) == 0 && (request.ifm_status & IFM_AVALID))
				sample.carrier = request.ifm_status & IFM_ACTIVE;
			::close(fd);
		}
	}
	if (if_nametoindex(encoded.constData()) != index)
		return sample; // Do not commit a snapshot spanning an interface replacement.
	sample.identity = row.ifm_index;
	sample.bytesRx = row.ifm_data.ifi_ibytes;
	sample.bytesTx = row.ifm_data.ifi_obytes;
	sample.packetsRx = row.ifm_data.ifi_ipackets;
	sample.packetsTx = row.ifm_data.ifi_opackets;
	sample.countersAvailable = true;
#else
	sample.exists = QFileInfo(mSysDevPath).isDir();
	if (!sample.exists)
		return sample;
	const quint64 identity = readInterfaceIdentity();
	if (identity == 0)
		return sample;
	FILE *file = fopen((mSysDevPath + "carrier").toLatin1(), "r");
	if (file) {
		sample.carrier = fgetc(file) == '1';
		fclose(file);
	}
	if (sample.carrier) {
		sample.countersAvailable = readInterfaceNumValue("rx_bytes", sample.bytesRx) &&
			readInterfaceNumValue("tx_bytes", sample.bytesTx) &&
			readInterfaceNumValue("rx_packets", sample.packetsRx) &&
			readInterfaceNumValue("tx_packets", sample.packetsTx);
	}
	// sysfs counters are separate reads. Preserve the replacement guard around
	// both link state and counters, including unsuccessful counter reads.
	if (readInterfaceIdentity() != identity)
		return sample;
	sample.identity = identity;
#endif
	return sample;
}

#ifdef Q_OS_LINUX
quint64 QNetStatsView::readInterfaceIdentity() const {
	unsigned int index{};
	std::ifstream file((mSysDevPath + "ifindex").toLatin1());
	if (!(file >> index))
		return 0;
	return index;
}

bool QNetStatsView::readInterfaceNumValue(const char *name, unsigned long long &value) const {
	std::ifstream file((mSysDevPath + "statistics/" + name).toLatin1());
	return bool(file >> value);
}
#endif

void QNetStatsView::resetSampling() {
	mRatesAvailable = false;
	mSampleClock.invalidate();
	mDeltaBufferPtr = 0;
	std::fill_n(mSampleSeconds, SPEED_BUFFER_SIZE, 0.0);
	std::fill_n(mDeltaBufferRx, SPEED_BUFFER_SIZE, 0.0);
	std::fill_n(mDeltaBufferTx, SPEED_BUFFER_SIZE, 0.0);
	std::fill_n(mDeltaBufferPRx, SPEED_BUFFER_SIZE, 0.0);
	std::fill_n(mDeltaBufferPTx, SPEED_BUFFER_SIZE, 0.0);
	mSpeedHistoryRx[mSpeedHistoryPtr] = 0.0;
	mSpeedHistoryTx[mSpeedHistoryPtr] = 0.0;
	calcMaxSpeed();
	updateTextTrayIcon();
	emit chartHistoryChanged(true);
}

void QNetStatsView::showStatistics() {
	mStatistics->showWindow();
	mStatistics->raise();
	mStatistics->activateWindow();
}

void QNetStatsView::iconActivated(QSystemTrayIcon::ActivationReason reason) {
#ifdef Q_OS_MACOS
	// Cocoa opens the context menu on mouse press; use its Statistics action.
	Q_UNUSED(reason);
#else
	if (reason == QSystemTrayIcon::ActivationReason::Trigger) {
		if (!mDisconnectedAt.isNull())
			showDisconnectWarning();
		else if (mStatistics->isVisible())
			mStatistics->hideWindow();
		else
			showStatistics();
	}
#endif
}

void QNetStatsView::updateTextTrayIcon(bool force) {
	if (!mOptions.mDisplayTextStatistics || (!mDisconnectedAt.isNull() && warningUsesTextIcon()))
		return;
	// Text statistics use only the latest counter interval, without smoothing.
	const double seconds = mSampleSeconds[mDeltaBufferPtr];
	const double upload = seconds > 0.0 ? mDeltaBufferTx[mDeltaBufferPtr] / seconds : 0.0;
	const double download = seconds > 0.0 ? mDeltaBufferRx[mDeltaBufferPtr] / seconds : 0.0;
	const QString upl = mRatesAvailable ? formatShortRate(upload, mOptions.mTextUseBits) : QString("-");
	const QString dld = mRatesAvailable ? formatShortRate(download, mOptions.mTextUseBits) : QString("-");
	if (force || upl != mLastUpload || dld != mLastDownload) {
		mTextTrayIcon->setIcon(textStatisticsIcon(mOptions, upl, dld));
		mLastUpload = upl;
		mLastDownload = dld;
	}
	const double multiplier = mOptions.mTextUseBits ? 8.0 : 1.0;
	const QString rateTemplate = mOptions.mTextUseBits
		? tr("Upload: %1 bits/s\nDownload: %2 bits/s")
		: tr("Upload: %1 bytes/s\nDownload: %2 bytes/s");
	const QString rates = mRatesAvailable
		? rateTemplate.arg(QLocale().toString(upload * multiplier, 'f', 1),
			QLocale().toString(download * multiplier, 'f', 1))
		: tr("Traffic statistics unavailable");
	mTextTrayIcon->setToolTip(QString("%1: %2\n%3")
		.arg(mOptions.mTextDigit).arg(displayName(), rates));
}
