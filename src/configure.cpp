#include <QListWidget>
#include <QNetworkInterface>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStringList>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QFontDialog>
#include <QStandardItemModel>
#include <QSet>
#include "textstatistics.h"

#include "configure.h"
#include "qnetstats.h"

#include "ui_configurebase.h"

Configure::Configure(QWidget *parent)
		: QDialog(parent), Ui::ConfigureBase(), mInterfaceIcon(":/img/icon_pci.png") {
	setupUi(this);
	mInterfaces->setViewMode(QListWidget::ListMode);
	setupTextStatisticsControls();

	refreshInterfaces();
	connect(this->mRefreshBtn, &QPushButton::clicked, this, &Configure::refreshInterfaces);
	connect(mInterfaces, &QListWidget::currentItemChanged, this, &Configure::changeInterface);
	connect(mTheme, qOverload<int>(&QComboBox::activated), this, &Configure::changeTheme);
}

void Configure::showEvent(QShowEvent *event) {
	if (event->spontaneous()) {
		QDialog::showEvent(event);
		return;
	}
	const QString selected = mCurrentItem;
	mCurrentItem.clear();
	mConfig.clear();
	QSettings settings;
	const QStringList monitored = settings.value("CurrentViews", QStringList()).toStringList();
	for (const QString &name : monitored)
		if (!QNetStats::interfaceIsIgnored(name))
			QNetStats::readInterfaceConfig(name, &mConfig[name]);
	refreshInterfaces();
	for (int i = 0; i < mInterfaces->count(); ++i) {
		auto *item = mInterfaces->item(i);
		if (item->data(Qt::UserRole).toString() == selected) {
			mInterfaces->setCurrentItem(item);
			break;
		}
	}
	QDialog::showEvent(event);
}

void Configure::refreshInterfaces() {
	storeCurrentOptions();
	const QString selected = mCurrentItem;
	// Drop cached entries as well as excluding newly discovered devices.
	for (auto it = mConfig.begin(); it != mConfig.end();) {
		if (QNetStats::interfaceIsIgnored(it.key()))
			it = mConfig.erase(it);
		else
			++it;
	}
	QStringList available;
	const auto interfaces = QNetworkInterface::allInterfaces();
	for (const auto &interface : interfaces) {
		const QString name = interface.name();
		if (QNetStats::interfaceIsIgnored(name))
			continue;
		available.append(name);
		if (!mConfig.contains(name))
			QNetStats::readInterfaceConfig(name, &mConfig[name]);
	}

	QListWidgetItem *current = nullptr;
	{
		const QSignalBlocker blocker(mInterfaces);
		mInterfaces->clear();
		for (auto it = mConfig.constBegin(); it != mConfig.constEnd(); ++it) {
			auto *item = new QListWidgetItem(mInterfaceIcon, QNetStats::interfaceDisplayName(it.key()), mInterfaces);
			item->setData(Qt::UserRole, it.key());
			item->setToolTip(it.key());
			if (!available.contains(it.key())) {
				item->setIcon(QIcon(":/img/interfaces_missing.png"));
				item->setToolTip(tr("Interface currently unavailable"));
			}
			if (it.key() == selected)
				current = item;
		}
		if (!current)
			current = mInterfaces->item(0);
		mInterfaces->setCurrentItem(current);
	}
	mCurrentItem.clear();
	changeInterface(current);
}

void Configure::storeCurrentOptions() {
	if (mCurrentItem.isEmpty() || !mConfig.contains(mCurrentItem))
		return;
	ViewOptions &view = mConfig[mCurrentItem];
	view.mMonitoring = mMonitoringInterface->isChecked();
	view.mNotifications = mDisplayNotifications->isChecked();
	view.mFlashOnDisconnect = mFlashOnDisconnect->isChecked();
	view.mDisplayTrayIcon = mDisplayTrayIcon->isChecked();
	view.mDisplayTextStatistics = mTextStatisticsGroup->isChecked();
	view.mTextDigit = mTextDigit->currentIndex();
	view.mTextDigitPosition = mTextDigitPosition->currentIndex();
	view.mTextShowDigit = mTextShowDigit->isChecked();
	view.mTextShadow = mTextShadow->isChecked();
	view.mTextUseBits = mTextUseBits->isChecked();
	view.mTextDigitColor = mTextDigitColor->color().name();
	view.mTextUploadColor = mTextUploadColor->color().name();
	view.mTextDownloadColor = mTextDownloadColor->color().name();
	view.mTextBackgroundColor = mTextBackgroundColor->color().name();
	view.mTextTransparentBackground = mTextTransparentBackground->isChecked();
	view.mTextFont = mSelectedTextFont;
	view.mUpdateInterval = mUpdateInterval->value();
	view.mTheme = mTheme->currentIndex();
	view.mChartUplColor = mChartUplColor->color().name();
	view.mChartDldColor = mChartDldColor->color().name();
	view.mChartBgColor = mChartBgColor->color().name();
	view.mChartTransparentBackground = mChartTransparentBackground->isChecked();
}

void Configure::changeInterface(QListWidgetItem *item) {
	storeCurrentOptions();
	mConfigurationGroup->setEnabled(item != nullptr);
	mAppearanceGroup->setEnabled(item != nullptr);
	mTextStatisticsGroup->setEnabled(item != nullptr);
	if (!item) {
		mCurrentItem.clear();
		return;
	}
	const QString interface = item->data(Qt::UserRole).toString();
	if (interface == mCurrentItem)
		return;
	mLoadingOptions = true;
	// Load the new interface options
	ViewOptions &view = mConfig[interface];
	// General options
	mMonitoringInterface->setChecked(view.mMonitoring);
	mDisplayNotifications->setChecked(view.mNotifications);
	mFlashOnDisconnect->setChecked(view.mFlashOnDisconnect);
	mDisplayTrayIcon->setChecked(view.mDisplayTrayIcon);
	mTextStatisticsGroup->setChecked(view.mDisplayTextStatistics);
	mTextDigit->setCurrentIndex(view.mTextDigit);
	mTextDigitPosition->setCurrentIndex(view.mTextDigitPosition);
	mTextShowDigit->setChecked(view.mTextShowDigit);
	mTextShadow->setChecked(view.mTextShadow);
	mTextUseBits->setChecked(view.mTextUseBits);
	mTextDigitColor->setColor(QColor(view.mTextDigitColor));
	mTextUploadColor->setColor(QColor(view.mTextUploadColor));
	mTextDownloadColor->setColor(QColor(view.mTextDownloadColor));
	mTextBackgroundColor->setColor(QColor(view.mTextBackgroundColor));
	mTextTransparentBackground->setChecked(view.mTextTransparentBackground);
	mTextBackgroundColor->setEnabled(!view.mTextTransparentBackground);
	mSelectedTextFont = view.mTextFont;
	mTextFontButton->setText(mSelectedTextFont.family());
	mUpdateInterval->setValue(view.mUpdateInterval);
	mTheme->setCurrentIndex(view.mTheme);
	// Chart Options
	mChartUplColor->setColor(QColor(view.mChartUplColor));
	mChartDldColor->setColor(QColor(view.mChartDldColor));
	mChartBgColor->setColor(QColor(view.mChartBgColor));
	mChartTransparentBackground->setChecked(view.mChartTransparentBackground);
	mCurrentItem = interface;

	changeTheme(view.mTheme);
	mLoadingOptions = false;
	updateTextStatisticsControls();
}

bool Configure::canSaveConfig() {
	// update the options
	storeCurrentOptions();

	QMap<int, QString> digits;
	for (auto it = mConfig.constBegin(); it != mConfig.constEnd(); ++it) {
		const auto &view = it.value();
		if (!view.mMonitoring || !view.mDisplayTextStatistics)
			continue;
		if (view.mTextDigit < 0 || view.mTextDigit > 9 || digits.contains(view.mTextDigit)) {
			QMessageBox::warning(this, tr("Text Statistics"),
				tr("Every monitored text icon needs a different digit from 0 to 9."));
			return false;
		}
		digits.insert(view.mTextDigit, it.key());
	}

	bool ok = false;
	for (OptionsMap::ConstIterator i = mConfig.begin(); i != mConfig.end(); ++i)
		if (i.value().mMonitoring) {
			ok = true;
			break;
		}

	if (!ok) {
		QMessageBox msg(this);
		msg.setIcon(QMessageBox::Icon::Information);
		msg.setText("Error");
		msg.setInformativeText("You need to select at least one interface to monitor.");
		msg.exec();
	}

	return ok;
}

void Configure::changeTheme(int theme) {
	mIconError->setPixmap(QPixmap(":/img/theme" + QString::number(theme) + "_error.png"));
	mIconNone->setPixmap(QPixmap(":/img/theme" + QString::number(theme) + "_none.png"));
	mIconTx->setPixmap(QPixmap(":/img/theme" + QString::number(theme) + "_tx.png"));
	mIconRx->setPixmap(QPixmap(":/img/theme" + QString::number(theme) + "_rx.png"));
	mIconBoth->setPixmap(QPixmap(":/img/theme" + QString::number(theme) + "_both.png"));
}

void Configure::setupTextStatisticsControls() {
	mTextStatisticsGroup = new QGroupBox(tr("Display Text Statistics"), this);
	mTextStatisticsGroup->setCheckable(true);
	mTextStatisticsGroup->setChecked(false);
	auto *form = new QFormLayout(mTextStatisticsGroup);
	mTextShowDigit = new QCheckBox(tr("Show interface digit"), mTextStatisticsGroup);
	form->addRow(mTextShowDigit);
	mTextDigit = new QComboBox(mTextStatisticsGroup);
	for (int digit = 0; digit <= 9; ++digit)
		mTextDigit->addItem(QString::number(digit));
	form->addRow(tr("Interface digit:"), mTextDigit);
	mTextDigitPosition = new QComboBox(mTextStatisticsGroup);
	mTextDigitPosition->addItems({tr("Top left"), tr("Top right"), tr("Bottom left"), tr("Bottom right")});
	form->addRow(tr("Digit position:"), mTextDigitPosition);
	mTextShadow = new QCheckBox(tr("Text shadow"), mTextStatisticsGroup);
	form->addRow(mTextShadow);
	mTextUseBits = new QCheckBox(tr("Use bits instead of bytes"), mTextStatisticsGroup);
	mTextUseBits->setToolTip(tr("Bits/s use lowercase suffixes and powers of 1000; bytes/s use uppercase suffixes and powers of 1024."));
	form->addRow(mTextUseBits);
	mTextDigitColor = new ColorButton(mTextStatisticsGroup);
	form->addRow(tr("Digit color:"), mTextDigitColor);
	mTextUploadColor = new ColorButton(mTextStatisticsGroup);
	form->addRow(tr("Upload color:"), mTextUploadColor);
	mTextDownloadColor = new ColorButton(mTextStatisticsGroup);
	form->addRow(tr("Download color:"), mTextDownloadColor);
	mTextBackgroundColor = new ColorButton(mTextStatisticsGroup);
	form->addRow(tr("Background color:"), mTextBackgroundColor);
	mTextTransparentBackground = new QCheckBox(tr("Transparent background"), mTextStatisticsGroup);
	form->addRow(mTextTransparentBackground);
	mTextFontButton = new QPushButton(tr("Choose Font"), mTextStatisticsGroup);
	form->addRow(tr("Statistics font:"), mTextFontButton);
	auto *previews = new QHBoxLayout;
	previews->addWidget(new QLabel(tr("16 px:"), mTextStatisticsGroup));
	mTextPreview16 = new QLabel(mTextStatisticsGroup);
	previews->addWidget(mTextPreview16);
	previews->addWidget(new QLabel(tr("22 px:"), mTextStatisticsGroup));
	mTextPreview22 = new QLabel(mTextStatisticsGroup);
	previews->addWidget(mTextPreview22);
	previews->addStretch();
	form->addRow(tr("Preview:"), previews);
	gridLayout_3->addWidget(mTextStatisticsGroup, 0, 2, 2, 1);
	connect(mTextStatisticsGroup, &QGroupBox::toggled, this, &Configure::updateTextStatisticsControls);
	connect(mMonitoringInterface, &QCheckBox::toggled, this, &Configure::updateTextStatisticsControls);
	connect(mTextDigit, qOverload<int>(&QComboBox::activated), this, &Configure::updateTextStatisticsControls);
	connect(mTextDigitPosition, qOverload<int>(&QComboBox::activated), this, &Configure::updateTextStatisticsPreview);
	connect(mTextShowDigit, &QCheckBox::toggled, this, &Configure::updateTextStatisticsPreview);
	connect(mTextShadow, &QCheckBox::toggled, this, &Configure::updateTextStatisticsPreview);
	connect(mTextUseBits, &QCheckBox::toggled, this, &Configure::updateTextStatisticsPreview);
	for (auto *button : {mTextDigitColor, mTextUploadColor, mTextDownloadColor, mTextBackgroundColor})
		connect(button, &QPushButton::clicked, this, &Configure::updateTextStatisticsPreview);
	connect(mTextTransparentBackground, &QCheckBox::toggled, this, [this](bool transparent) {
		mTextBackgroundColor->setEnabled(!transparent);
		updateTextStatisticsPreview();
	});
	connect(mTextFontButton, &QPushButton::clicked, this, [this]() {
		bool accepted = false;
		const QFont selected = QFontDialog::getFont(&accepted, mSelectedTextFont, this, tr("Statistics Font"));
		if (accepted) {
			mSelectedTextFont = selected;
			mTextFontButton->setText(selected.family());
			updateTextStatisticsPreview();
		}
	});
}

void Configure::updateTextStatisticsControls() {
	if (mLoadingOptions || mCurrentItem.isEmpty())
		return;
	storeCurrentOptions();
	QSet<int> used;
	for (auto it = mConfig.constBegin(); it != mConfig.constEnd(); ++it)
		if (it.key() != mCurrentItem && it->mMonitoring && it->mDisplayTextStatistics)
			used.insert(it->mTextDigit);
	if (mMonitoringInterface->isChecked() && mTextStatisticsGroup->isChecked() && used.contains(mTextDigit->currentIndex())) {
		int available = 0;
		while (available < 10 && used.contains(available))
			++available;
		if (available == 10) {
			const QSignalBlocker blocker(mTextStatisticsGroup);
			mTextStatisticsGroup->setChecked(false);
			QMessageBox::information(this, tr("Text Statistics"),
				tr("All ten digits are in use. Disable text statistics on another interface to free one."));
		} else {
			mTextDigit->setCurrentIndex(available);
		}
	}
	auto *model = qobject_cast<QStandardItemModel *>(mTextDigit->model());
	for (int digit = 0; digit < 10; ++digit)
		model->item(digit)->setEnabled(!used.contains(digit));
	storeCurrentOptions();
	updateTextStatisticsPreview();
}

void Configure::updateTextStatisticsPreview() {
	if (mLoadingOptions || mCurrentItem.isEmpty())
		return;
	storeCurrentOptions();
	const bool showDigit = mTextShowDigit->isChecked();
	mTextDigitPosition->setEnabled(showDigit);
	mTextDigitColor->setEnabled(showDigit);
	const auto &view = mConfig[mCurrentItem];
	mTextPreview16->setPixmap(QPixmap::fromImage(renderTextStatistics(view, formatShortRate(1.1 * 1024 * 1024, view.mTextUseBits),
		formatShortRate(222 * 1024, view.mTextUseBits), 16)));
	mTextPreview22->setPixmap(QPixmap::fromImage(renderTextStatistics(view, formatShortRate(1.1 * 1024 * 1024, view.mTextUseBits),
		formatShortRate(222 * 1024, view.mTextUseBits), 22)));
}
