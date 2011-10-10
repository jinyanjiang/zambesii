
#include <chipset/pkg/chipsetPackage.h>
#include <__kstdlib/__kflagManipulation.h>
#include <__kstdlib/__kclib/string.h>
#include <__kclasses/debugPipe.h>
#include <kernel/common/task.h>
#include <kernel/common/cpuTrib/cpuStream.h>
#include <kernel/common/cpuTrib/powerOperations.h>
#include <__kthreads/__kcpuPowerOn.h>

// We make a global cpuStream for the bspCpu.
#if __SCALING__ >= SCALING_CC_NUMA
cpuStreamC		bspCpu(0, 0, 0);
#else
cpuStreamC		bspCpu(0, 0);
#endif


/**	NOTE:
 * A lot of preprocessing in here: It looks quite ugly I suppose.
 **/
#if __SCALING__ >= SCALING_CC_NUMA
cpuStreamC::cpuStreamC(numaBankId_t bid, cpu_t cid, ubit32 acpiId)
#else
cpuStreamC::cpuStreamC(cpu_t cid, ubit32 acpiId)
#endif
:
cpuId(cid), cpuAcpiId(acpiId),
#if __SCALING__ >= SCALING_CC_NUMA
bankId(bid),
#endif
taskStream(this)
#if __SCALING__ >= SCALING_SMP
,interCpuMessager(this)
#endif
{
	// Nothing to be done for now.
}

#include <arch/tlbControl.h>

status_t cpuStreamC::powerControl(ubit16 command, uarch_t flags)
{
	__kcpuPowerOnBlock.lock.acquire();
	__kcpuPowerOnBlock.cpuStream = this;
	__kcpuPowerOnBlock.sleepStack = &this->sleepStack[PAGING_BASE_SIZE];

	// Call the chipset code to now actually wake the CPU up.
	(*chipsetPkg.cpus->powerControl)(cpuId, command, flags);

for (;;) {
	tlbControl::smpFlushEntryRange((void *)0xC0800000, 32);
};
return ERROR_SUCCESS;
}

cpuStreamC::~cpuStreamC(void)
{
}

