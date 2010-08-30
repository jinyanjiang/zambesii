
#include <debug.h>
#include <arch/paging.h>
#include <__kstdlib/__kflagManipulation.h>
#include <__kstdlib/__kcxxlib/new>
#include <__kclasses/memoryBog.h>
#include <__kclasses/debugPipe.h>
#include <kernel/common/memoryTrib/memoryTrib.h>


memoryBogC::memoryBogC(uarch_t bogSize)
{
	blockSize = bogSize;
	head.rsrc = __KNULL;
}

error_t memoryBogC::initialize(void)
{
	return ERROR_SUCCESS;
}

memoryBogC::~memoryBogC(void)
{
	// Free all blocks.
}

// Transparent method of copying headers.
void memoryBogC::moveHeaderDown(void *hdr, uarch_t nBytes)
{
	memoryBogC::allocHeaderS	tmp;

	memcpy(&tmp, hdr, sizeof(memoryBogC::allocHeaderS));
	memcpy(
		reinterpret_cast<void *>(
			(uarch_t)hdr + nBytes ),
		&tmp,
		sizeof(memoryBogC::allocHeaderS));
}

void memoryBogC::moveHeaderUp(void *hdr, uarch_t nBytes)
{
	memoryBogC::allocHeaderS	tmp;

	memcpy(
		&tmp,
		reinterpret_cast<void *>( (uarch_t)hdr + nBytes ),
		sizeof(memoryBogC::allocHeaderS));

	memcpy(hdr, &tmp, sizeof(memoryBogC::allocHeaderS));
}

void memoryBogC::dump(void)
{
	bogBlockS	*block;
	freeObjectS	*obj;

	__kprintf(NOTICE MEMBOG"Dumping.\n");

	head.lock.acquire();
	__kprintf(NOTICE MEMBOG"Size: 0x%X, head pointer: 0x%X.\n",
		blockSize, head.rsrc);

	block = head.rsrc;
	for (; block != __KNULL; block = block->next)
	{
		__kprintf((utf8Char *)"\tBlock: 0x%X, refCount %d 1stObj 0x%X."
			"\n", block, block->refCount, block->firstObject);

		obj = block->firstObject;
		for (; obj != __KNULL; obj = obj->next)
		{
			__kprintf((utf8Char *)"\t\tFree object: "
				"Addr 0x%X, nBytes 0x%X.\n",
				obj, obj->nBytes);
		};
	};

	head.lock.release();
}

void *memoryBogC::allocate(uarch_t nBytes, uarch_t flags)
{
	bogBlockS	*blockTmp;
	freeObjectS	*ret=0, *objTmp, *objTmpPrev;

	if (nBytes == 0) {
		return __KNULL;
	};

	// Size of the alloc must accommodate the alloc header.
	nBytes += sizeof(allocHeaderS);

	head.lock.acquire();

	// If swamp is empty, allocate new block.
	if (head.rsrc == __KNULL)
	{
		head.rsrc = getNewBlock();
		if (head.rsrc == __KNULL)
		{
			head.lock.release();
			return __KNULL;
		};
	};

	// head.rsrc now points to a block of usable memory.
	blockTmp = head.rsrc;

	for (; blockTmp != __KNULL; blockTmp = blockTmp->next)
	{
		objTmpPrev = 0;
		objTmp = blockTmp->firstObject;
		for (; objTmp != __KNULL; )
		{
			// If our allocation can come from this node.
			if (objTmp->nBytes >= nBytes)
			{
				// Just give out the whole space in this case.
				if ((objTmp->nBytes - nBytes)
					<= sizeof(freeObjectS))
				{
					nBytes = objTmp->nBytes;
					if (objTmpPrev) {
						objTmpPrev->next = objTmp->next;
					};
					ret = objTmp;
					// Make sure to keep head valid.
					if (objTmp == blockTmp->firstObject)
					{
						blockTmp->firstObject =
							objTmp->next;
					};
				}
				else
				{
					objTmp->nBytes -= nBytes;
					ret = R_CAST(
						freeObjectS *,
						R_CAST(uarch_t, objTmp)
							+ objTmp->nBytes );
				};
				blockTmp->refCount++;

				head.lock.release();

				R_CAST(allocHeaderS *, ret)->magic =
					MEMBOG_MAGIC;

				R_CAST(allocHeaderS *, ret)->nBytes = nBytes;
				R_CAST(allocHeaderS *, ret)->parent = blockTmp;

				return R_CAST(
					void *,
					R_CAST(uarch_t, ret)
						+ sizeof(allocHeaderS) );
			};
			objTmpPrev = objTmp;
			objTmp = objTmp->next;
		};
	};

	// If we reach here, there isn't enough space. Get new pool, try again.
	if (!__KFLAG_TEST(flags, MEMBOG_NO_EXPAND_ON_FAIL))
	{
		blockTmp = getNewBlock();
		if (blockTmp == __KNULL)
		{
			head.lock.release();
			return __KNULL;
		};

		blockTmp->next = head.rsrc;
		head.rsrc = blockTmp;
		objTmp = head.rsrc->firstObject;

		if (objTmp->nBytes >= nBytes)
		{
			if (objTmp->nBytes - nBytes <= sizeof(freeObjectS))
			{
				nBytes = objTmp->nBytes;
				ret = objTmp;
				blockTmp->firstObject = __KNULL;
			}
			else
			{
				objTmp->nBytes -= nBytes;
				ret = R_CAST(
					freeObjectS *,
					R_CAST(uarch_t, objTmp)
						+ objTmp->nBytes );
			};
			blockTmp->refCount++;

			head.lock.release();

			((allocHeaderS *)ret)->magic = MEMBOG_MAGIC;
			((allocHeaderS *)ret)->nBytes = nBytes;
			((allocHeaderS *)ret)->parent = blockTmp;
			return reinterpret_cast<void *>(
				R_CAST(uarch_t, ret) + sizeof(allocHeaderS) );
		};
	};

	head.lock.release();
	return __KNULL;
}

void memoryBogC::free(void *_mem)
{
	allocHeaderS	*mem;
	bogBlockS	*block;
	freeObjectS	*objTmp, *prevObj;
	uarch_t		nBytesTmp;

	/**	EXPLANATION:
	 * We essentially want to first check to make sure the memory hasn't
	 * been corrupted, or the memory isn't a bad free, then as quickly as
	 * possible, locate the insertion point for the object and free.
	 **/
	mem = reinterpret_cast<allocHeaderS *>(
		reinterpret_cast<uarch_t>( _mem ) - sizeof(allocHeaderS) );

	if (mem == __KNULL) {
		return;
	};

	if (mem->magic != MEMBOG_MAGIC)
	{
		__kprintf(WARNING MEMBOG"Corrupt memory or bad free at 0x%X, "
			"magic was 0x%X.\n", mem, mem->magic);

		return;
	};

	block = mem->parent;

	head.lock.acquire();

	prevObj = __KNULL;
	objTmp = block->firstObject;

	for (; objTmp != __KNULL; )
	{
		if ((void *)mem < (void *)objTmp)
		{
			nBytesTmp = mem->nBytes;
			((freeObjectS *)mem)->next = objTmp;
			((freeObjectS *)mem)->nBytes = nBytesTmp;

			// Concatenate forward.
			if ((R_CAST(uarch_t, mem)
				+ R_CAST(freeObjectS*, mem)->nBytes)
				== R_CAST(uarch_t, objTmp))
			{
				R_CAST(freeObjectS *, mem)->nBytes +=
					objTmp->nBytes;

				R_CAST(freeObjectS *, mem)->next = objTmp->next;
			};

			if (prevObj != __KNULL)
			{
				prevObj->next = R_CAST(freeObjectS *, mem);

				// Concatenate backward.
				if ((R_CAST(uarch_t, prevObj) + prevObj->nBytes)
					== R_CAST(uarch_t, mem))
				{
					prevObj->nBytes +=
						R_CAST(freeObjectS *, mem)
							->nBytes;

					prevObj->next =
						R_CAST(freeObjectS *, mem)
							->next;
				};
			}
			else {
				block->firstObject = R_CAST(freeObjectS *, mem);
			};
			block->refCount--;

			head.lock.release();
			return;
		};
		prevObj = objTmp;
		objTmp = objTmp->next;
	};

	/* The loop takes care of the case where we add at the front of the
	 * object list, and in the middle. This next bit of code will handle
	 * case where we add at the end of the list or the list is empty.
	 **/
	if (prevObj != __KNULL)
	{
		// Adding at the end of the list.
		nBytesTmp = mem->nBytes;
		prevObj->next = (freeObjectS *)mem;
		R_CAST(freeObjectS *, mem)->next = __KNULL;
		R_CAST(freeObjectS *, mem)->nBytes = nBytesTmp;

		if ((R_CAST(uarch_t, prevObj) + prevObj->nBytes)
			== R_CAST(uarch_t, mem))
		{
			prevObj->nBytes += R_CAST(freeObjectS *, mem)->nBytes;
			prevObj->next = R_CAST(freeObjectS *, mem)->next;
		}
		block->refCount--;

		head.lock.release();
		return;
	}
	else
	{
		// List is empty. Just add and terminal 'next' ptr.
		nBytesTmp = mem->nBytes;
		R_CAST(freeObjectS *, mem)->nBytes = nBytesTmp;
		R_CAST(freeObjectS *, mem)->next = __KNULL;
		block->firstObject = R_CAST(freeObjectS *, mem);
		block->refCount--;

		head.lock.release();
		return;
	};
}
		

memoryBogC::bogBlockS *memoryBogC::getNewBlock(void)
{
	bogBlockS	*ret;

	ret = new ((memoryTrib.__kmemoryStream.*
		memoryTrib.__kmemoryStream.memAlloc)(
			PAGING_BYTES_TO_PAGES(
				blockSize + sizeof(bogBlockS)), 0)) bogBlockS;

	if (ret == __KNULL) {
		return __KNULL;
	};

	ret->next = __KNULL;
	ret->refCount = 0;
	ret->firstObject = R_CAST(freeObjectS *,
		( R_CAST(uarch_t, ret) + sizeof(bogBlockS)) );

	ret->firstObject->nBytes = blockSize;
	ret->firstObject->next = __KNULL;

	__kprintf(NOTICE MEMBOG"New bog block @v 0x%X, 1stObj 0x%X, 1stObj "
		"nBytes 0x%X.\n",
		ret, ret->firstObject, ret->firstObject->nBytes);

	return ret;
}
