#ifndef QNETSTATSVIEW_H
#define QNETSTATSVIEW_H

#include <QSystemTrayIcon>
#include <QWidget>
#include <QIcon>
#include <QElapsedTimer>
#include "configure.h"

class QNetStats;
class Statistics;
class QMenu;
class QTimer;

class QNetStatsView : public QWidget {
Q_OBJECT

public:
	enum BUFFER_SIZES {
		HISTORY_SIZE = 50,    // Tamanho do historico.
		SPEED_BUFFER_SIZE = 10    // Tamanho do buffer usado para calcular a velocidade
	};

	//	Rx e Tx to bytes and packets
	unsigned long long mBRx{}, mBTx{}, mPRx{}, mPTx{};
	// Statistics
	unsigned long long mTotalBytesRx{}, mTotalBytesTx{}, mTotalPktRx{}, mTotalPktTx{};
	// Traffic deltas for the rolling speed window
	double mDeltaBufferRx[SPEED_BUFFER_SIZE]{}, mDeltaBufferTx[SPEED_BUFFER_SIZE]{};
	double mDeltaBufferPRx[SPEED_BUFFER_SIZE]{}, mDeltaBufferPTx[SPEED_BUFFER_SIZE]{};
	// pointer to current traffic delta buffer position
	int mDeltaBufferPtr{};
	int mSpeedHistoryPtr{};

	// History buffer TODO: Make it configurable!
	double mSpeedHistoryRx[HISTORY_SIZE]{};
	double mSpeedHistoryTx[HISTORY_SIZE]{};
	double mMaxSpeed{};
	QString mInterface;                // Current interface

	QNetStatsView(QNetStats *parent, const QString &interface);

	void updateViewOptions();
	void showStatistics();

	QString displayName() const;

	///	The current Update Interval in miliseconds
	inline int updateInterval() const;

	const ViewOptions *getViewOptions() const { return &mOptions; }

	// Calculate a rate from traffic deltas and their measured durations.
	inline double calcSpeed(const double *buffer) const;

	inline bool trayIconVisible() { return mTrayIcon->isVisible() || mTextTrayIcon->isVisible(); }

signals:
	void chartHistoryChanged(bool reset);

private:
	QNetStats *mParent;
#ifdef Q_OS_LINUX
	QString mSysDevPath;            // Path to the Linux device.
#endif
	bool mInterfaceExists = false;
	bool mCarrier;                    // Interface carrier is on?
	QSystemTrayIcon *mTrayIcon;
	QSystemTrayIcon *mTextTrayIcon;
	QString mLastUpload, mLastDownload;
	QString mDisplayName;
	bool mRatesAvailable = false;
	QMenu *mContextMenu;
	Statistics *mStatistics;        // Statistics window
	ViewOptions mOptions;            // View options
	// Icons
	QIcon mIconNone, mIconTx, mIconRx, mIconBoth;
	QIcon *mCurrentIcon{};            // Current state
	QTimer *mTimer;                    // Timer
	quint64 mInterfaceIdentity{};
	QElapsedTimer mSampleClock;
	double mSampleSeconds[SPEED_BUFFER_SIZE]{};

	void updateTextTrayIcon(bool force = false);
	void resetSampling();
	struct InterfaceSample {
		bool exists = false;
		bool carrier = false;
		bool countersAvailable = false;
		quint64 identity = 0;
		unsigned long long bytesRx = 0, bytesTx = 0, packetsRx = 0, packetsTx = 0;
	};
	InterfaceSample readInterfaceSample();
#ifdef Q_OS_LINUX
	quint64 readInterfaceIdentity() const;
	bool readInterfaceNumValue(const char *name, unsigned long long &value) const;
#endif

	// set up the view.
	void setupTrayIcon();

	void updateStats();

	// calc tha max. speed stored in the history buffer
	inline void calcMaxSpeed();

private slots:

	void setupView();

	void checkMissingInterface();

	void iconActivated(QSystemTrayIcon::ActivationReason reason);

	void interfaceMissing();
};

void QNetStatsView::calcMaxSpeed() {
	mMaxSpeed = 0.0;
	for (int i = 0; i < HISTORY_SIZE; ++i) {
		if (mSpeedHistoryRx[i] > mMaxSpeed)
			mMaxSpeed = mSpeedHistoryRx[i];
		if (mSpeedHistoryTx[i] > mMaxSpeed)
			mMaxSpeed = mSpeedHistoryTx[i];
	}
}

double QNetStatsView::calcSpeed(const double *buffer) const {
	double total = 0.0;
	double seconds = 0.0;
	for (int i = 0; i < SPEED_BUFFER_SIZE; ++i) {
		total += buffer[i];
		seconds += mSampleSeconds[i];
	}
	return seconds > 0.0 ? total / seconds : 0.0;
}

int QNetStatsView::updateInterval() const {
	return mOptions.mUpdateInterval;
}

#endif
