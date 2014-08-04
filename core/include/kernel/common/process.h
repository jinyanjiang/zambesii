#ifndef _PROCESS_H
	#define _PROCESS_H

	#include <scaling.h>
	#include <arch/memory.h>
	#include <chipset/chipset.h>
	#include <chipset/memory.h>
	#include <__kstdlib/__ktypes.h>
	#include <__kstdlib/__kflagManipulation.h>
	#include <__kclasses/bitmap.h>
	#include <__kclasses/wrapAroundCounter.h>
	#include <kernel/common/thread.h>
	#include <kernel/common/sharedResourceGroup.h>
	#include <kernel/common/multipleReaderLock.h>
	#include <kernel/common/execDomain.h>
	#include <kernel/common/zasyncStream.h>
	#include <kernel/common/memoryTrib/memoryStream.h>
	#include <kernel/common/timerTrib/timerStream.h>
	#include <kernel/common/floodplainn/floodplainnStream.h>
	#include <__kthreads/__korientation.h>

#define PROCESS_ENV_MAX_NVARS		(64)
#define PROCESS_ENV_NAME_MAXLEN		(96)
#define PROCESS_ENV_VALUE_MAXLEN	(512)
#define PROCESS_FULLNAME_MAXLEN		(2000)
#define PROCESS_ARGUMENTS_MAXLEN	(32700)

/**	Flags for processC::flags.
 **/
#define PROCESS_FLAGS___KPROCESS	(1<<0)
#define PROCESS_FLAGS_SPAWNED_DORMANT	(1<<1)

/**	Flags for ProcessStream::spawnThread().
 **/
// STINHERIT by default.
#define SPAWNTHREAD_FLAGS_AFFINITY_STINHERIT	(0)
// Implied if the cpuAffinity BMP != NULL && !PINHERIT
#define SPAWNTHREAD_FLAGS_AFFINITY_SET		(0)
#define SPAWNTHREAD_FLAGS_AFFINITY_PINHERIT	(1<<1)

// STINHERIT by default.
#define SPAWNTHREAD_FLAGS_SCHEDPOLICY_STINHERIT	(0)
#define SPAWNTHREAD_FLAGS_SCHEDPOLICY_SET	(1<<3)

// Set to PRIOCLASS_DEFAULT by default.
#define SPAWNTHREAD_FLAGS_SCHEDPRIO_CLASS_DEFAULT	(0)
#define SPAWNTHREAD_FLAGS_SCHEDPRIO_STINHERIT		(1<<7)
#define SPAWNTHREAD_FLAGS_SCHEDPRIO_PRIOCLASS_SET	(1<<5)
#define SPAWNTHREAD_FLAGS_SCHEDPRIO_SET			(1<<6)

// Set to prevent the thread from being scheduled automatically on creation.
#define SPAWNTHREAD_FLAGS_DORMANT			(1<<8)
// Indicates that this is the first thread of a new process.
#define SPAWNTHREAD_FLAGS_FIRST_THREAD			(1<<9)

#define SPAWNTHREAD_STATUS_NO_PIDS		0x1

namespace fplainn
{
	class Driver;
	class DriverInstance;
}

class ProcessStream
{
public:
	enum typeE {
		DRIVER=0, KERNEL_DRIVER, DISTRIBUTARY, APPLICATION, KERNEL };

	enum executableFormatE { RAW=0, ELF, PE, MACHO };

	ProcessStream(
		processId_t processId, processId_t parentThreadId,
		ubit8 execDomain, void *privateData)
	:
		id(processId), parentThreadId(parentThreadId),
		executableFormat(RAW),
		flags(0),
		privateData(privateData), responseMessage(NULL),

		// Kernel process hands out thread IDs from 1 since 0 is taken.
		nextTaskId(
			CHIPSET_MEMORY_MAX_NTASKS - 1,
			(processId == __KPROCESSID) ? 1 : 0),
		nTasks(0),

		fullName(NULL), workingDirectory(NULL),
		arguments(NULL),
		nEnvVars(0),
		environment(NULL),

		execDomain(execDomain),

		memoryStream(processId, this),
		timerStream(processId, this),
		floodplainnStream(processId, this),
		zasyncStream(processId, this)
	{
		memset(tasks, 0, sizeof(tasks));
		defaultMemoryBank.rsrc = NUMABANKID_INVALID;

		if (id == __KPROCESSID)
		{
			FLAG_SET(flags, PROCESS_FLAGS___KPROCESS);
			nTasks = 1;
			tasks[0] = &__korientationThread;
			defaultMemoryBank.rsrc =
				CHIPSET_NUMA___KSPACE_BANKID;
		};
	}

	error_t initialize(
		const utf8Char *commandLine, const utf8Char *environment,
		Bitmap *cpuAffinity);

	virtual ~ProcessStream(void);

public:
	// Must remain a POD data type.
	struct sInitializationBlockizeInfo
	{
		uarch_t			fullNameSize;
		uarch_t			workingDirectorySize;
		uarch_t			argumentsSize;
	};

	// Must remain a POD data type.
	struct sInitializationBlock
	{
		utf8Char		*fullName;
		utf8Char		*workingDirectory;
		utf8Char		*arguments;
		// enum ProcessStream::typeE.
		ubit8			type;
		ubit8			execDomain;
	};

public:
	Thread *getTask(processId_t processId);
	Thread *getThread(processId_t processId) { return getTask(processId); }

	ubit32 getProcessFullNameMaxLength(void)
		{ return PROCESS_FULLNAME_MAXLEN; }

	ubit32 getProcessArgumentsMaxLength(void)
		{ return PROCESS_ARGUMENTS_MAXLEN; }

	virtual VaddrSpaceStream *getVaddrSpaceStream(void)=0;
	virtual typeE getType(void)=0;
	virtual fplainn::DriverInstance *getDriverInstance(void)
		{ return NULL; }

	executableFormatE getExecutableFormat(void) { return executableFormat; }

	void getInitializationBlockSizeInfo(sInitializationBlockizeInfo *ret);
	void getInitializationBlock(sInitializationBlock *ret);

	void sendResponse(error_t err);

	// I am very reluctant to have this "argument" parameter.
	error_t spawnThread(
		void (*entryPoint)(void *),
		void *argument,
		Bitmap *cpuAffinity,
		Task::schedPolicyE schedPolicy, ubit8 prio,
		uarch_t flags,
		Thread **ret);

public:
	struct sEnvironmentVar
	{
		utf8Char	name[PROCESS_ENV_NAME_MAXLEN],
				value[PROCESS_ENV_VALUE_MAXLEN];
	};

	processId_t		id, parentThreadId;
	executableFormatE	executableFormat;
	uarch_t			flags;
	// Only used once, but w/e, only 4-8 bytes.
	void			*privateData;
	MessageStream::sHeader	*responseMessage;

	MultipleReaderLock	taskLock;
	WrapAroundCounter	nextTaskId;
	uarch_t			nTasks;
	Thread			*tasks[CHIPSET_MEMORY_MAX_NTASKS];

	utf8Char		*fullName, *workingDirectory, *arguments;
	ubit8			nEnvVars;
	sEnvironmentVar		*environment;

	// Execution domain and privilege elevation information.
	ubit8			execDomain;

#if __SCALING__ >= SCALING_SMP
	// Tells us which CPUs this process has run on.
	Bitmap			cpuTrace;
	Bitmap			cpuAffinity;
#endif
#if __SCALING__ >= SCALING_CC_NUMA
	SharedResourceGroup<MultipleReaderLock, numaBankId_t>
		defaultMemoryBank;
#endif

	// Events which have been queued on this process.
	Bitmap			pendingEvents;

	MemoryStream		memoryStream;
	TimerStream		timerStream;
	FloodplainnStream	floodplainnStream;
	ZAsyncStream		zasyncStream;

private:
	error_t getNewThreadId(processId_t *ret);
	Thread *allocateNewThread(processId_t newThreadId, void *privateData);
	void removeThread(processId_t id);

	error_t allocateInternals(void);
	error_t generateFullName(
		const utf8Char *commandLineString, ubit16 *argumentsStartIndex);

	error_t generateArguments(const utf8Char *argumentString);
	error_t generateEnvironment(const utf8Char *environmentString);
	error_t initializeBitmaps(void);
};

/**	ContainerProcessC and ContainedProcess.
 ******************************************************************************/

class ContainerProcess
:
public ProcessStream
{
public:
	ContainerProcess(
		processId_t processId, processId_t parentProcessId,
		ubit8 execDomain, numaBankId_t numaAddrSpaceBinding,
		void *vaddrSpaceBaseAddr, uarch_t vaddrSpaceSize,
		void *privateData)
	:
	ProcessStream(processId, parentProcessId, execDomain, privateData),
	addrSpaceBinding(numaAddrSpaceBinding),
	vaddrSpaceStream(
		processId, this,
		vaddrSpaceBaseAddr, vaddrSpaceSize)
	{}

	error_t initialize(
		const utf8Char *commandLine, const utf8Char *environment,
		Bitmap *affinity)
	{
		error_t		ret;

		ret = ProcessStream::initialize(
			commandLine, environment, affinity);

		if (ret != ERROR_SUCCESS) { return ret; };
		return vaddrSpaceStream.initialize();
	}

	~ContainerProcess(void) {}

public:
	virtual VaddrSpaceStream *getVaddrSpaceStream(void)
		{ return &vaddrSpaceStream; }

public:
	numaBankId_t		addrSpaceBinding;

private:
	VaddrSpaceStream	vaddrSpaceStream;
};

class ContainedProcess
:
public ProcessStream
{
public:
	ContainedProcess(
		processId_t processId, processId_t parentProcessId,
		ubit8 execDomain,
		ContainerProcess *containerProcess,
		void *privateData)
	:
	ProcessStream(processId, parentProcessId, execDomain, privateData),
	containerProcess(containerProcess)
	{}

	error_t initialize(
		const utf8Char *commandLine, const utf8Char *environment,
		Bitmap *affinity)
	{
		return ProcessStream::initialize(
			commandLine, environment,affinity);
	}

	~ContainedProcess(void) {}

public:
	ContainerProcess *getContainerProcess(void)
		{ return containerProcess; }

	virtual VaddrSpaceStream *getVaddrSpaceStream(void)
		{ return containerProcess->getVaddrSpaceStream(); }

private:
	ContainerProcess	*containerProcess;
};

/**	KernelProcess
 ******************************************************************************/

class KernelProcess
:
public ContainerProcess
{
public:
	KernelProcess(void *vaddrSpaceBaseAddr, uarch_t vaddrSpaceSize)
	:
	ContainerProcess(
		__KPROCESSID, __KPROCESSID,
		PROCESS_EXECDOMAIN_KERNEL,
		NUMABANKID_INVALID,
		vaddrSpaceBaseAddr, vaddrSpaceSize,
		NULL)
	{}

	error_t initialize(
		const utf8Char *fullName, const utf8Char *environment,
		Bitmap *affinity)
	{
		return ContainerProcess::initialize(
			fullName, environment, affinity);
	}

	~KernelProcess(void) {}

public:
	virtual typeE getType(void) { return KERNEL; }
};

/**	DistributaryProcess
 ******************************************************************************/

class DistributaryProcess
:
public ContainerProcess
{
public:
	DistributaryProcess(
		processId_t processId, processId_t parentProcessId,
		numaBankId_t numaAddrSpaceBinding,
		void *privateData)
	:
	ContainerProcess(
		processId, parentProcessId,
		PROCESS_EXECDOMAIN_KERNEL,	// Always kernel domain.
		numaAddrSpaceBinding,
		(void *)0x100000,
		ARCH_MEMORY___KLOAD_VADDR_BASE - 0x100000 - 0x1000,
		privateData)
	{}

	error_t initialize(
		const utf8Char *fullName, const utf8Char *environment,
		Bitmap *affinity)
	{
		return ContainerProcess::initialize(
			fullName, environment, affinity);
	}

	~DistributaryProcess(void) {}

public:
	virtual typeE getType(void) { return DISTRIBUTARY; }
};

/**	DriverProcess.
 ******************************************************************************/

class DriverProcess
:
public ContainerProcess
{
public:
	DriverProcess(
		processId_t processId, processId_t parentProcessId,
		ubit8 execDomain,
		numaBankId_t numaAddrSpaceBinding,
		fplainn::DriverInstance *driverInstance,
		void *privateData)
	:
	ContainerProcess(
		processId, parentProcessId,
		execDomain,
		numaAddrSpaceBinding,
		(void *)0x100000,
		ARCH_MEMORY___KLOAD_VADDR_BASE - 0x100000 - 0x1000,
		privateData),
	driverInstance(driverInstance)
	{}

	error_t initialize(
		const utf8Char *fullName, const utf8Char *environment,
		Bitmap *affinity)
	{
		return ContainerProcess::initialize(
			fullName, environment, affinity);
	}

	~DriverProcess(void) {}

public:
	virtual typeE getType(void) { return DRIVER; }
	virtual fplainn::DriverInstance *getDriverInstance(void)
		{ return driverInstance; }

private:
	fplainn::DriverInstance	*driverInstance;
};

/**	Inline Methods:
 ******************************************************************************/

inline Thread *ProcessStream::getTask(processId_t id)
{
	Thread		*ret;
	uarch_t		rwFlags;

	taskLock.readAcquire(&rwFlags);
	ret = tasks[PROCID_TASK(id)];
	taskLock.readRelease(rwFlags);

	return ret;
}

inline error_t ProcessStream::getNewThreadId(processId_t *newThreadId)
{
	sarch_t		nextVal;

	nextVal = nextTaskId.getNextValue(reinterpret_cast<void **>( tasks ));
	if (nextVal < 0) { return ERROR_RESOURCE_EXHAUSTED; };

	*newThreadId = nextVal;
	return ERROR_SUCCESS;
}

#endif

