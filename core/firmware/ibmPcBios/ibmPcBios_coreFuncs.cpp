
#include <arch/paging.h>
#include <arch/walkerPageRanger.h>
#include <firmware/ibmPcBios/ibmPcBios_coreFuncs.h>
#include <__kstdlib/__ktypes.h>
#include <__kstdlib/__kclib/string.h>
#include <__kclasses/debugPipe.h>
#include <kernel/common/memoryTrib/memoryTrib.h>
#include <kernel/common/waitLock.h>
#include "x86emu.h"
#include "x86EmuAuxFuncs.h"


#define LOWMEM_NPAGES		(0x100000 / PAGING_BASE_SIZE)
#define FWFWS			"Firmware: "


waitLockC		ibmPcBiosLock;

error_t ibmPcBios::initialize(void)
{
	status_t	nMapped;

	memset(&M, 0, sizeof(M));

	// Allocate vmem and map in lower memory.
	M.mem_base = (uarch_t)(memoryTrib.__kmemoryStream.vaddrSpaceStream
		.*memoryTrib.__kmemoryStream.vaddrSpaceStream.getPages)(
			LOWMEM_NPAGES);

	if (M.mem_base == __KNULL)
	{
		__kprintf(ERROR FWFWS"initialize(): Failed to get %d pages for "
			"lowmem buffer.\n", LOWMEM_NPAGES);

		return ERROR_MEMORY_NOMEM_VIRTUAL;
	};

	/* Note carefully the PAGEATTRIB_CACHE_WRITE_THROUGH flag. I did this
	 * because I'm very sure that the firmware will be writing to all kinds
	 * of weird places, and doing all kinds of crazy stuff. Therefore,
	 * since I'm not at all sure of how any particular chipset's firmware
	 * will behave, I'll take the safe route and make sure that all data
	 * writes are immediately seen, and in the right order.
	 **/
	nMapped = walkerPageRanger::mapInc(
		&memoryTrib.__kmemoryStream.vaddrSpaceStream.vaddrSpace,
		(void *)M.mem_base, 0x0, LOWMEM_NPAGES,
		PAGEATTRIB_WRITE | PAGEATTRIB_CACHE_WRITE_THROUGH |
		PAGEATTRIB_PRESENT | PAGEATTRIB_SUPERVISOR);

	if (nMapped < LOWMEM_NPAGES)
	{
		__kprintf(ERROR FWFWS"initialize(): Unable to map buff 0x%p to "
			"lowmem. %d of %d pages were mapped.\n",
			M.mem_base, nMapped, LOWMEM_NPAGES);

		return ERROR_MEMORY_VIRTUAL_PAGEMAP;
	};

	X86EMU_setupMemFuncs(&x86Emu_memFuncs);
	X86EMU_setupPioFuncs(&x86Emu_ioFuncs);

	M.x86.debug = 0;
	M.x86.mode = 0;

	__kprintf(NOTICE FWFWS"initialize(): Done. Lowmem 0x%p.\n",
		M.mem_base);

	return ERROR_SUCCESS;
}

error_t ibmPcBios::shutdown(void)
{
	ibmPcBiosLock.acquire();

	memoryTrib.__kmemoryStream.vaddrSpaceStream.releasePages(
		(void *)M.mem_base, LOWMEM_NPAGES);

	M.mem_base = __KNULL;

	ibmPcBiosLock.release();
	return ERROR_SUCCESS;
}

error_t ibmPcBios::suspend(void)
{
	return ERROR_SUCCESS;
}

error_t ibmPcBios::restore(void)
{
	return ERROR_SUCCESS;
}

/**	EXPLANATION:
 * General API: In order to use the x86Emu lib, a caller must first call
 * ibmPcBios_lock_acquire(). This must be done *before* attempting to set
 * registers or calling for an interrupt, and the caller is required to harvest
 * any information returned in special registers before calling
 * ibmPcBios_lock_release().
 *
 * The code does not guarantee any preservation of state between separate lock
 * acquires.
 **/

void ibmPcBios::acquireLock(void)
{
	ibmPcBiosLock.acquire();
}

void ibmPcBios::releaseLock(void)
{
	ibmPcBiosLock.release();
}

// Assume the caller has already set reg state as needed.
void ibmPcBios::executeInterrupt(ubit8 intNo)
{
	// Place HLT opcodes at 0x500 and point EIP there.
	*(ubit32 *)(M.mem_base + 0x500) = 0xF4F4F4F4;

	M.x86.R_CS = 0x0;
	M.x86.R_EIP = 0x500;

	/* Have the emulator use 0x7E00 as a stack. Should be safe since it
	 * is unlikely that GRUB places multiboot/useful info there: we'd just
	 * be overwriting GRUB (stage1) itself, but not anything useful.
	 *
	 * Technically though, we don't really care about multiboot info. The
	 * very fact that we are using x86Emu relieves us of the need to rely on
	 * it.
	 **/
	M.x86.R_SS = 0x0;
	M.x86.R_ESP = 0x7E00;

	X86EMU_prepareForInt(intNo);
	X86EMU_exec();
}

