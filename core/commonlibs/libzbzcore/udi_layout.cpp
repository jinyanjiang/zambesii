/*
 * Taken from the UDI Reference Implementation.
 * File: env/common/udi_layout.c
 */

/*
 * $Copyright udi_reference:
 *
 *
 *    Copyright (c) 1995-2001; Compaq Computer Corporation; Hewlett-Packard
 *    Company; Interphase Corporation; The Santa Cruz Operation, Inc;
 *    Software Technologies Group, Inc; and Sun Microsystems, Inc
 *    (collectively, the "Copyright Holders").  All rights reserved.
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the conditions are met:
 *
 *            Redistributions of source code must retain the above
 *            copyright notice, this list of conditions and the following
 *            disclaimer.
 *
 *            Redistributions in binary form must reproduce the above
 *            copyright notice, this list of conditions and the following
 *            disclaimers in the documentation and/or other materials
 *            provided with the distribution.
 *
 *            Neither the name of Project UDI nor the names of its
 *            contributors may be used to endorse or promote products
 *            derived from this software without specific prior written
 *            permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *    "AS IS," AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *    HOLDERS OR ANY CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *    OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *    USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *    DAMAGE.
 *
 *    THIS SOFTWARE IS BASED ON SOURCE CODE PROVIDED AS A SAMPLE REFERENCE
 *    IMPLEMENTATION FOR VERSION 1.01 OF THE UDI CORE SPECIFICATION AND/OR
 *    RELATED UDI SPECIFICATIONS. USE OF THIS SOFTWARE DOES NOT IN AND OF
 *    ITSELF CONSTITUTE CONFORMANCE WITH THIS OR ANY OTHER VERSION OF ANY
 *    UDI SPECIFICATION.
 *
 *
 * $
 */

#define UDI_VERSION		0x101
#include <udi.h>
#define UDI_PHYSIO_VERSION	0x101
#include <udi_physio.h>
#include <__kstdlib/__kclib/assert.h>
#include <__kstdlib/__kclib/string8.h>
#include <__kclasses/debugPipe.h>
#include <kernel/common/panic.h>
#include <kernel/common/floodplainn/channel.h>


#define _UDI_PHYSIO_SUPPORTED		1

// FIXME: I do *not* like this macro at all.
#define _ported_udi_alignto(_item, boundary) \
  (((unsigned)((uintptr_t)(_item) + (boundary) - 1) >= (unsigned)((uintptr_t) (_item)))	\
   ? (((uintptr_t) (_item) + ((boundary) - 1)) & (~((uintptr_t)(boundary)-1)))	\
   : ~ (long) 0)

/*
 * Analyze a layout template and see how much core storage it will take.
 *
 * Has the additional benefit that it will validate the layout template
 * before we ever try to do anything "hard" with it.
 */

struct sLayoutElementDescriptor
{
	uarch_t		elementSize,
			layoutSkipCount;
};

sLayoutElementDescriptor layoutElements[] =
{
	{ 0, 0 },				// UDI_DL_END

	// #def = 1, index = 1 (Fixed width types)
	#define ZUDI_LAYOUT_ELEMDESC_INTEGRAL_BASE	(1)
	{ sizeof(udi_ubit8_t), 1 },		// UDI_DL_UBIT8_T
	{ sizeof(udi_sbit8_t), 1 },		// UDI_DL_SBIT8_T
	{ sizeof(udi_ubit16_t), 1 },		// UDI_DL_UBIT16_T
	{ sizeof(udi_sbit16_t), 1 },		// UDI_DL_SBIT16_T
	{ sizeof(udi_ubit32_t), 1 },		// UDI_DL_UBIT32_T
	{ sizeof(udi_sbit32_t), 1 },		// UDI_DL_SBIT32_T
	{ sizeof(udi_boolean_t), 1 },		// UDI_DL_BOOLEAN_T
	{ sizeof(udi_status_t), 1 },		// UDI_DL_STATUS_T

	// #def = 20, index = 9 (Abstract types)
	#define ZUDI_LAYOUT_ELEMDESC_ABSTRACT_BASE	(9)
	{ sizeof(udi_index_t), 1 },		// UDI_DL_INDEX_T

	// #def = 30, index = 10 (Opaque handles)
	#define ZUDI_LAYOUT_ELEMDESC_OPAQUE_BASE	(10)
	{ sizeof(udi_channel_t), 1 },		// UDI_DL_CHANNEL_T
	{ 0, 1 },				// INVALID
	{ sizeof(udi_origin_t), 1 },		// UDI_DL_ORIGIN_T

	// #def = 40, index = 13 (Indirect element descriptors)
	#define ZUDI_LAYOUT_ELEMDESC_INDIRECT_BASE	(13)
	{ sizeof(udi_buf_t *), 4 },		// UDI_DL_BUF
	{ sizeof(udi_cb_t *), 1 },		// UDI_DL_CB
	{ sizeof(void *), 1 },			// UDI_DL_INLINE_UNTYPED
	{ sizeof(void *), 1 },			// UDI_DL_INLINE_DRIVER_TYPED
	{ sizeof(void *), 1 },			// UDI_DL_MOVABLE_UNTYPED

	// #def = 50, index = 18 (Nested element descriptors)
	#define ZUDI_LAYOUT_ELEMDESC_NESTED_BASE	(18)
	{ sizeof(void *), 0 },			// UDI_DL_INLINE_TYPED
	{ sizeof(void *), 0 },			// UDI_DL_MOVABLE_TYPED
	{ 0, 0 },				// UDI_DL_ARRAY

	// #def = 200, index = 21 (Physio opaque handles)
	#define ZUDI_LAYOUT_ELEMDESC_PHYSIO_BASE	(21)
	{ sizeof(udi_pio_handle_t), 1 },	// UDI_DL_PIO_HANDLE_T
	{ sizeof(udi_dma_constraints_t), 1 }	// UDI_DL_DMA_CONSTRAINTS_T
};

udi_size_t fplainn::sChannelMsg::zudi_layout_get_element_size(
	const udi_layout_t element, const udi_layout_t nestedLayout[],
	udi_size_t *layoutSkipCount
	)
{
	udi_index_t		index=element;

	// Physio opaque handle.
	if (element >= 200)
	{
		index = index - 200 + ZUDI_LAYOUT_ELEMDESC_PHYSIO_BASE;
		goto out;
	};

	// Nested element.
	if (element >= 50)
	{
		if (nestedLayout == NULL) {
			*layoutSkipCount = 0; return 0;
		};

		index = index - 50 + ZUDI_LAYOUT_ELEMDESC_NESTED_BASE;

		if (element == UDI_DL_INLINE_TYPED
			|| element == UDI_DL_MOVABLE_TYPED)
		{
			/* For these, the next few bytes until the next
			 * UDI_DL_END describe the structure of the object
			 * pointed to by the pointer element. Skip these
			 * detailed descriptor bytes, and increase the caller's
			 * skip count by the number of detail bytes as well.
			 **/
			*layoutSkipCount = 1;
			for (
				udi_layout_t *curr=nestedLayout;
				*curr != UDI_DL_END;
				curr++)
				{ *layoutSkipCount += 1; };

			return layoutElements[index].elementSize;
		};

		if (element == UDI_DL_ARRAY)
		{
			udi_layout_t		*curr;
			udi_size_t		nElements, elemSize=0;

			/* Skip count starts at 2, because the caller will
			 * have to skip both the UDI_DL_ARRAY byte and the
			 * element_count byte that follows it.
			 **/
			*layoutSkipCount = 2;

			curr = nestedLayout;
			nElements = *nestedLayout;
			nestedLayout++;

			for (; *curr != UDI_DL_END; curr++)
			{
				udi_size_t		currLElemSize,
							currLElemSkip;

				currLElemSize = zudi_layout_get_element_size(
					*curr, curr + 1, &currLElemSkip);

				if (currLElemSkip == 0) {
					*layoutSkipCount = 0; return 0;
				};

				elemSize += currLElemSize;
				*layoutSkipCount += currLElemSkip;
			};

			return elemSize * nElements;
		}
		else
		{
			// Should never reach here.
			*layoutSkipCount = 0;
			return 0;
		};
	};

	// Indirect element pointer.
	if (element >= 40)
	{
		index = index - 40 + ZUDI_LAYOUT_ELEMDESC_INDIRECT_BASE;
		goto out;
	};

	// Opaque handle.
	if (element >= 30)
	{
		index = index - 30 + ZUDI_LAYOUT_ELEMDESC_OPAQUE_BASE;
		goto out;
	};

	// Abstract type.
	if (element >= 20)
	{
		index = index - 20 + ZUDI_LAYOUT_ELEMDESC_ABSTRACT_BASE;
		goto out;
	}
	else // Integral type.
	{
		index = index - 1 + ZUDI_LAYOUT_ELEMDESC_INTEGRAL_BASE;
		goto out;
	};

out:
	*layoutSkipCount = layoutElements[index].layoutSkipCount;
	return layoutElements[index].elementSize;
}

status_t fplainn::sChannelMsg::marshalStackArguments(
	ubit8 *dest, va_list args, udi_layout_t *layout
	)
{
	uarch_t				offset=0, skipCount;
	status_t			elemSize;

	for (
		udi_layout_t *curr=layout;
		curr != NULL && *curr != UDI_DL_END;
		curr += skipCount, offset += elemSize)
	{
		/* We don't actually try to care about the type or size of the
		 * argument itself by using va_arg. We are only using it to
		 * ensure that we get the correct stack pop width when taking
		 * the arguments off the stack.
		 **/
		elemSize = zudi_layout_get_element_size(
			*curr, curr + 1, &skipCount);

		if (elemSize < 0) { return elemSize; };

		// We don't support huge objects on the stack. Period.
		if (elemSize > 16) { return ERROR_UNSUPPORTED; };

		offset = align(offset, elemSize);
		switch (elemSize)
		{
		case 1: *(ubit8 *)(dest + offset) = va_arg(args, int); break;
		case 2: *(ubit16 *)(dest + offset) = va_arg(args, int); break;
		case 4: *(ubit32 *)(dest + offset) = va_arg(args, int); break;
		case 8:
#if __WORDSIZE == 32
			*(ubit32 *)(dest + offset) = va_arg(args, int);
			*(ubit32 *)(dest + offset + 4) = va_arg(args, int);
#elif __WORDSIZE >= 64
			*(ubit64 *)(dest + offset) = va_arg(args, int);
#else
#error "Unable to generate argument 8Byte marshalling for your arch wordsize."
#endif
			break;

		case 16:
#if __WORDSIZE == 32
			*(ubit32 *)(dest + offset) = va_arg(args, int);
			*(ubit32 *)(dest + offset + 4) = va_arg(args, int);
			*(ubit32 *)(dest + offset + 8) = va_arg(args, int);
			*(ubit32 *)(dest + offset + 12) = va_arg(args, int);
#elif __WORDSIZE == 64
			*(ubit64 *)(dest + offset) = va_arg(args, int);
			*(ubit64 *)(dest + offset + 8) = va_arg(args, int);
#elif __WORDSIZE >= 128
			*(ubit128 *)(dest + offset) = va_arg(args, int);
#else
#error "Unable to generate argument 16Byte marshalling for your arch wordsize."
#endif
			break;

		default:
			return ERROR_UNSUPPORTED;
		};
	};

	return ERROR_SUCCESS;
}

/*
 * look for the first occurance a layout_t and return the offset.
 * in the special case of UDI_DL_END, intermmediate nested
 * structures are ignored. ie:
 * UDI_UBIT8_T UDI_DL_ARRAY UDI_UBIT8_T 10 UDI_DL_END UDI_DL_END
 * _udi_get_layout_offset(layout, UDI_DL_END) would return
 * on the second UDI_DL_END
 */
udi_boolean_t fplainn::sChannelMsg::_udi_get_layout_offset(
	udi_layout_t *start, udi_layout_t **end, udi_size_t *offset,
	udi_layout_t key
	)
{

	udi_layout_t			*layout=start;
	udi_size_t			layout_size=0, element_size,
					array_size=0;

	if (layout == NULL) { return FALSE; };

	while (*layout != key)
	{
		switch (*layout)
		{
		case UDI_DL_UBIT8_T:
			element_size = sizeof(udi_ubit8_t);
			break;
		case UDI_DL_SBIT8_T:
			element_size = sizeof(udi_sbit8_t);
			break;
		case UDI_DL_UBIT16_T:
			element_size = sizeof(udi_ubit16_t);
			break;
		case UDI_DL_SBIT16_T:
			element_size = sizeof(udi_sbit16_t);
			break;
		case UDI_DL_UBIT32_T:
			element_size = sizeof(udi_ubit32_t);
			break;
		case UDI_DL_SBIT32_T:
			element_size = sizeof(udi_sbit32_t);
			break;
		case UDI_DL_BOOLEAN_T:
			element_size = sizeof(udi_boolean_t);
			break;
		case UDI_DL_STATUS_T:
			element_size = sizeof(udi_status_t);
			break;

		case UDI_DL_INDEX_T:
			element_size = sizeof(udi_index_t);
			break;

		case UDI_DL_CHANNEL_T:
			element_size = sizeof(udi_channel_t);
			break;

		case UDI_DL_ORIGIN_T:
			element_size = sizeof(udi_origin_t);
			break;

#if _UDI_PHYSIO_SUPPORTED
		case UDI_DL_DMA_CONSTRAINTS_T:
			element_size = sizeof(udi_dma_constraints_t);
			break;
#endif /* _UDI_PHYSIO_SUPPORTED */

		case UDI_DL_BUF:
			element_size = sizeof(udi_buf_t *);
			layout += 3;	/* skip "uninteresting" args */
			break;

		case UDI_DL_CB:
			element_size = sizeof(udi_cb_t *);
			break;

		case UDI_DL_INLINE_UNTYPED:
		case UDI_DL_INLINE_DRIVER_TYPED:
			element_size = sizeof(void *);
			break;

		case UDI_DL_INLINE_TYPED:
			/*
			 * The size in the surrounding object is
			 * just a pointer, but we have to skip over
			 * the nested layout.
			 * TODO: Something will probably care about
			 * the content of the nested layout.
			 */
			element_size = sizeof(void *);
			while (*++layout != UDI_DL_END);
			break;

		case UDI_DL_MOVABLE_UNTYPED:
			element_size = sizeof(void *);
			break;

		case UDI_DL_MOVABLE_TYPED:
			/*
			 * The size in the surrounding object is
			 * just a pointer, but we have to skip over
			 * the nested layout.
			 * TODO: Something will probably care about
			 * the content of the nested layout.
			 */
			element_size = sizeof(void *);
			while (*++layout != UDI_DL_END);
			break;

		case UDI_DL_ARRAY:
			/*
			 * Recurse to parse these
			 */
			array_size = *++layout;
//			element_size = array_size
//				* _udi_get_layout_size(++layout, NULL, NULL);

			while (*++layout != UDI_DL_END);
			break;

#if defined (UDI_DL_PIO_HANDLE_T)
		case UDI_DL_PIO_HANDLE_T:
			element_size = sizeof(udi_pio_handle_t);
			break;
#endif

		case UDI_DL_END:
			return FALSE;
		default:
			assert_fatal(0);
		}
		layout++;

		/*
		 * Properly align this element
		 */
		layout_size = _ported_udi_alignto(layout_size, element_size);

		/*
		 * Add the element to the total size
		 */
		layout_size += element_size;
	}
	*end = layout;
	*offset = layout_size;
	return TRUE;
}
