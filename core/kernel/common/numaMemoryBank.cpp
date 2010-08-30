
#include <debug.h>
#include <__kstdlib/__kflagManipulation.h>
#include <__kstdlib/__kclib/string.h>
#include <__kstdlib/__kcxxlib/new>
#include <__kclasses/debugPipe.h>
#include <kernel/common/numaMemoryBank.h>


#define NUMAMEMBANK_DEFINDEX_NONE	(-1)

numaMemoryBankC::numaMemoryBankC(void)
{
	ranges.rsrc.arr = __KNULL;
	ranges.rsrc.nRanges = 0;
	ranges.rsrc.defRange = 0;
}

numaMemoryBankC::~numaMemoryBankC(void)
{
	ranges.lock.writeAcquire();

	for (uarch_t i=0; i<ranges.rsrc.nRanges; i++) {
		delete ranges.rsrc.arr[i];
	};

	ranges.rsrc.nRanges = 0;
	delete ranges.rsrc.arr;

	ranges.lock.writeRelease();
}

status_t numaMemoryBankC::addMemoryRange(
	paddr_t baseAddr, paddr_t size, void *mem
	)
{
	numaMemoryRangeC	*memRange, **tmp;
	error_t			err;
	uarch_t			nRanges;

	// Allocate a new bmp allocator.
	memRange = new numaMemoryRangeC(baseAddr, size);
	if (memRange == __KNULL) {
		return ERROR_MEMORY_NOMEM;
	};

	if (mem != __KNULL) {
		err = memRange->initialize(mem);
	}
	else {
		err = memRange->initialize();
	};

	if (err != ERROR_SUCCESS) {
		return static_cast<status_t>( err );
	};

	ranges.lock.writeAcquire();

	nRanges = ranges.rsrc.nRanges;
	tmp = new numaMemoryRangeC*[nRanges + 1];
	if (tmp == __KNULL)
	{
		ranges.lock.writeRelease();
		delete memRange;
		return ERROR_MEMORY_NOMEM;
	};

	memcpy(tmp, ranges.rsrc.arr, sizeof(numaMemoryRangeC *) * nRanges);
	delete ranges.rsrc.arr;
	ranges.rsrc.arr = tmp;
	ranges.rsrc.arr[nRanges] = memRange;
	ranges.rsrc.nRanges++;

	ranges.lock.writeRelease();

	return ERROR_SUCCESS;
}

status_t numaMemoryBankC::removeMemoryRange(paddr_t baseAddr)
{
	numaMemoryRangeC		*tmp=0;

	ranges.lock.writeAcquire();

	for (uarch_t i=0; i<ranges.rsrc.nRanges; i++)
	{
		if (ranges.rsrc.arr[i]->identifyPaddr(baseAddr))
		{
			tmp = ranges.rsrc.arr[i];
			// Move every other pointer up to cover it.
			for (uarch_t j=i; j<(ranges.rsrc.nRanges - 1); j++) {
				ranges.rsrc.arr[j] = ranges.rsrc.arr[j+1];
			};

			ranges.rsrc.nRanges--;
			// If we just removed the current default range:
			if (ranges.rsrc.defRange == static_cast<sarch_t>( i ))
			{
				ranges.rsrc.defRange =
					(ranges.rsrc.nRanges == 0)
					? NUMAMEMBANK_DEFINDEX_NONE : 0;
			};
			break;
		};
	};

	ranges.lock.writeRelease();

	// Memory range with this base address/contained address doesn't exist.
	if (tmp == __KNULL) {
		return ERROR_INVALID_ARG_VAL;
	};

	delete tmp;
	return ERROR_SUCCESS;
}

error_t numaMemoryBankC::contiguousGetFrames(uarch_t nFrames, paddr_t *paddr)
{
	uarch_t			rwFlags;
	numaMemoryRangeC	*rangeTmp;
	status_t		ret;

	ranges.lock.readAcquire(&rwFlags);

	if (ranges.rsrc.defRange == NUMAMEMBANK_DEFINDEX_NONE)
	{
		// Check and see if any new ranges have been added recently.
		if (ranges.rsrc.nRanges == 0)
		{
			// This bank has no associated ranges of memory.
			ranges.lock.readRelease(rwFlags);
			return ERROR_UNKNOWN;
		};

		ranges.lock.readReleaseWriteAcquire(rwFlags);
		ranges.rsrc.defRange = 0;
		ranges.lock.writeRelease();
	};

	ranges.lock.readAcquire(&rwFlags);

	// Allocate from the default first.
	rangeTmp = ranges.rsrc.arr[ranges.rsrc.defRange];
	ret = rangeTmp->contiguousGetFrames(nFrames, paddr);
	if (ret == ERROR_SUCCESS)
	{
		ranges.lock.readRelease(rwFlags);
		return ret;
	};

	// If default has no more mem,
	for (uarch_t i=0; i<ranges.rsrc.nRanges; i++)
	{
		// Don't waste time re-trying the same range.
		if (static_cast<sarch_t>( i ) == ranges.rsrc.defRange) {
			continue;
		};

		ret = ranges.rsrc.arr[i]->contiguousGetFrames(nFrames, paddr);
		if (ret == ERROR_SUCCESS)
		{
			ranges.lock.readReleaseWriteAcquire(rwFlags);

			ranges.rsrc.defRange = i;

			ranges.lock.writeRelease();
			return ret;
		};
	};

	// Reaching here means no mem was found.
	ranges.lock.readRelease(rwFlags);
	return ERROR_MEMORY_NOMEM_PHYSICAL;
}			

status_t numaMemoryBankC::fragmentedGetFrames(uarch_t nFrames, paddr_t *paddr)
{
	uarch_t			rwFlags;
	numaMemoryRangeC	*rangeTmp;
	error_t			ret;

	ranges.lock.readAcquire(&rwFlags);

	if (ranges.rsrc.defRange == NUMAMEMBANK_DEFINDEX_NONE)
	{
		// Check and see if any new ranges have been added recently.
		if (ranges.rsrc.nRanges == 0)
		{
			// This bank has no associated ranges of memory.
			ranges.lock.readRelease(rwFlags);
			return ERROR_UNKNOWN;
		};

		ranges.lock.readReleaseWriteAcquire(rwFlags);
		ranges.rsrc.defRange = 0;
		ranges.lock.writeRelease();
	};

	ranges.lock.readAcquire(&rwFlags);

	// Allocate from the default first.
	rangeTmp = ranges.rsrc.arr[ranges.rsrc.defRange];
	ret = rangeTmp->fragmentedGetFrames(nFrames, paddr);
	if (ret > 0)
	{
		ranges.lock.readRelease(rwFlags);
		return ret;
	};

	// If default has no more mem,
	for (uarch_t i=0; i<ranges.rsrc.nRanges; i++)
	{
		// Don't waste time re-trying the same range.
		if (static_cast<sarch_t>( i ) == ranges.rsrc.defRange) {
			continue;
		};

		ret = ranges.rsrc.arr[i]->fragmentedGetFrames(nFrames, paddr);
		if (ret > 0)
		{
			ranges.lock.readReleaseWriteAcquire(rwFlags);

			ranges.rsrc.defRange = i;

			ranges.lock.writeRelease();
			return ret;
		};
	};

	// Reaching here means no mem was found.
	ranges.lock.readRelease(rwFlags);
	return ERROR_MEMORY_NOMEM_PHYSICAL;
}

void numaMemoryBankC::releaseFrames(paddr_t basePaddr, uarch_t nFrames)
{
	uarch_t		rwFlags;

	ranges.lock.readAcquire(&rwFlags);

	for (uarch_t i=0; i<ranges.rsrc.nRanges; i++)
	{
		if (ranges.rsrc.arr[i]->identifyPaddr(basePaddr))
		{
			ranges.rsrc.arr[i]->releaseFrames(basePaddr, nFrames);

			ranges.lock.readRelease(rwFlags);
			return;
		};
	};

	ranges.lock.readRelease(rwFlags);

	__kprintf(WARNING NUMAMEMBANK"releaseFrames(0x%X, %d) Pmem leak.\n",
		basePaddr, nFrames);
}

sarch_t numaMemoryBankC::identifyPaddr(paddr_t paddr)
{
	uarch_t		rwFlags;

	ranges.lock.readAcquire(&rwFlags);

	for (uarch_t i=0; i<ranges.rsrc.nRanges; i++)
	{
		/* A paddr can only correspond to ONE memory range. We never
		 * hand out pmem allocations spanning multiple discontiguous
		 * physical memory ranges in one allocation. Therefore when
		 * freeing, it's impossible for us to get a pmem or pmem range
		 * to be freed which isn't contiguous, and within one range.
		 **/
		if (ranges.rsrc.arr[i]->identifyPaddr(paddr)) {
			return 1;
		};
	};
	return 0;
}

void numaMemoryBankC::mapMemUsed(paddr_t baseAddr, uarch_t nFrames)
{
	uarch_t		rwFlags;

	ranges.lock.readAcquire(&rwFlags);

	for (uarch_t i=0; i<ranges.rsrc.nRanges; i++)
	{
		if (ranges.rsrc.arr[i]->identifyPaddr(baseAddr)) {
			ranges.rsrc.arr[i]->mapMemUsed(baseAddr, nFrames);
		};
	};

	ranges.lock.readRelease(rwFlags);
}

void numaMemoryBankC::mapMemUnused(paddr_t baseAddr, uarch_t nFrames)
{
	uarch_t		rwFlags;

	ranges.lock.readAcquire(&rwFlags);

	for (uarch_t i=0; i<ranges.rsrc.nRanges; i++)
	{
		if (ranges.rsrc.arr[i]->identifyPaddr(baseAddr)) {
			ranges.rsrc.arr[i]->mapMemUnused(baseAddr, nFrames);
		};
	};

	ranges.lock.readRelease(rwFlags);
}

