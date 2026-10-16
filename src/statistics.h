#ifndef STATISTICS_H
#define STATISTICS_H

#include "ui_statisticsbase.h"
#include <QElapsedTimer>
#include <QByteArray>
#include <QPoint>

class QNetStatsView;
class QTimer;
class QShowEvent;
class QHideEvent;
class QKeyEvent;
class Chart;
class QLabel;
class QVBoxLayout;

class Statistics : public QDialog, public Ui::StatisticsBase {
Q_OBJECT
public:
	explicit Statistics(QNetStatsView *parent);
	void updateDetailsInterval();

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	QTimer *mDetailsTimer;
	QElapsedTimer mDetailsClock;
	QNetStatsView *mParent;
	Chart *mChartWidget;
	QWidget *mNormalContent;
	QVBoxLayout *mRootLayout;
	QLabel *mInterfaceLabel;
	QLabel *mCompactMaxSpeedLabel;
	bool mCompact = false;
	bool mChangingMode = false;
	bool mDragPending = false;
	bool mManualDrag = false;
	QPoint mDragStart;
	QPoint mDragWindowStart;
	QByteArray mNormalGeometry;
	QByteArray mHiddenGeometry;
	Qt::WindowFlags mNormalFlags;

	void setCompact(bool compact);
	void showAtCurrentPosition();
	void updateCompactLabels();

public slots:

	void showWindow();

	void hideWindow();

private slots:

	void updateStatistics();
	void updateInterfaceDetails();

	void updateTabSize(int tabIndex);
};

#endif
