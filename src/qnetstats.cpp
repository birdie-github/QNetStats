#include "qnetstats.h"
#include "qnetstatsview.h"
#include "configure.h"

#include <QApplication>
#include <QMenu>
#include <QNetworkInterface>
#include <QSettings>
#include <QMessageBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTimer>
#include <QFontDatabase>
#include <QColor>
#include <QSet>
#include <algorithm>
#ifdef Q_OS_LINUX
#include <QFileInfo>
#include <QRegularExpression>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#endif

extern const char *programName;

namespace {
ViewOptions defaultInterfaceOptions(const QString &name) {
	int theme = name.startsWith("wlan") ? 3 : 0;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
	if (QNetworkInterface::interfaceFromName(name).type() == QNetworkInterface::Wifi)
		theme = 3;
#endif
	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	font.setBold(true);
	ViewOptions defaults;
	defaults.mUpdateInterval = 500;
	defaults.mMonitoring = true;
	defaults.mNotifications = true;
	defaults.mFlashOnDisconnect = false;
	defaults.mDisplayTrayIcon = true;
	defaults.mDisplayTextStatistics = false;
	defaults.mTextDigit = 0;
	defaults.mTextDigitPosition = 0;
	defaults.mTextShowDigit = true;
	defaults.mTextShadow = false;
	defaults.mTextUseBits = false;
	defaults.mTextDigitColor = "#ffd700";
	defaults.mTextUploadColor = "#FF0000";
	defaults.mTextDownloadColor = "#00FF00";
	defaults.mTextBackgroundColor = "#202020";
	defaults.mTextTransparentBackground = false;
	defaults.mTextFont = font;
	defaults.mTheme = theme;
	defaults.mChartUplColor = "#FF0000";
	defaults.mChartDldColor = "#00FF00";
	defaults.mChartBgColor = "#000000";
	defaults.mChartTransparentBackground = false;
	return defaults;
}

template<typename T>
void saveOverride(QSettings &settings, const char *key, const T &value, const T &defaultValue) {
	if (value == defaultValue)
		settings.remove(key);
	else
		settings.setValue(key, QVariant::fromValue(value));
}

void saveColorOverride(QSettings &settings, const char *key,
		const QString &value, const QString &defaultValue) {
	if (QColor(value) == QColor(defaultValue))
		settings.remove(key);
	else
		settings.setValue(key, value);
}
}

QNetStats::QNetStats() : QDialog(nullptr, Qt::Window), mConfigure(nullptr) {
	// read the current views from config file
	QSettings settings;
	QStringList views = settings.value("CurrentViews", QStringList()).toStringList();

	const QStringList savedViews = views;
	views.erase(std::remove_if(views.begin(), views.end(), &QNetStats::interfaceIsIgnored), views.end());
	if (views != savedViews)
		settings.setValue("CurrentViews", views);
	views.removeDuplicates();
	views.sort();
	QSet<int> digits;
	for (const QString &name : views) {
		ViewOptions opts;
		readInterfaceConfig(name, &opts);
		if (!opts.mDisplayTextStatistics)
			continue;
		int digit = opts.mTextDigit;
		if (digits.contains(digit)) {
			for (digit = 0; digit < 10 && digits.contains(digit); ++digit) {}
			settings.beginGroup(name);
			if (digit < 10)
				saveOverride(settings, "TextStatisticsDigit", digit, defaultInterfaceOptions(name).mTextDigit);
			else
				saveOverride(settings, "DisplayTextStatistics", false, defaultInterfaceOptions(name).mDisplayTextStatistics);
			settings.endGroup();
		}
		if (digit < 10)
			digits.insert(digit);
	}

	setup();
	if (views.empty()) {    // no views... =/, display the configuration dialog
		mConfigure->show();
	} else {
		// start the views
		for (auto &view: views) {
			auto *kview = new QNetStatsView(this, view);
			mViews[view] = kview;
		}
	}
	checkTrayIconsAvailable();
}

void QNetStats::checkTrayIconsAvailable() {
	updateFallbackWindow();
	mBackupStatisticsMenu->clear();
	QStringList names = mViews.keys();
	names.sort();
	for (const QString &name : names) {
		auto *view = mViews.value(name);
		mBackupStatisticsMenu->addAction(view->displayName(), view, &QNetStatsView::showStatistics);
	}
	mBackupStatisticsMenu->setEnabled(!names.isEmpty());
	for (auto view: mViews) {
		if (view->trayIconVisible()) {
			mBackupTrayIcon->hide();
			return;
		}
	}
	mBackupTrayIcon->show();
}

void QNetStats::setup() {
	mConfigure = new Configure(this);
	setupBackupTrayIcon();
	setupFallbackWindow();

	if (QNetworkInterface::allInterfaces().empty()) {
		QMessageBox msg(this);
		msg.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		msg.setIcon(QMessageBox::Icon::Warning);
		msg.setText("Warning:");
		msg.setInformativeText("Could not find any network interfaces!");
		msg.exec();
	}

	connect(mConfigure->mOk, &QPushButton::clicked, this, [this]() {
		if (mConfigure->canSaveConfig()) {
			saveConfig(mConfigure->options());
			mConfigure->accept();
		}
	});
	connect(mConfigure->mApply, &QPushButton::clicked, this, &QNetStats::configApply);
	connect(mConfigure->mCancel, &QPushButton::clicked, mConfigure, &QDialog::reject);
}

void QNetStats::setupBackupTrayIcon() {
	mBackupTrayIcon = new QSystemTrayIcon(QIcon(":/img/interfaces_missing.png"), this);
	mBackupTrayIcon->setToolTip("QNetStats — Configure Interfaces");
	auto *mContextMenu = new QMenu(this);
	mBackupStatisticsMenu = mContextMenu->addMenu(tr("Statistics"));
	mContextMenu->addAction("Configure Interfaces", this, &QNetStats::showConfigure);
	mContextMenu->addAction("Quit QNetStats", this, []() { QApplication::quit(); });
	mBackupTrayIcon->setContextMenu(mContextMenu);

#ifndef Q_OS_MACOS
	connect(mBackupTrayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
		if (reason != QSystemTrayIcon::Trigger)
			return;
		if (mConfigure->isVisible()) {
			mConfigure->hide();
			return;
		}
		mConfigure->show();
	});
#endif
}

void QNetStats::setupFallbackWindow() {
	mFallbackWindow = new QDialog(this);
	mFallbackWindow->setWindowTitle(programName);
	auto *layout = new QVBoxLayout(mFallbackWindow);
	auto *notice = new QLabel(tr("No system tray is available. Select an interface to view its statistics."), mFallbackWindow);
	notice->setWordWrap(true);
	layout->addWidget(notice);
	mFallbackInterfaces = new QListWidget(mFallbackWindow);
	layout->addWidget(mFallbackInterfaces);

	auto *buttons = new QHBoxLayout;
	mFallbackStatistics = new QPushButton(tr("Statistics"), mFallbackWindow);
	mFallbackStatistics->setEnabled(false);
	mFallbackStatistics->setDefault(true);
	auto *configure = new QPushButton(tr("Configure Interfaces"), mFallbackWindow);
	auto *quit = new QPushButton(tr("Quit QNetStats"), mFallbackWindow);
	buttons->addWidget(mFallbackStatistics);
	buttons->addWidget(configure);
	buttons->addWidget(quit);
	layout->addLayout(buttons);

	connect(mFallbackStatistics, &QPushButton::clicked, this, &QNetStats::showSelectedStatistics);
	connect(mFallbackInterfaces, &QListWidget::itemActivated, this, &QNetStats::showSelectedStatistics);
	connect(mFallbackInterfaces, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
		mFallbackStatistics->setEnabled(item != nullptr);
	});
	connect(configure, &QPushButton::clicked, this, &QNetStats::showConfigure);
	connect(quit, &QPushButton::clicked, this, []() { QApplication::quit(); });
	connect(mFallbackWindow, &QDialog::rejected, this, []() {
		if (!QSystemTrayIcon::isSystemTrayAvailable())
			QApplication::quit();
	});

	// Qt exposes tray availability as a query, without an availability-change signal.
	auto *timer = new QTimer(this);
	timer->setInterval(1000);
	connect(timer, &QTimer::timeout, this, &QNetStats::updateFallbackWindow);
	timer->start();
}

void QNetStats::updateFallbackWindow() {
	if (QSystemTrayIcon::isSystemTrayAvailable()) {
		mFallbackWindow->hide();
		return;
	}

	QStringList names = mViews.keys();
	names.sort();
	QStringList displayed;
	for (int i = 0; i < mFallbackInterfaces->count(); ++i)
		displayed.append(mFallbackInterfaces->item(i)->data(Qt::UserRole).toString());
	if (names != displayed) {
		const auto *selected = mFallbackInterfaces->currentItem();
		const QString selectedName = selected ? selected->data(Qt::UserRole).toString() : QString();
		const QSignalBlocker blocker(mFallbackInterfaces);
		mFallbackInterfaces->clear();
		QListWidgetItem *current = nullptr;
		for (const QString &name : names) {
			auto *item = new QListWidgetItem(interfaceDisplayName(name), mFallbackInterfaces);
			item->setData(Qt::UserRole, name);
			item->setToolTip(name);
			if (name == selectedName)
				current = item;
		}
		if (current)
			mFallbackInterfaces->setCurrentItem(current);
		else if (!names.isEmpty())
			mFallbackInterfaces->setCurrentRow(0);
	}
	mFallbackStatistics->setEnabled(mFallbackInterfaces->currentItem() != nullptr);
	if (!mFallbackWindow->isVisible())
		mFallbackWindow->show();
}

void QNetStats::showSelectedStatistics() {
	const auto *item = mFallbackInterfaces->currentItem();
	if (!item)
		return;
	auto *view = mViews.value(item->data(Qt::UserRole).toString(), nullptr);
	if (view)
		view->showStatistics();
}

void QNetStats::showConfigure() {
	mConfigure->show();
	mConfigure->raise();
	mConfigure->activateWindow();
}

QString QNetStats::interfaceDisplayName(const QString &name) {
#ifdef Q_OS_WIN
	const auto interface = QNetworkInterface::interfaceFromName(name);
	const QString friendlyName = interface.humanReadableName();
	if (!friendlyName.isEmpty())
		return friendlyName;
#endif
	return name;
}

bool QNetStats::interfaceIsIgnored(const QString &name) {
#ifdef Q_OS_LINUX
	// Old saved dummyN entries must not return as unavailable interfaces.
	if (!QFileInfo::exists("/sys/class/net/" + name)) {
		static const QRegularExpression dummyName(QStringLiteral("^dummy[0-9]+$"));
		return dummyName.match(name).hasMatch();
	}
	// Query the driver, so renamed dummy devices are excluded too and VPN,
	// bridge and other virtual interfaces remain eligible for monitoring.
	const QByteArray encoded = name.toLocal8Bit();
	if (encoded.isEmpty() || encoded.size() >= IFNAMSIZ)
		return false;
	const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return false;
	struct ifreq request{};
	std::memcpy(request.ifr_name, encoded.constData(), encoded.size());
	struct ethtool_drvinfo info{};
	info.cmd = ETHTOOL_GDRVINFO;
	request.ifr_data = reinterpret_cast<char *>(&info);
	const bool ignored = ioctl(fd, SIOCETHTOOL, &request) == 0 &&
		std::strncmp(info.driver, "dummy", sizeof(info.driver)) == 0;
	::close(fd);
	return ignored;
#else
	Q_UNUSED(name);
	return false;
#endif
}

void QNetStats::readInterfaceConfig(const QString &ifName, ViewOptions *opts) {
	QSettings settings;
	const ViewOptions defaults = defaultInterfaceOptions(ifName);
	settings.beginGroup(ifName);
	opts->mUpdateInterval = settings.value("UpdateInterval", defaults.mUpdateInterval).toInt();
	if (opts->mUpdateInterval <= 0)
		opts->mUpdateInterval = defaults.mUpdateInterval;
	opts->mMonitoring = settings.value("Monitoring", defaults.mMonitoring).toBool();
	opts->mNotifications = settings.value("DisplayNotifications", defaults.mNotifications).toBool();
	opts->mFlashOnDisconnect = settings.value("FlashOnDisconnect", defaults.mFlashOnDisconnect).toBool();
	opts->mDisplayTrayIcon = settings.value("DisplayTrayIcon", defaults.mDisplayTrayIcon).toBool();
	opts->mDisplayTextStatistics = settings.value("DisplayTextStatistics", defaults.mDisplayTextStatistics).toBool();
	opts->mTextDigit = std::clamp(settings.value("TextStatisticsDigit", defaults.mTextDigit).toInt(), 0, 9);
	opts->mTextDigitPosition = std::clamp(settings.value("TextStatisticsDigitPosition", defaults.mTextDigitPosition).toInt(), 0, 3);
	opts->mTextShowDigit = settings.value("TextStatisticsShowDigit", defaults.mTextShowDigit).toBool();
	opts->mTextShadow = settings.value("TextStatisticsShadow", defaults.mTextShadow).toBool();
	opts->mTextUseBits = settings.value("TextStatisticsUseBits", defaults.mTextUseBits).toBool();
	opts->mTextDigitColor = settings.value("TextStatisticsDigitColor", defaults.mTextDigitColor).toString();
	opts->mTextUploadColor = settings.value("TextStatisticsUploadColor", defaults.mTextUploadColor).toString();
	opts->mTextDownloadColor = settings.value("TextStatisticsDownloadColor", defaults.mTextDownloadColor).toString();
	opts->mTextBackgroundColor = settings.value("TextStatisticsBackgroundColor", defaults.mTextBackgroundColor).toString();
	opts->mTextTransparentBackground = settings.value("TextStatisticsTransparentBackground", defaults.mTextTransparentBackground).toBool();
	opts->mTextFont = settings.value("TextStatisticsFont", QVariant::fromValue(defaults.mTextFont)).value<QFont>();
	opts->mTheme = settings.value("Theme", defaults.mTheme).toInt();
	opts->mChartUplColor = settings.value("ChartUplColor", defaults.mChartUplColor).toString();
	opts->mChartDldColor = settings.value("ChartDldColor", defaults.mChartDldColor).toString();
	opts->mChartBgColor = settings.value("ChartBgColor", defaults.mChartBgColor).toString();
	opts->mChartTransparentBackground = settings.value("ChartUseTransparentBackground", defaults.mChartTransparentBackground).toBool();
	settings.endGroup();
}

void QNetStats::configApply() {
	if (mConfigure->canSaveConfig())
		saveConfig(mConfigure->options());
}

void QNetStats::saveConfig(const OptionsMap &options) {
	QSettings settings;

	for (OptionsMap::ConstIterator i = options.begin(); i != options.end(); ++i) {
		if (interfaceIsIgnored(i.key())) {
			delete mViews.take(i.key());
			continue;
		}
		TrayIconMap::Iterator trayIcon = mViews.find(i.key());
		const ViewOptions &opt = i.value();

		const ViewOptions defaults = defaultInterfaceOptions(i.key());
		settings.beginGroup(i.key());
		saveOverride(settings, "UpdateInterval", opt.mUpdateInterval, defaults.mUpdateInterval);
		saveOverride(settings, "Monitoring", opt.mMonitoring, defaults.mMonitoring);
		saveOverride(settings, "DisplayNotifications", opt.mNotifications, defaults.mNotifications);
		saveOverride(settings, "FlashOnDisconnect", opt.mFlashOnDisconnect, defaults.mFlashOnDisconnect);
		saveOverride(settings, "DisplayTrayIcon", opt.mDisplayTrayIcon, defaults.mDisplayTrayIcon);
		saveOverride(settings, "DisplayTextStatistics", opt.mDisplayTextStatistics, defaults.mDisplayTextStatistics);
		saveOverride(settings, "TextStatisticsDigit", opt.mTextDigit, defaults.mTextDigit);
		saveOverride(settings, "TextStatisticsDigitPosition", opt.mTextDigitPosition, defaults.mTextDigitPosition);
		saveOverride(settings, "TextStatisticsShowDigit", opt.mTextShowDigit, defaults.mTextShowDigit);
		saveOverride(settings, "TextStatisticsShadow", opt.mTextShadow, defaults.mTextShadow);
		saveOverride(settings, "TextStatisticsUseBits", opt.mTextUseBits, defaults.mTextUseBits);
		saveColorOverride(settings, "TextStatisticsDigitColor", opt.mTextDigitColor, defaults.mTextDigitColor);
		saveColorOverride(settings, "TextStatisticsUploadColor", opt.mTextUploadColor, defaults.mTextUploadColor);
		saveColorOverride(settings, "TextStatisticsDownloadColor", opt.mTextDownloadColor, defaults.mTextDownloadColor);
		saveColorOverride(settings, "TextStatisticsBackgroundColor", opt.mTextBackgroundColor, defaults.mTextBackgroundColor);
		saveOverride(settings, "TextStatisticsTransparentBackground", opt.mTextTransparentBackground, defaults.mTextTransparentBackground);
		saveOverride(settings, "TextStatisticsFont", opt.mTextFont, defaults.mTextFont);
		saveOverride(settings, "Theme", opt.mTheme, defaults.mTheme);
		saveColorOverride(settings, "ChartUplColor", opt.mChartUplColor, defaults.mChartUplColor);
		saveColorOverride(settings, "ChartDldColor", opt.mChartDldColor, defaults.mChartDldColor);
		saveColorOverride(settings, "ChartBgColor", opt.mChartBgColor, defaults.mChartBgColor);
		saveOverride(settings, "ChartUseTransparentBackground", opt.mChartTransparentBackground, defaults.mChartTransparentBackground);
		settings.remove("TextStatisticsDigitMode");
		settings.remove("TextStatisticsColor");
		settings.endGroup();

		if (opt.mMonitoring) {    // check if we are already monitoring this interface.
			if (trayIcon == mViews.end()) { // new interface!
				auto *kview = new QNetStatsView(this, i.key());
				mViews[i.key()] = kview;
			} else
				trayIcon.value()->updateViewOptions();
		} else {
			// Check if a tray icon exist and remove then!
			if (trayIcon != mViews.end()) {
				delete trayIcon.value();
				mViews.erase(trayIcon);
			}
		}
		checkTrayIconsAvailable();
	}

	settings.setValue("CurrentViews", QStringList(mViews.keys()));
	checkTrayIconsAvailable();
}

void QNetStats::showInterfaceNotification(const QString &message,
		QSystemTrayIcon::MessageIcon icon, int milliseconds) {
	// Notifications remain independent of either interface icon switch.
	QSystemTrayIcon *target = mBackupTrayIcon;
	for (auto *tray : findChildren<QSystemTrayIcon *>()) {
		if (tray->isVisible()) {
			target = tray;
			break;
		}
	}
	target->showMessage(programName, message, icon, milliseconds);
}
