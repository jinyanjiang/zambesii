
#include <arch/arch.h>
#include <__kstdlib/__kmath.h>


static const uarch_t		shiftTab[] =
{
	1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384,
	32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304,
	8388608, 16777216, 33554432, 67108864, 134217728, 268435456, 536870912,
	1073741824

	// For 64-bit build, we'll generate more numbers here.
};
	

ubit16 getShiftFor(uarch_t num)
{
	if (num == 0) {
		return __WORDSIZE - 1;
	};

	for (ubit16 i=0; i<__WORDSIZE - 1; i++)
	{
		if (shiftTab[i] == num) {
			return i;
		};
	};

	return 0;
}

static const utf8Char *const strErrorTable[] =
{
	CC"SUCCESS", CC"GENERAL", CC"UNKNOWN", CC"CRITICAL", CC"FATAL",
		CC"", CC"", CC"", CC"", CC"",

	CC"INVALID_ARG", CC"INVALID_ARG_VAL", CC"INVALID_OPERATION",
		CC"INVALID_RESOUCE_FORMAT", CC"INVALID_FORMAT",
		CC"", CC"", CC"", CC"", CC"",

	CC"MEMORY_NOMEM", CC"MEMORY_NOMEM_VIRTUAL", CC"MEMORY_NOMEM_PHYSICAL",
		CC"MEMORY_VIRTUAL_PAGEMAP", CC"MEMORY_NOMEM_IN_CONSTRUCTOR",
		CC"", CC"", CC"", CC"", CC"",

	CC"UNIMPLEMENTED", CC"UNSUPPORTED", CC"UNINITIALIZED", CC"UNAUTHORIZED",
		CC"", CC"", CC"", CC"", CC"", CC"",

	CC"RESOURCE_BUSY", CC"RESOURCE_UNAVAILABLE", CC"RESOURCE_EXHAUSTED",
		CC"", CC"", CC"", CC"", CC"", CC"", CC"",

	CC"WOULD_BLOCK", CC"NOT_FOUND", CC"LIMIT_OVERFLOWED",
		CC"", CC"", CC"", CC"", CC"", CC"", CC""
};

const utf8Char *strerror(error_t err)
{
	if (err > 0) { return (const utf8Char *)"Invalid error code"; };

	if (err != 0) { err = -err; };
	return strErrorTable[err];
}

