
#include <arch/paddr_t.h>
#include <arch/paging.h>
#include <arch/walkerPageRanger.h>
#include <__kstdlib/__kclib/string.h>
#include <commonlibs/libacpi/rxsdt.h>
#include <kernel/common/memoryTrib/memoryTrib.h>


static void	*sdtTmp;

static void *acpi_tmpMapSdt(paddr_t p)
{
	status_t	nMapped;

	if (sdtTmp == __KNULL)
	{
		sdtTmp = (memoryTrib.__kmemoryStream.vaddrSpaceStream
			.*memoryTrib.__kmemoryStream.vaddrSpaceStream.getPages)(
			2);

		if (sdtTmp == __KNULL) {
			return __KNULL;
		};
	};

	nMapped = walkerPageRanger::mapInc(
		&memoryTrib.__kmemoryStream.vaddrSpaceStream.vaddrSpace,
		sdtTmp, p, 2,
		PAGEATTRIB_PRESENT | PAGEATTRIB_SUPERVISOR);

	if (nMapped < 2)
	{
		memoryTrib.__kmemoryStream.vaddrSpaceStream.releasePages(
			sdtTmp, 2);

		return __KNULL;
	};
	return sdtTmp;
}

static void *acpi_mapTable(paddr_t p, uarch_t nPages)
{
	void		*ret;
	status_t	nMapped;

	ret = (memoryTrib.__kmemoryStream.vaddrSpaceStream
		.*memoryTrib.__kmemoryStream.vaddrSpaceStream.getPages)(nPages);

	if (ret == __KNULL) {
		return __KNULL;
	};

	nMapped = walkerPageRanger::mapInc(
		&memoryTrib.__kmemoryStream.vaddrSpaceStream.vaddrSpace,
		ret, p, nPages,
		PAGEATTRIB_PRESENT | PAGEATTRIB_SUPERVISOR);

	if (nMapped < static_cast<status_t>( nPages ))
	{
		memoryTrib.__kmemoryStream.vaddrSpaceStream.releasePages(
			ret, nPages);

		return __KNULL;
	};

	ret = reinterpret_cast<void *>(
		(uarch_t)ret + (p & PAGING_BASE_MASK_LOW) );

	return ret;
}

acpi_rSratS *acpiRsdt::getNextSrat(acpi_rsdtS *rsdt, void **const handle)
{
	acpi_sdtS	*sdt;
	acpi_rSratS	*ret=__KNULL;

	if (*handle == __KNULL) {
		*handle = ACPI_TABLE_GET_FIRST_ENTRY(rsdt);
	};

	for (; *handle < ACPI_TABLE_GET_ENDADDR(rsdt); )
	{
		sdt = (acpi_sdtS *)acpi_tmpMapSdt(*(paddr_t *)*handle);
		sdt = reinterpret_cast<acpi_sdtS *>(
			(uarch_t)sdt
			+ ((*(paddr_t *)*handle) & PAGING_BASE_MASK_LOW) );

		if (strncmp(sdt->sig, ACPI_SDT_SIG_SRAT, 4) == 0)
		{
			ret = (acpi_rSratS *)acpi_mapTable(
				*(paddr_t *)*handle,
				PAGING_BYTES_TO_PAGES(sdt->tableLength) + 1);
		};

		*handle = reinterpret_cast<void *>( (uarch_t)*handle + 4 );
		if (ret != __KNULL) {
			return ret;
		};
	};

	return ret;
}

acpi_rMadtS *acpiRsdt::getNextMadt(acpi_rsdtS *rsdt, void **const handle)
{
	acpi_sdtS	*sdt;
	acpi_rMadtS	*ret=__KNULL;

	if (*handle == __KNULL) {
		*handle = ACPI_TABLE_GET_FIRST_ENTRY(rsdt);
	};

	for (; *handle < ACPI_TABLE_GET_ENDADDR(rsdt); )
	{
		sdt = (acpi_sdtS *)acpi_tmpMapSdt(*(paddr_t *)*handle);
		sdt = reinterpret_cast<acpi_sdtS *>(
			(uarch_t)sdt
			+ ((*(paddr_t *)*handle) & PAGING_BASE_MASK_LOW) );

		if (strncmp(sdt->sig, ACPI_SDT_SIG_APIC, 4) == 0)
		{
			ret = (acpi_rMadtS *)acpi_mapTable(
				*(paddr_t *)*handle,
				PAGING_BYTES_TO_PAGES(sdt->tableLength) + 1);
		};

		*handle = reinterpret_cast<void *>( (uarch_t)*handle + 4 );
		if (ret != __KNULL) {
			return ret;
		};
	};

	return ret;
}

acpi_rFacpS *acpiRsdt::getNextFacp(acpi_rsdtS *rsdt, void **const handle)
{
	acpi_sdtS	*sdt;
	acpi_rFacpS	*ret=__KNULL;

	if (*handle == __KNULL) {
		*handle = ACPI_TABLE_GET_FIRST_ENTRY(rsdt);
	};

	for (; *handle < ACPI_TABLE_GET_ENDADDR(rsdt); )
	{
		sdt = (acpi_sdtS *)acpi_tmpMapSdt(*(paddr_t *)*handle);
		sdt = reinterpret_cast<acpi_sdtS *>(
			(uarch_t)sdt
			+ ((*(paddr_t *)*handle) & PAGING_BASE_MASK_LOW) );

		if (strncmp(sdt->sig, ACPI_SDT_SIG_FACP, 4) == 0)
		{
			ret = (acpi_rFacpS *)acpi_mapTable(
				*(paddr_t *)*handle,
				PAGING_BYTES_TO_PAGES(sdt->tableLength) + 1);
		};

		*handle = reinterpret_cast<void *>( (uarch_t)*handle + 4 );
		if (ret != __KNULL) {
			return ret;
		};
	};

	return ret;
}

void acpiRsdt::destroySdt(acpi_sdtS *sdt)
{
	uarch_t		nPages, f;
	paddr_t		p;

	if (sdt == __KNULL) {
		return;
	};

	// Find out how many pages:
	nPages = PAGING_BYTES_TO_PAGES(sdt->tableLength) + 1;
	walkerPageRanger::unmap(
		&memoryTrib.__kmemoryStream.vaddrSpaceStream.vaddrSpace,
		sdt, &p, nPages, &f);

	memoryTrib.__kmemoryStream.vaddrSpaceStream.releasePages(sdt, nPages);
}

