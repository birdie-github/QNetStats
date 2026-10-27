#ifndef QNETSTATS_H
#define QNETSTATS_H

#include "configure.h"
#include <QHash>
#include <QSystemTrayIcon>

class QNetStatsView;
class QListWidget;
class QPushButton;
class QMenu;

class QNetStats : public QDialog {
Q_OBJECT
public:
	QNetStats();

	static QString interfaceDisplayName(const QString &name);
	static bool interfaceIsIgnored(const QString &name);

	static void readInterfaceConfig(const QString &ifName, ViewOptions *opts);

	void checkTrayIconsAvailable();
	void addInterfaceHistoryMenu(QMenu *menu);
	void showInterfaceNotification(const QString &message,
		QSystemTrayIcon::MessageIcon icon, int milliseconds);

public slots:

	void showConfigure();

	/// Configure dialog Apply button
	void configApply();

private:
	typedef QHash<QString, QNetStatsView *> TrayIconMap;
	QSystemTrayIcon *mBackupTrayIcon;
	QMenu *mBackupStatisticsMenu;
	TrayIconMap mViews;
	Configure *mConfigure;
	QDialog *mFallbackWindow;
	QListWidget *mFallbackInterfaces;
	QPushButton *mFallbackStatistics;

	void setup();

	void setupBackupTrayIcon();
	void setupFallbackWindow();
	void updateFallbackWindow();
	void showSelectedStatistics();

	void saveConfig(const OptionsMap &options);
};

#endif
