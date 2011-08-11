
#include <arch/interrupts.h>
#include <asm/cpuControl.h>
#include <__kstdlib/__kflagManipulation.h>


x86Lapic::sendPhysicalIpi(ubit8 type, ubit8 vector, ubit8 dest)
{
	ubit32		val, timeout=800;

	// For SIPI, the vector field is ignored.
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
		| (dest << x86LAPIC_IPI_SHORTDEST_SHIFT));

	while (__KFLAG_TEST(
		x86Lapic::read32(x86LAPIC_REG_INT_CMD0),
		x86LAPIC_IPI_DELIVERY_STATUS_PENDING) && timeout != 0)
	{
		timeout--;
		cpuControl::subZero();
	};
	
	// Check for successful delivery here.
}

