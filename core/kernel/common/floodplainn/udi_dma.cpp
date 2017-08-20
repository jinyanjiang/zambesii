
#include <arch/walkerPageRanger.h>
#include <__kstdlib/__kclib/assert.h>
#include <kernel/common/memoryTrib/vaddrSpaceStream.h>
#include <kernel/common/processTrib/processTrib.h>
#include <kernel/common/floodplainn/zudi.h>

utf8Char *fplainn::Zudi::dma::DmaConstraints::attrTypeNames[N_ATTR_TYPE_NAMES] = {
	CC"ADDRESSABLE_BITS",
	CC"ALIGNMENT_BITS",

	CC"DATA_ADDRESSABLE_BITS",
	CC"NO_PARTIAL",

	CC"SCGTH_MAX_ELEMENTS",
	CC"SCGTH_FORMAT",
	CC"SCGTH_ENDIANNESS",
	CC"SCGTH_ADDRESSABLE_BITS",
	CC"SCGTH_MAX_SEGMENTS",

	CC"SCGTH_ALIGNMENT_BITS",
	CC"SCGTH_MAX_EL_PER_SEG",
	CC"SCGTH_PREFIX_BYTES",

	CC"ELEMENT_ALIGNMENT_BITS",
	CC"ELEMENT_LENGTH_BITS",
	CC"ELEMENT_GRANULARITY_BITS",

	CC"ADDR_FIXED_BITS",
	CC"ADDR_FIXED_TYPE",
	CC"ADDR_FIXED_VALUE_LO",
	CC"ADDR_FIXED_VALUE_HI",

	CC"SEQUENTIAL",
	CC"SLOP_IN_BITS",
	CC"SLOP_OUT_BITS",
	CC"SLOP_OUT_EXTRA",
	CC"SLOP_BARRIER_BITS",

	CC"LITTLE_ENDIAN",
	CC"BIG_ENDIAN",

	CC"FIXED_ELEMENT",
	CC"FIXED_LIST",
	CC"FIXED_VALUE"
};

static utf8Char		*unknownString = CC"<UNKNOWN>";

utf8Char *fplainn::Zudi::dma::DmaConstraints::getAttrTypeName(
	udi_dma_constraints_attr_t a
	)
{
	if (a == 0 || a > UDI_DMA_SLOP_BARRIER_BITS) { return unknownString; };

	if (a >= UDI_DMA_SEQUENTIAL)
		{ return attrTypeNames[a - UDI_DMA_SEQUENTIAL + 19]; };

	if (a >= UDI_DMA_ADDR_FIXED_BITS)
		{ return attrTypeNames[a - UDI_DMA_ADDR_FIXED_BITS + 15]; };

	if (a >= UDI_DMA_ELEMENT_ALIGNMENT_BITS) {
		return attrTypeNames[a - UDI_DMA_ELEMENT_ALIGNMENT_BITS + 12];
	};

	if (a >= UDI_DMA_SCGTH_ALIGNMENT_BITS)
		{ return attrTypeNames[a - UDI_DMA_SCGTH_ALIGNMENT_BITS + 9]; };

	if (a >= UDI_DMA_SCGTH_MAX_ELEMENTS)
		{ return attrTypeNames[a - UDI_DMA_SCGTH_MAX_ELEMENTS + 4]; };

	if (a >= UDI_DMA_DATA_ADDRESSABLE_BITS) {
		return attrTypeNames[a - UDI_DMA_DATA_ADDRESSABLE_BITS + 2];
	};

	if (a >= UDI_DMA_ADDRESSABLE_BITS)
		{ return attrTypeNames[a - UDI_DMA_ADDRESSABLE_BITS + 0]; };

	return unknownString;
}

void fplainn::Zudi::dma::DmaConstraints::dump(void)
{
	printf(NOTICE"DMA Constraints obj @%p, %d attrs: dumping.\n",
		this, attrs.getNIndexes());

	for (AttrArray::Iterator it=attrs.begin(); it != attrs.end(); ++it)
	{
		udi_dma_constraints_attr_spec_t		*tmp=&*it;

		printf(CC"\tAttr %s,\t\tValue %x.\n",
			getAttrTypeName(tmp->attr_type),
			tmp->attr_value);
	};
}

error_t fplainn::Zudi::dma::DmaConstraints::addOrModifyAttrs(
	udi_dma_constraints_attr_spec_t *_attrs, uarch_t nAttrs
	)
{
	error_t			ret;
	uarch_t			nNewAttrs=0;

	if (_attrs == NULL) { return ERROR_INVALID_ARG; };

	// How many of the attrs are new ones?
	for (uarch_t i=0; i<nAttrs; i++) {
		if (!attrAlreadySet(_attrs[i].attr_type)) { nNewAttrs++; };
	};

	// Make room for all the new attrs.
	if (nNewAttrs > 0)
	{
		uarch_t			prevNIndexes = attrs.getNIndexes();

		ret = attrs.resizeToHoldIndex(
			prevNIndexes + nNewAttrs - 1);

		if (ret != ERROR_SUCCESS) { return ret; };

		/* Set all the new attrs' attr_types to 0 to distinguish them.
		 * It should not be necessary to lock this operation.
		 */
		for (uarch_t i=0; i<nNewAttrs; i++) {
			attrs[prevNIndexes + i].attr_type = 0;
		};
	};

	for (AttrArray::Iterator it=attrs.begin(); it != attrs.end(); ++it)
	{
		udi_dma_constraints_attr_spec_t		*spec=&*it;

		/* If it's a newly allocated attr (attr_type=0), search the
		 * passed attrs for one that's new, and put it in this slot.
		 **/
		if (spec->attr_type == 0)
		{
			for (uarch_t i=0; i<nAttrs; i++)
			{
				if (attrAlreadySet(_attrs[i].attr_type))
					{ continue; };

				*spec = _attrs[i];
			};
		}
		/* Else search the passed attrs to see if the caller wanted this
		 * attr modified.
		 **/
		else
		{
			for (uarch_t i=0; i<nAttrs; i++)
			{
				if (spec->attr_type != _attrs[i].attr_type)
					{ continue; };

				spec->attr_value = _attrs[i].attr_value;
			};
		};
	};

	return ERROR_SUCCESS;
}

error_t fplainn::Zudi::dma::MappedScatterGatherList::addPages(
	void *vaddr, uarch_t nBytes
	)
{
	uarch_t nPages;
	error_t ret;

	assert_warn(nBytes % PAGING_BASE_SIZE == 0);
	nPages = __KMATH_NELEMENTS(nBytes, PAGING_BASE_SIZE);

	ret = pageArray.resizeToHoldIndex(pageArray.getNIndexes() + nPages - 1);
	if (ret != ERROR_SUCCESS) {
		return ret;
	}

	for (uarch_t i=0; i<nPages; i++)
	{
		pageArray[i] = (void *)((uintptr_t)vaddr
			+ (i * PAGING_BASE_SIZE));
	};

	return ERROR_SUCCESS;
}

error_t fplainn::Zudi::dma::ScatterGatherList::map(
	MappedScatterGatherList *retobj
	)
{
	status_t		ret;
	uarch_t			nFrames=0;
	void			*vmem=NULL;
	uintptr_t		currVaddr=0;
	VaddrSpaceStream	*__kvasStream;

	/**	EXPLANATION:
	 * First count the number of pages we have to allocate in order to
	 * fully map the entire SGList. Then allocate the vmem to map it,
	 * and well, map it.
	 **/
	if (retobj == NULL) { return ERROR_INVALID_ARG_VAL; };

	__kvasStream = processTrib.__kgetStream()->getVaddrSpaceStream();

	for (ubit8 pass=1; pass <= 2; pass++)
	{
		if (addressSize == ADDR_SIZE_32)
		{
			for (
				SGList32::Iterator it=elements32.begin();
				it != elements32.end();
				++it)
			{
				udi_scgth_element_32_t		*tmp = &*it;
				paddr_t				p, p2;
				uarch_t				currNFrames;

				p = tmp->block_busaddr;
				p2 = p + tmp->block_length;

				currNFrames = PAGING_BYTES_TO_PAGES(p2 - p)
					.getLow();

				if (pass == 1) {
					nFrames += currNFrames;
				};

				if (pass == 2)
				{
					if (vmem == NULL)
					{
						// Alloc the vmem.
						vmem = __kvasStream->getPages(nFrames);
						if (vmem == NULL)
						{
							printf(ERROR"SGList::map: "
								"Failed to alloc vmem "
								"to map the %d "
								"frames in the list.\n",
								nFrames);

							return ERROR_MEMORY_NOMEM_VIRTUAL;
						};

						currVaddr = (uintptr_t)vmem;
					}

					ret = walkerPageRanger::mapInc(
						&__kvasStream->vaddrSpace,
						(void *)currVaddr, p,
						currNFrames,
						PAGEATTRIB_PRESENT
						| PAGEATTRIB_WRITE
						| PAGEATTRIB_SUPERVISOR);

					if (ret < (signed)currNFrames)
					{
						printf(ERROR"SGList::map: "
							"Failed to map all %d "
							"pages for SGList "
							"element P%P, %d "
							"frames.\n",
							&p, nFrames);

						goto releaseVmem;
					};

					ret = retobj->addPages(
						(void *)currVaddr,
						currNFrames * PAGING_BASE_SIZE);

					if (ret != ERROR_SUCCESS)
					{
						printf(ERROR"SGList::map: "
							"Failed to add "
							"page %p in DMA "
							"sglist mapping to "
							"internal metadata "
							"list.\n",
						(void*)currVaddr);
					}

					currVaddr += currNFrames
						* PAGING_BASE_SIZE;
				};
			};
		}
		else
		{
			for (
				SGList64::Iterator it=elements64.begin();
				it != elements64.end();
				++it)
			{
				udi_scgth_element_64_t		*tmp = &*it;
				paddr_t				p, p2;
				uarch_t				currNFrames;

				assign_busaddr64_to_paddr(
					p, tmp->block_busaddr);

				p2 = p + tmp->block_length;

				currNFrames = PAGING_BYTES_TO_PAGES(p2 - p)
					.getLow();

				if (pass == 1) {
					nFrames += currNFrames;
				};

				if (pass == 2)
				{
					if (vmem == NULL)
					{
						// Alloc the vmem.
						vmem = __kvasStream->getPages(nFrames);
						if (vmem == NULL)
						{
							printf(ERROR"SGList::map: "
								"Failed to alloc vmem "
								"to map the %d "
								"frames in the list.\n",
								nFrames);

							return ERROR_MEMORY_NOMEM_VIRTUAL;
						};

						currVaddr = (uintptr_t)vmem;
					}

					ret = walkerPageRanger::mapInc(
						&__kvasStream->vaddrSpace,
						(void *)currVaddr, p,
						currNFrames,
						PAGEATTRIB_PRESENT
						| PAGEATTRIB_WRITE
						| PAGEATTRIB_SUPERVISOR);

					if (ret < (signed)currNFrames)
					{
						printf(ERROR"SGList::map: "
							"Failed to map all %d "
							"pages for SGList "
							"element P%P, %d "
							"frames.\n",
							&p, nFrames);

						goto releaseVmem;
					};

					ret = retobj->addPages(
						(void *)currVaddr,
						currNFrames * PAGING_BASE_SIZE);

					if (ret != ERROR_SUCCESS)
					{
						printf(ERROR"SGList::map: "
							"Failed to add "
							"page %p in DMA "
							"sglist mapping to "
							"internal metadata "
							"list.\n",
						(void*)currVaddr);
					}

					currVaddr += currNFrames
						* PAGING_BASE_SIZE;
				};
			};
		};
	};

	/**	FIXME: Build the page list here.
	 **/

	return ERROR_SUCCESS;

releaseVmem:
	__kvasStream->releasePages(vmem, nFrames);
	return ERROR_MEMORY_VIRTUAL_PAGEMAP;
}

udi_dma_constraints_attr_spec_t *fplainn::Zudi::dma::DmaConstraints::getAttr(
	udi_dma_constraints_attr_t attr
	)
{
	attrs.lock();

	for (AttrArray::Iterator it = attrs.begin(); it != attrs.end(); ++it)
	{
		udi_dma_constraints_attr_spec_t		*tmp = &*it;

		if (tmp->attr_type != attr) { continue; };

		attrs.unlock();
		return tmp;
	};

	attrs.unlock();
	return NULL;
}

void fplainn::Zudi::dma::DmaConstraints::Compiler::dump(void)
{
	printf(NOTICE"DMACon Compiler @%p: Parent @%p. Dumping.\n"
		"\tCan address %u bits, starting at PFN %x upto PFN %x.\n"
		"\tSkip stride of %d frames, and allocates in blocks of at least %d frames\n"
		"\tSlop in is %d bits, out is %d bits.\n",
		this, parent,
		i.addressableBits, i.startPfn, i.beyondEndPfn - 1,
		i.pfnSkipStride, i.minElementGranularityNFrames,
		i.slopInBits, i.slopOutBits);
}

error_t fplainn::Zudi::dma::DmaConstraints::Compiler::compile(void)
{
	const udi_dma_constraints_attr_spec_t	*tmpAttr;

	/**	EXPLANATION:
	 * We take a constraints specification, and try to allocate "nFrames"
	 * frames worth of RAM in accordance with it.
	 *
	 *	1. UDI_DMA_DATA_ADDRESSABLE_BITS tells us which bit in the BMP
	 *		we have to start at.
	 *	2. UDI_DMA_ADDR_FIXED_BITS further constrains us to some starting
	 *		bit.
	 *	3. UDI_DMA_ELEMENT_ALIGNMENT_BITS tells us our skip-stride when
	 *		allocating.
	 *	4. UDI_DMA_ELEMENT_GRANULARITY_BITS tells us the minimum size
	 *		for each element in the list. We reject any constraints spec
	 *		that speciifies a minimum granularity less than
	 *		PAGING_BASE_SIZE.
	 *	5. UDI_DMA_ELEMENT_LENGTH_BITS tells us the max size each
	 *		element can be.
	 *
	 *	6. UDI_DMA_SCGTH_MAX_EL_PER_SEG tells us when to break into
	 *		a new segment.
	 *	7. UDI_DMA_SCGTH_MAX_ELEMENTS tells us when we should break out
	 *		and fail, telling the caller that we couldn't allocate enough
	 *		frames without violating its MAX_ELEMENTS constraint.
	 *
	 * * We handle SLOP by allocating an extra frame before and after the
	 *   constraint-obeying frames. This opens us up to a DoS attack in
	 *   which the caller can modify his SLOP specification such that we
	 *   aren't aware of the SLOP rounding when freeing a DMA allocation
	 *   that had slop.
	 * * We do not support SLOP greater than PAGING_BASE_SIZE.
	 * * We currently perform no optimizations for the case of
	 *   UDI_DMA_SEQUENTIAL.
	 * * We do not support UDI_SCGTH_DMA_MAPPED.
	 * * We don't support UDI_DMA_FIXED_LIST.
	 *
	 * So what we're trying to do is allocate nFrames, starting at
	 * UDI_DMA_DATA_ADDRESSABLE_BITS + UDI_DMA_ADDR_FIXED_*.
	 *
	 * This function doesn't care when we break into new elements, etc. Just
	 * concerns itself with allocating the frames and adding them to the
	 * retlist.
	 */

	i.minElementGranularityNFrames = 1;
	i.maxNContiguousFrames = 1;

	// Don't allocate frames beyond what the DMA engine can address.
	i.addressableBits = __PADDR_NBITS__;
	i.beyondEndPfn = 0;
	i.beyondEndPfn = ~i.beyondEndPfn;
	tmpAttr = parent->getAttr(UDI_DMA_DATA_ADDRESSABLE_BITS);
	if (tmpAttr != NULL)
	{
		i.addressableBits = tmpAttr->attr_value;
		// Clamp addressableBits down to __PADDR_NBITS__.
		if (i.addressableBits > __PADDR_NBITS__) {
			i.addressableBits = __PADDR_NBITS__;
		};

		if (i.addressableBits >= PAGING_BASE_SHIFT)
		{
			i.beyondEndPfn = paddr_t(1)
				<< (i.addressableBits - PAGING_BASE_SHIFT);
		}
		else {
			/* If the engine can only address the first frame, then
			 * limit allocation to the first frame
			 */
			i.beyondEndPfn = 1;
		};
	};

	/* Skip stride is # to skip - 1, so that we can always do
	 * a += (1 + stride) in the loop bound increment step.
	 * Skip stride ensures we meet element alignment requirements.
	 */
	i.pfnSkipStride = 0;
	tmpAttr = parent->getAttr(UDI_DMA_ELEMENT_ALIGNMENT_BITS);
	if (tmpAttr != NULL && tmpAttr->attr_value > PAGING_BASE_SHIFT) {
		i.pfnSkipStride = (1 << (tmpAttr->attr_value - PAGING_BASE_SHIFT)) - 1;
	};

	i.startPfn = 0;
	tmpAttr = parent->getAttr(UDI_DMA_ADDR_FIXED_BITS);
	if (tmpAttr != NULL && tmpAttr->attr_value > 0)
	{
		paddr_t					tmpFixed,
							beyondEndOfFixedPfn;
		const udi_dma_constraints_attr_spec_t	*tmpFixedType,
							*tmpFixedLo,*tmpFixedHi;

		tmpFixedType = parent->getAttr(UDI_DMA_ADDR_FIXED_TYPE);
		if (tmpFixedType != NULL && tmpFixedType->attr_value == UDI_DMA_FIXED_LIST)
		{
			printf(WARNING MEMBMP"constrainedGF: "
				"FIXED_LIST unsupported. Rejecting.\n");

			return ERROR_UNSUPPORTED;
		};

		/* We ignore UDI_DMA_FIXED_ELEMENT because it has no practical
		 * meaning, as far as I can tell.
		 */
		if (tmpFixedType != NULL && tmpFixedType->attr_value == UDI_DMA_FIXED_ELEMENT)
		{
			printf(WARNING MEMBMP"constrainedGF: FIXED_ELEMENT "
				"silently ignored.\n");
		};

		if (tmpFixedType != NULL && tmpFixedType->attr_value == UDI_DMA_FIXED_VALUE)
		{
			/**	EXPLANATION:
			 * Extract the fixed bits in a PAE-compatible way, and
			 * use them to determine what changes need to be made to the
			 * start and beyondEnd PFNs.
			 */
			tmpFixedLo = parent->getAttr(UDI_DMA_ADDR_FIXED_VALUE_LO);
			tmpFixedHi = parent->getAttr(UDI_DMA_ADDR_FIXED_VALUE_HI);

#ifndef CONFIG_ARCH_x86_32_PAE
			if (tmpFixedHi != NULL && tmpFixedHi->attr_value != 0) {
				printf(ERROR MEMBMP"constrainedGF: Fixed value "
					"hi constraint in a non-PAE build.\n\t"
					"Unsupported. Hi value is %x.\n",
					tmpFixedHi->attr_value);

				return ERROR_UNSUPPORTED;
			};
#endif

			tmpFixed = paddr_t(
#ifdef CONFIG_ARCH_x86_32_PAE
				((tmpFixedHi != NULL) ? tmpFixedHi->attr_value : 0),
#endif
				((tmpFixedLo != NULL) ? tmpFixedLo->attr_value : 0));

			i.startPfn = (tmpFixed << (i.addressableBits - i.fixedBits))
				>> PAGING_BASE_SHIFT;

			/* Because beyondEndOfFixedPfn is calculated relative to
			 * startPfn, we don't have to check later on to see if
			 * beyondEndOfFixedPfn is before startPfn.
			 */
			beyondEndOfFixedPfn = ((tmpFixed + 1)
				<< (i.addressableBits - i.fixedBits))
				>> PAGING_BASE_SHIFT;

			/* We also have to modify beyondEndPfn based on the
			 * fixed bits: the fixed bits' beyond-end PFN
			 * will be our new beyondEndPfn, as long as it's
			 * not outside the range of this BMP.
			 *
			 *	XXX:
			 * Don't need to check if beyondEndOfFixedPfn is before
			 * startPfn, because addressableBits was already checked.
			 */
			if (beyondEndOfFixedPfn > i.beyondEndPfn) {
				beyondEndOfFixedPfn = i.beyondEndPfn;
			}
		};

	};

	/* There's nothing really to check for here. Perhaps we could check to
	 * ensure that the element granularity wouldn't cause the first attempt
	 * to cross beyond the end of the BMP? Other than that, no harm
	 * can come from values in this attribute.
	 */
	tmpAttr = parent->getAttr(UDI_DMA_ELEMENT_GRANULARITY_BITS);
	if (tmpAttr != NULL && tmpAttr->attr_value > PAGING_BASE_SHIFT)
	{
		i.minElementGranularityNFrames = 1
			<< (tmpAttr->attr_value - PAGING_BASE_SHIFT);
	};

	tmpAttr = parent->getAttr(UDI_DMA_ELEMENT_LENGTH_BITS);
	if (tmpAttr != NULL) {
		i.maxNContiguousFrames = tmpAttr->attr_value - PAGING_BASE_SHIFT;
	}

	// maxNContiguousFrames cannot be less than minElementGranularityNFrames.
	if (i.maxNContiguousFrames < i.minElementGranularityNFrames)
	{
		printf(ERROR MEMBMP"constrainedGF: Minimum granularity is %d "
			"frames, but max element size is %d frames.\n",
			i.minElementGranularityNFrames, i.maxNContiguousFrames);

		return ERROR_INVALID_ARG_VAL;
	};

	/* We don't yet support slop. Especially not SLOP-IN. */
	i.slopInBits = i.slopOutBits = i.slopOutExtra = 0;

	tmpAttr = parent->getAttr(UDI_DMA_SLOP_IN_BITS);
	if (tmpAttr != NULL) { i.slopInBits = tmpAttr->attr_value; };
	tmpAttr = parent->getAttr(UDI_DMA_SLOP_OUT_BITS);
	if (tmpAttr != NULL) { i.slopOutBits = tmpAttr->attr_value; };
	tmpAttr = parent->getAttr(UDI_DMA_SLOP_OUT_EXTRA);
	if (tmpAttr != NULL) { i.slopOutExtra = tmpAttr->attr_value; };

	if (i.slopInBits != 0 || i.slopOutBits != 0 || i.slopOutExtra != 0)
	{
		printf(ERROR MEMBMP"constrainedGF: SLOP is unsupported\n"
			"\tSLOP-IN %d, SLOP-OUT %d, SLOP-EXTRA %d.\n",
			i.slopInBits, i.slopOutBits, i.slopOutExtra);

		return ERROR_UNSUPPORTED;
	};

	return ERROR_SUCCESS;
}
