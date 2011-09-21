
#include <arch/interrupts.h>
#include <asm/cpuControl.h>
#include <__kstdlib/__kflagManipulation.h>
#include <__kclasses/debugPipe.h>
#include <commonlibs/libx86mp/libx86mp.h>


error_t x86Lapic::sendPhysicalIpi(
	ubit8 type, ubit8 vector, ubit8 shortDest, cpu_t dest
	)
{
	// Timeout value is the one used by Linux.
	ubit32		timeout=1000;

	// For SIPI, the vector field is ignored.
	if (shortDest == x86LAPIC_IPI_SHORTDEST_NONE) {
		x86Lapic::write32(x86LAPIC_REG_INT_CMD1, dest << 24);
	};

	x86Lapic::write32(
		x86LAPIC_REG_INT_CMD0,
		vector
		| (type << x86LAPIC_IPI_TYPE_SHIFT)
		| (x86LAPIC_IPI_DESTMODE_PHYS << x86LAPIC_IPI_DESTMODE_SHIFT)
		| (((type == x86LAPIC_IPI_TYPE_ARBIT_RESET) ?
			x86LAPIC_IPI_LEVEL_ARBIT_RESET
			: x86LAPIC_IPI_LEVEL_OTHER)
			<< x86LAPIC_IPI_LEVEL_SHIFT)
		| (x86LAPIC_IPI_TRIGG_EDGE << x86LAPIC_IPI_TRIGG_SHIFT)
		| (shortDest << x86LAPIC_IPI_SHORTDEST_SHIFT));

	while (__KFLAG_TEST(
		x86Lapic::read32(x86LAPIC_REG_INT_CMD0),
		x86LAPIC_IPI_DELIVERY_STATUS_PENDING) && timeout != 0)
	{
		timeout--;
		cpuControl::subZero();
	};

	// Check for successful delivery here.
	return (timeout != 0) ? ERROR_SUCCESS : ERROR_UNKNOWN;
}
