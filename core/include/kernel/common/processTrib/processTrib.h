#ifndef _PROCESS_TRIBUTARY_H
	#define _PROCESS_TRIBUTARY_H

	#include <chipset/memory.h>
	#include <__kstdlib/__ktypes.h>
	#include <__kclasses/wrapAroundCounter.h>
	#include <kernel/common/process.h>
	#include <kernel/common/thread.h>
	#include <kernel/common/tributary.h>
	#include <kernel/common/sharedResourceGroup.h>
	#include <kernel/common/multipleReaderLock.h>
	#include <kernel/common/processId.h>

#define PROCTRIB			"Proc Trib: "

#define PROCESSTRIB_UPDATE_ADD		0x0
#define PROCESSTRIB_UPDATE_SUBTRACT	0x1
#define PROCESSTRIB_UPDATE_SET		0x2

/**	Flag values for processTribC::spawnProcess().
 **/
#define SPAWNPROC_FLAGS_AFFINITY_STINHERIT	(0)
#define SPAWNPROC_FLAGS_AFFINITY_PINHERIT	(1<<0)

#define SPAWNPROC_FLAGS_SCHEDPRIO_PRIOCLASS	(0)
#define SPAWNPROC_FLAGS_SCHEDPRIO_SET		(1<<5)

/**	Return values for processTribC::spawnProcess().
 **/
#define SPAWNPROC_STATUS_INVALID_FILE_NAME	(0x1)


class processTribC
:
public tributaryC
{
public:
	processTribC(void *vaddrSpaceBaseAddr, uarch_t vaddrSpaceSize)
	:
	__kprocess(vaddrSpaceBaseAddr, vaddrSpaceSize),
	// Kernel occupies process ID 1; we begin handing process IDs from 2.
	nextProcId(
		CHIPSET_MEMORY_MAX_NPROCESSES - 1,
		PROCID_PROCESS(__KPROCESSID) + 1)
	{
		memset(processes.rsrc, 0, sizeof(processes.rsrc));
		/**	EXPLANATION:
		 * I am not a fan of mnemonic magic numbers, but in this case
		 * I will use this here. The kernel reserves process ID 0, and
		 * occupies process ID 1 for itself.
		 *
		 * To ensure that process ID 0 is never allocated, we fill its
		 * index with garbage. We then expect that should this index
		 * ever be dereferenced we should get a page fault of some kind.
		 **/
		processes.rsrc[0] = (processStreamC *)0xFEEDBEEF;
		processes.rsrc[PROCID_PROCESS(__KPROCESSID)] = &__kprocess;

		fillOutPrioClasses();
	}

	error_t initialize(void)
	{
		/* DO NOT call __kprocess.initialize() here. It is explicitly
		 * called by __korientationInit().
		 **/
		return ERROR_SUCCESS;
	}

public:
	containerProcessC *__kgetStream(void) { return &__kprocess; };
	processStreamC *getStream(processId_t id);

	/**	EXPLANATION:
	 * Distributaries are by nature high privilege processes with high
	 * trust placed in them, and they all run in the kernel executaion
	 * domain. Their scheduler priority and policy are determined by
	 * the kernel and not by the spawning process, and they handle their own
	 * NUMA affinity on a per-thread basis.
	 *
	 * As you can see, the majority of the general userspace arguments
	 * to spawnStream() are redundant for creating distributary processes.
	 **/
	// Callback uses genericCallbackS.
	#define ZMESSAGE_PROCESS_SPAWN_DISTRIBUTARY	(0)
	error_t spawnDistributary(
		utf8Char *commandLine,
		utf8Char *environment,
		numaBankId_t addrSpaceBinding,
		ubit8 schedPrio,
		uarch_t flags,
		void *privateData,
		distributaryProcessC **ret);

	// Callback uses genericCallbackS.
	#define ZMESSAGE_PROCESS_SPAWN_STREAM		(1)
	error_t spawnStream(
		utf8Char *commandLine,
		utf8Char *environment,
		numaBankId_t addrSpaceBinding,	// NUMA addrspace binding.
		bitmapC *cpuAffinity,		// Ocean/NUMA/SMP affinity.
		void *elevation,		// Privileges.
		ubit8 execDomain,		// Kernel mode vs. User mode.
		ubit8 schedPolicy,		// Sched policy of 1st thread.
		ubit8 prio,			// Sched prio of 1st thread.
		uarch_t flags,			// proc + 1st thread spawn flags
		void *privateData,
		processStreamC **ret);		// Returned error value.

	error_t destroyStream(void);

private:
	void fillOutPrioClasses(void);
	error_t getNewProcessId(processId_t *ret);

	// All processes begin execution here.
	static void commonEntry(void *);
	static error_t getDistributaryExecutableFormat(
		utf8Char *fullName,
		processStreamC::executableFormatE *executableFormat,
		void (**entryPoint)(void));

	static error_t getDriverExecutableFormat(
		utf8Char *fullName,
		processStreamC::executableFormatE *executableFormat);

	static error_t getApplicationExecutableFormat(
		utf8Char *fullName,
		processStreamC::executableFormatE *executableFormat);

	static error_t getExecutableFormat(
		ubit8 *buffer,
		processStreamC::executableFormatE *executableFormat);

private:
	kernelProcessC		__kprocess;
	wrapAroundCounterC	nextProcId;
	struct
	{
		multipleReaderLockC	lock;
		processStreamC		*rsrc[CHIPSET_MEMORY_MAX_NPROCESSES];
	} processes;
};

extern processTribC	processTrib;

/**	Inline Methods
 *****************************************************************************/

#endif

