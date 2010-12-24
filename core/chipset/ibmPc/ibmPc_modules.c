
#include "memoryMod.h"
#include "terminalMod.h"
#include "pic.h"


struct intControllerModS	ibmPc_intControllerMod =
{
	&ibmPc_pic_initialize,
	&ibmPc_pic_shutdown,
	&ibmPc_pic_suspend,
	&ibmPc_pic_restore,

	&ibmPc_pic_maskAll,
	&ibmPc_pic_maskSingle,
	&ibmPc_pic_unmaskAll,
	&ibmPc_pic_unmaskSingle
};

struct memoryModS	ibmPc_memoryMod =
{
	&ibmPc_memoryMod_initialize,
	&ibmPc_memoryMod_shutdown,
	&ibmPc_memoryMod_suspend,
	&ibmPc_memoryMod_restore,

	&ibmPc_memoryMod_getNumaMap,
	&ibmPc_memoryMod_getMemoryMap,
	&ibmPc_memoryMod_getMemoryConfig
};

// struct cpuModS		ibmPc_cpuMod;
struct debugModS	ibmPc_terminalMod =
{
	&ibmPc_terminalMod_initialize,
	&ibmPc_terminalMod_shutdown,
	&ibmPc_terminalMod_suspend,
	&ibmPc_terminalMod_awake,

	&ibmPc_terminalMod_isInitialized,
	&ibmPc_terminalMod_syphon,
	&ibmPc_terminalMod_clear
};
