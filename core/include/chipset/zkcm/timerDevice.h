#ifndef _ZKCM_TIMER_SOURCE_H
	#define _ZKCM_TIMER_SOURCE_H

	#include <chipset/zkcm/device.h>
	#include <__kstdlib/__ktypes.h>
	#include <kernel/common/timerTrib/timeTypes.h>
	#include <kernel/common/processId.h>

/**	Constants used with struct zkcmTimerSourceS.
 **/
// Values for zkcmTimerDeviceC.capabilities.modes.
#define ZKCM_TIMERSRC_CAP_MODE_PERIODIC		(1<<0)
#define ZKCM_TIMERSRC_CAP_MODE_ONESHOT		(1<<1)

// Values for zkcmTimerDeviceC.capabilities.resolutions.
#define ZKCM_TIMERSRC_CAP_RES_1S		(1<<0)
#define ZKCM_TIMERSRC_CAP_RES_100ms		(1<<1)
#define ZKCM_TIMERSRC_CAP_RES_10ms		(1<<2)
#define ZKCM_TIMERSRC_CAP_RES_1ms		(1<<3)
#define ZKCM_TIMERSRC_CAP_RES_100ns		(1<<4)
#define ZKCM_TIMERSRC_CAP_RES_10ns		(1<<5)
#define ZKCM_TIMERSRC_CAP_RES_1ns		(1<<6)

// Values for zkcmTimerSourceS.state.flags.
#define ZKCM_TIMERSRC_STATE_FLAGS_ENABLED	(1<<0)
#define ZKCM_TIMERSRC_STATE_FLAGS_LATCHED	(1<<1)

class zkcmTimerDeviceC
:
public zkcmDeviceBaseC
{
public:
	enum timerTypeE { PER_CPU=0, CHIPSET };
	enum ioLatencyE { LOW=0, MODERATE, HIGH };
	enum precisionE { EXACT=0, NEGLIGABLE, OVERFLOW, UNDERFLOW, ANY };
	enum modeE { PERIODIC=0, ONESHOT };

	zkcmTimerDeviceC(
		timerTypeE type, ubit32 modes, ubit32 resolutions,
		ioLatencyE ioLatency, precisionE precision,
		zkcmDeviceC *device)
	:
	zkcmDeviceBaseC(device),
	latchedProcess(0),
	capabilities(type, modes, resolutions, ioLatency, precision)
	{}

public:
	virtual error_t initialize(void);
	virtual error_t shutdown(void);
	virtual error_t suspend(void);
	virtual error_t restore(void);

	virtual status_t enable(void);
	virtual void disable(void);
	// Call disable() before setting timer options, then enable() again.
	virtual status_t setTimerSourcePeriodic(struct timeS interval);
	virtual status_t setTimerSourceOneshot(struct timestampS timeout);

	/* When a timer source has a 'capability.precision' other than
	 * EXACT or NEGLIGABLE, this API call will return the exact nanosecond
	 * underflow or overflow for the period passed as an argument.
	 *
	 * For example, on the IBM-PC, the RTC can generate a timing signal that
	 * has an /almost/ 1ms period, but underflows the exact precision of 1ms
	 * by a few fractions of a second. It actually generates a period of
	 * 0.976ms (0.976562). For this timer, the driver would set the
	 * "precision" property to PRECISION_UNDERFLOW, and when
	 * getPrecisionDisparityForPeriod(myTimer, ZKCM_TIMERSRC_PERIOD_1ms)
	 * is called, it would return the nanosecond disparity between the
	 * requested period and the actual period generated by the timer source.
	 **/
	virtual uarch_t getPrecisionDiscrepancyForPeriod(ubit32 period);

public:
	// Process ID using this timer source. Valid if FLAGS_LATCHED is set.
	processId_t		latchedProcess;

	struct capabilitiesS
	{
		capabilitiesS(
			timerTypeE type, ubit32 modes, ubit32 resolutions,
			ioLatencyE ioLatency, precisionE precision)
		:
		type(type), ioLatency(ioLatency), precision(precision),
		modes(modes), resolutions(resolutions)
		{}
		
		timerTypeE	type;
		ioLatencyE	ioLatency;
		precisionE	precision;
		// Capabilities (bitfield): PERIODIC, ONESHOT.
		ubit32		modes;
		// Resolutions (bitfield): 1s, 100ms 10ms 1ms, 100ns 10ns 1ns.
		ubit32		resolutions;
	} capabilities;

	struct stateS
	{
		stateS(void)
		:
		flags(0), mode(PERIODIC), period(0)
		{}

		ubit32		flags;
		// Current mode: periodic/oneshot. Valid if FLAGS_ENABLED set.
		modeE		mode;
		// For periodic mode: stores the current timer period in ns.
		ubit32		period;
		// For oneshot mode: stores the current timeout date and time.
		timestampS	timeout;
	} state;
};

#endif

