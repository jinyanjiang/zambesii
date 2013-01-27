#ifndef ___KORIENTATION_H
	#define ___KORIENTATION_H

	#include <multiboot.h>
	#include <arch/paging.h>
	#include <__kstdlib/__ktypes.h>
	#include <kernel/common/panic.h>
	#include <kernel/common/task.h>

#define ORIENT_QUOTE(x)			#x

#define DO_OR_DIE(__trib,__method,__ret)			\
	__ret = __trib.__method; \
	if (__ret != ERROR_SUCCESS) \
	{ \
		panic( \
			__ret, \
			FATAL"Kernel Orientation: Failure to initialize " \
			ORIENT_QUOTE(__trib)" with method " \
			ORIENT_QUOTE(__method)".\n"); \
	}

#define ORIENT				"Orientation: "

extern "C" void __korientationInit(ubit32 mbMagic, multibootDataS *mbData);
extern "C" void __korientationMain(void);

extern taskC		__korientationThread;
extern ubit8		__korientationStack[];
extern ubit8		__korientationPreallocatedBmpMem[][64];

#endif

