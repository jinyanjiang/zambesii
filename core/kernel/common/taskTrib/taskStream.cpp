
#include <scaling.h>
#include <arch/cpuControl.h>
#include <arch/registerContext.h>
#include <__kstdlib/__kflagManipulation.h>
#include <__kclasses/debugPipe.h>
#include <kernel/common/process.h>
#include <kernel/common/taskTrib/taskStream.h>
#include <kernel/common/cpuTrib/cpuTrib.h>
#include <__kthreads/__kcpuPowerOn.h>
#include <__kthreads/__korientation.h>


extern "C" void taskStream_pull(RegisterContext *savedContext)
{
	TaskStream	*currTaskStream;

	// XXX: We are operating on the CPU's sleep stack.
	currTaskStream = &cpuTrib.getCurrentCpuStream()->taskStream;
	if (savedContext != NULL) {
		currTaskStream->getCurrentTaskContext()->context = savedContext;
	};

	currTaskStream->pull();
}

TaskStream::TaskStream(cpuStream *parent)
:
Stream(0),
load(0), capacity(0),
currentPerCpuThread(NULL),
roundRobinQ(SCHEDPRIO_MAX_NPRIOS), realTimeQ(SCHEDPRIO_MAX_NPRIOS),
parentCpu(parent)
{
	/* Ensure that the BSP CPU's pointer to __korientation isn't trampled
	 * by the general case constructor.
	 **/
	if (!FLAG_TEST(parentCpu->flags, CPUSTREAM_FLAGS_BSP)) {
		currentTask = &__kcpuPowerOnThread;
	};
}

error_t TaskStream::initialize(void)
{
	error_t		ret;

	ret = realTimeQ.initialize();
	if (ret != ERROR_SUCCESS) { return ret; };

	return roundRobinQ.initialize();
}

void TaskStream::dump(void)
{
	printf(NOTICE TASKSTREAM"%d: load %d, capacity %d, "
		"currentTask 0x%x(@0x%p): dump.\n",
		parentCpu->cpuId,
		load, capacity,
		(getCurrentTask()->getType() == task::PER_CPU)
			? ((Thread *)getCurrentTask())->getFullId()
			: parentCpu->cpuId,
		getCurrentTask());

	realTimeQ.dump();
	roundRobinQ.dump();
}

TaskContext *TaskStream::getCurrentTaskContext(void)
{
	if (getCurrentTask()->getType() == task::PER_CPU) {
		return parentCpu->getTaskContext();
	} else {
		return ((Thread *)getCurrentTask())->getTaskContext();
	};
}

processId_t TaskStream::getCurrentTaskId(void)
{
	if (getCurrentTask()->getType() == task::PER_CPU) {
		return (processId_t)parentCpu->cpuId;
	} else {
		return ((Thread *)getCurrentTask())->getFullId();
	};
}

error_t TaskStream::cooperativeBind(void)
{
	/**	EXPLANATION:
	 * This function is only ever called on the BSP CPU's Task Stream,
	 * because only the BSP task stream will ever be deliberately co-op
	 * bound without even checking for the presence of pre-empt support.
	 *
	 * At boot, co-operative scheduling is needed to enable certain kernel
	 * services to run, such as the timer services, etc. The timer trib
	 * service for example uses a worker thread to dispatch timer queue
	 * request objects. There must be a simple scheduler in place to enable
	 * thread switching.
	 *
	 * Thus, here we are. To enable co-operative scheduling, we simply
	 * bring up the BSP task stream, add the __korientation thread to it,
	 * set the CPU's bit in onlineCpus, then exit.
	 **/
	/*__korientationThread.schedPolicy = Task::ROUND_ROBIN;
	__korientationThread.schedOptions = 0;
	__korientationThread.schedFlags = 0;

	return roundRobinQ.insert(
		&__korientationThread,
		__korientationThread.schedPrio->prio,
		__korientationThread.schedOptions);*/

	cpuTrib.onlineCpus.setSingle(parentCpu->cpuId);
	return ERROR_SUCCESS;
}

status_t TaskStream::schedule(Task *task)
{
	status_t	ret;
	TaskContext	*taskContext;

	/**	EXPLANATION:
	 * TaskStreamC::schedule() is the lowest level schedule() call; it is
	 * per-CPU in nature and it is the only schedule() layer that can
	 * accept a per-cpu thread as an argument since per-cpu threads are
	 * directly scheduled to their target CPUs (using this function).
	 *
	 * For a normal thread, we just schedule it normally. For per-cpu
	 * threads, we also have to set the CPU's currentPerCpuThread pointer.
	 **/
	taskContext = task->getType() == (task::PER_CPU)
		? parentCpu->getTaskContext()
		: static_cast<Thread *>( task )->getTaskContext();

#if __SCALING__ >= SCALING_SMP
	// We don't need to test this for per-cpu threads.
	if (task->getType() == task::UNIQUE)
	{
		// Make sure that this CPU is in the thread's affinity.
		if (!taskContext->cpuAffinity.testSingle(parentCpu->cpuId))
			{ return TASK_SCHEDULE_TRY_AGAIN; };
	};

	CHECK_AND_RESIZE_BMP(
		&task->parent->cpuTrace, parentCpu->cpuId, &ret,
		"schedule", "cpuTrace");

	if (ret != ERROR_SUCCESS) { return ret; };
#endif
	// Check CPU suitability to run task (FPU, other features).

	// Validate any scheduling parameters that need to be checked.

	taskContext->runState = TaskContext::RUNNABLE;
	if (task->getType() == task::UNIQUE)
		{ ((Thread *)task)->Currenttpu = parentCpu; }
	else
		{ currentPerCpuThread = (PerCpuThread *)task; };

	// Finally, add the task to a queue.
	switch (task->schedPolicy)
	{
	case Task::ROUND_ROBIN:
		ret = roundRobinQ.insert(
			task, task->schedPrio->prio, task->schedOptions);

		break;

	case Task::REAL_TIME:
		ret = realTimeQ.insert(
			task, task->schedPrio->prio, task->schedOptions);

		break;

	default:
		return ERROR_INVALID_ARG_VAL;
	};

	if (ret != ERROR_SUCCESS) { return ret; };

	task->parent->cpuTrace.setSingle(parentCpu->cpuId);
	// Increment and notify upper layers of new task being scheduled.
	updateLoad(LOAD_UPDATE_ADD, 1);
	return ret;
}

static inline void getCr3(paddr_t *ret)
{
	asm volatile("movl	%%cr3, %0\n\t"
		: "=r" (*ret));
}

void TaskStream::pull(void)
{
	Task		*newTask;

	for (;;)
	{
		newTask = pullRealTimeQ();
		if (newTask != NULL) {
			break;
		};

		newTask = pullRoundRobinQ();
		if (newTask != NULL) {
			break;
		};

		// Else set the CPU to a low power state.
		if (/*!FLAG_TEST(parentCpu->flags, CPUSTREAM_FLAGS_BSP)*/ 0)
		{
			printf(NOTICE TASKSTREAM"%d: Entering C1.\n",
				parentCpu->cpuId);
		};

		cpuControl::halt();
	};

	// FIXME: Should take a tlbContextC object instead.
	tlbControl::saveContext(
		&currentTask->parent->getVaddrSpaceStream()->vaddrSpace);

	// If addrspaces differ, switch; don't switch if kernel/per-cpu thread.
	if (newTask->parent->getVaddrSpaceStream()
		!= currentTask->parent->getVaddrSpaceStream()
		&& newTask->parent->id != __KPROCESSID)
	{
		tlbControl::loadContext(
			&newTask->parent->getVaddrSpaceStream()
				->vaddrSpace);
	};

	TaskContext		*newTaskContext;

	newTaskContext = (newTask->getType() == task::PER_CPU)
		? parentCpu->getTaskContext()
		: static_cast<Thread *>( newTask )->getTaskContext();

	newTaskContext->runState = TaskContext::RUNNING;
	currentTask = newTask;
	loadContextAndJump(newTaskContext->context);
}

// TODO: Merge the two queue pull functions for cache efficiency.
Task* TaskStream::pullRealTimeQ(void)
{
	Task		*ret;
	status_t	status;

	(void)status;
	do
	{
		ret = static_cast<Task*>( realTimeQ.pop() );
		if (ret == NULL) {
			return NULL;
		};

		// Make sure the scheduler isn't waiting for this task.
		if (FLAG_TEST(
			ret->schedFlags, TASK_SCHEDFLAGS_SCHED_WAITING))
		{
			status = schedule(ret);
			continue;
		};

		return ret;
	} while (1);
}

Task* TaskStream::pullRoundRobinQ(void)
{
	Task		*ret;
	status_t	status;

	(void)status;
	do
	{
		ret = static_cast<Task*>( roundRobinQ.pop() );
		if (ret == NULL) {
			return NULL;
		};

		// Make sure the scheduler isn't waiting for this task.
		if (FLAG_TEST(
			ret->schedFlags, TASK_SCHEDFLAGS_SCHED_WAITING))
		{
			status = schedule(ret);
			continue;
		};

		return ret;
	} while (1);
}

void TaskStream::updateCapacity(ubit8 action, uarch_t val)
{
	NumaCpuBank		*ncb;

	switch (action)
	{
	case CAPACITY_UPDATE_ADD:
		capacity += val;
		break;

	case CAPACITY_UPDATE_SUBTRACT:
		capacity -= val;
		break;

	case CAPACITY_UPDATE_SET:
		capacity = val;
		break;

	default: return;
	};

	ncb = cpuTrib.getBank(parentCpu->bankId);
	if (ncb == NULL) { return; };

	ncb->updateCapacity(action, val);
}

void TaskStream::updateLoad(ubit8 action, uarch_t val)
{
	NumaCpuBank		*ncb;

	switch (action)
	{
	case LOAD_UPDATE_ADD:
		load += val;
		break;

	case LOAD_UPDATE_SUBTRACT:
		load -= val;
		break;

	case LOAD_UPDATE_SET:
		load = val;
		break;

	default: return;
	};

	ncb = cpuTrib.getBank(parentCpu->bankId);
	if (ncb == NULL) { return; };

	ncb->updateLoad(action, val);
}

void TaskStream::dormant(Task *task)
{
	TaskContext		*taskContext;

	taskContext = (task->getType() == task::PER_CPU)
		? parentCpu->getTaskContext()
		: static_cast<Thread *>( task )->getTaskContext();

	taskContext->runState = TaskContext::STOPPED;
	taskContext->blockState = TaskContext::DORMANT;

	switch (task->schedPolicy)
	{
	case Task::ROUND_ROBIN:
		roundRobinQ.remove(task, task->schedPrio->prio);
		break;

	case Task::REAL_TIME:
		realTimeQ.remove(task, task->schedPrio->prio);
		break;

	default: // Might want to panic or make some noise here.
		return;
	};
}

void TaskStream::block(Task *task)
{
	TaskContext		*taskContext;

	taskContext = (task->getType() == task::PER_CPU)
		? parentCpu->getTaskContext()
		: static_cast<Thread *>( task )->getTaskContext();

	taskContext->runState = TaskContext::STOPPED;
	taskContext->blockState = TaskContext::BLOCKED;

	switch (task->schedPolicy)
	{
	case Task::ROUND_ROBIN:
		roundRobinQ.remove(task, task->schedPrio->prio);
		break;

	case Task::REAL_TIME:
		realTimeQ.remove(task, task->schedPrio->prio);
		break;

	default:
		return;
	};
}

error_t TaskStream::unblock(Task *task)
{
	TaskContext		*taskContext;

	taskContext = (task->getType() == task::PER_CPU)
		? parentCpu->getTaskContext()
		: static_cast<Thread *>( task )->getTaskContext();

	taskContext->runState = TaskContext::RUNNABLE;

	switch (task->schedPolicy)
	{
	case Task::ROUND_ROBIN:
		return roundRobinQ.insert(
			task, task->schedPrio->prio,
			task->schedOptions);

	case Task::REAL_TIME:
		return realTimeQ.insert(
			task, task->schedPrio->prio,
			task->schedOptions);

	default:
		return ERROR_UNSUPPORTED;
	};
}

void TaskStream::yield(Task *task)
{
	TaskContext		*taskContext;

	taskContext = (task->getType() == task::PER_CPU)
		? parentCpu->getTaskContext()
		: static_cast<Thread *>( task )->getTaskContext();

	taskContext->runState = TaskContext::RUNNABLE;

	switch (task->schedPolicy)
	{
	case Task::ROUND_ROBIN:
		roundRobinQ.insert(
			task, task->schedPrio->prio,
			task->schedOptions);
		break;

	case Task::REAL_TIME:
		realTimeQ.insert(
			task, task->schedPrio->prio,
			task->schedOptions);
		break;

	default:
		return;
	};
}

error_t TaskStream::wake(Task *task)
{
	TaskContext		*taskContext;

	taskContext = (task->getType() == task::PER_CPU)
		? parentCpu->getTaskContext()
		: static_cast<Thread *>( task )->getTaskContext();

	if (!(taskContext->runState == TaskContext::STOPPED
		&& taskContext->blockState == TaskContext::DORMANT)
		&& taskContext->runState != TaskContext::UNSCHEDULED)
	{
		return ERROR_UNSUPPORTED;
	};

	taskContext->runState = TaskContext::RUNNABLE;

	switch (task->schedPolicy)
	{
	case Task::ROUND_ROBIN:
		return roundRobinQ.insert(
			task, task->schedPrio->prio,
			task->schedOptions);

	case Task::REAL_TIME:
		return realTimeQ.insert(
			task, task->schedPrio->prio,
			task->schedOptions);

	default:
		return ERROR_CRITICAL;
	};
}

