#ifndef _POINTER_DOUBLY_LINKED_LIST_H
	#define _POINTER_DOUBLY_LINKED_LIST_H

	#include <__kstdlib/__ktypes.h>
	#include <__kstdlib/__kcxxlib/new>
	#include <kernel/common/sharedResourceGroup.h>
	#include <kernel/common/waitLock.h>

#define PTRDBLLIST			"Dbl-Link PtrList: "

#define PTRDBLLIST_ADD_HEAD		0x0
#define PTRDBLLIST_ADD_TAIL		0x1

template <class T>
class pointerDoubleListC
{
public:
	pointerDoubleListC(void)
	{
		list.rsrc.head = list.rsrc.tail = __KNULL;
		list.rsrc.nItems = 0;
	}

	~pointerDoubleListC(void);

	error_t initialize(void) { return ERROR_SUCCESS; };

public:
	void dump(void);

	error_t addItem(T *item, ubit8 mode=PTRDBLLIST_ADD_TAIL);
	void removeItem(T *item);
	T *popFromHead(void);
	T *popFromTail(void);
	T *getHead(void);
	T *getTail(void);

	uarch_t	getNItems(void);

private:
	struct listNodeS
	{
		T		*item;
		listNodeS	*prev, *next;
	};

	struct listStateS
	{
		listNodeS	*head, *tail;
		uarch_t		nItems;
	};

	sharedResourceGroupC<waitLockC, listStateS>	list;
};


/**	Template definition.
 ******************************************************************************/

template <class T>
uarch_t pointerDoubleListC<T>::getNItems(void)
{
	uarch_t		ret;

	list.lock.acquire();
	ret = list.rsrc.nItems;
	list.lock.release();

	return ret;
}

template <class T>
void pointerDoubleListC<T>::dump(void)
{
	listNodeS	*curr, *head, *tail;
	sbit8		flipFlop=0;

	list.lock.acquire();
	head = list.rsrc.head;
	tail = list.rsrc.tail;
	list.lock.release();

	__kprintf(NOTICE PTRDBLLIST"@ head 0x%p, tail 0x%p, %d items, "
		"lock obj 0x%p: Dumping.\n",
		head, tail, getNItems(), &list.lock);

	list.lock.acquire();

	curr = head;
	for (; curr != __KNULL; curr = curr->next, flipFlop++)
	{
		if (flipFlop < 4) { __kprintf(CC"\t0x%p ", curr->item); }
		else
		{
			__kprintf(CC"0x%p\n", curr->item);
			flipFlop = -1;
		};
	};

	if (flipFlop != 0) { __kprintf(CC"\n"); };
	list.lock.release();
}

template <class T>
error_t pointerDoubleListC<T>::addItem(T *item, ubit8 mode)
{
	listNodeS	*newNode;

	newNode = new listNodeS;
	if (newNode == __KNULL) { return ERROR_MEMORY_NOMEM; };

	newNode->item = item;

	switch (mode)
	{
	case PTRDBLLIST_ADD_HEAD:
		newNode->prev = __KNULL;
		
		list.lock.acquire();

		newNode->next = list.rsrc.head;
		if (list.rsrc.head != __KNULL) {
			list.rsrc.head->prev = newNode;
		}
		else { list.rsrc.tail = newNode; };
		list.rsrc.head = newNode;
		list.rsrc.nItems++;

		list.lock.release();
		break;

	case PTRDBLLIST_ADD_TAIL:
		newNode->next = __KNULL;

		list.lock.acquire();

		newNode->prev = list.rsrc.tail;
		if (list.rsrc.tail != __KNULL) {
			list.rsrc.tail->next = newNode;
		}
		else { list.rsrc.head = newNode; };
		list.rsrc.tail =  newNode;
		list.rsrc.nItems++;

		list.lock.release();
		break;

	default:
		__kprintf(ERROR PTRDBLLIST"addItem(0x%p): Invalid add mode.\n",
			item);

		return ERROR_INVALID_ARG;
	};

	return ERROR_SUCCESS;
}

template <class T>
void pointerDoubleListC<T>::removeItem(T *item)
{
	listNodeS	*curr;

	list.lock.acquire();
	// Just run through until we find it, then remove it.
	for (curr = list.rsrc.head; curr != __KNULL; curr = curr->next)
	{
		if (curr->item == item)
		{
			if (list.rsrc.head == curr)
			{
				list.rsrc.head = curr->next;
				if (list.rsrc.head != __KNULL) {
					curr->next->prev = __KNULL;
				};
			}
			else { curr->prev->next = curr->next; };

			if (list.rsrc.tail == curr)
			{
				list.rsrc.tail = curr->prev;
				if (list.rsrc.tail != __KNULL) {
					curr->prev->next = __KNULL;
				};
			}
			else { curr->next->prev = curr->prev; };

			list.rsrc.nItems--;
			list.lock.release();

			delete curr;
			return;
		};
	};

	list.lock.release();
}

template <class T>
T *pointerDoubleListC<T>::popFromHead(void)
{
	T		*ret=__KNULL;
	listNodeS	*tmp;

	list.lock.acquire();

	// If removing last item:
	if (list.rsrc.tail == list.rsrc.head) { list.rsrc.tail = __KNULL; };

	if (list.rsrc.head != __KNULL)
	{
		tmp = list.rsrc.head;
		list.rsrc.head->prev = __KNULL;
		list.rsrc.head = list.rsrc.head->next;
		list.rsrc.nItems--;

		list.lock.release();

		ret = tmp->item;
		delete tmp;
		return ret;
	}
	else
	{
		list.lock.release();
		return __KNULL;
	};
}

template <class T>
T *pointerDoubleListC<T>::popFromTail(void)
{
	T		*ret=__KNULL;
	listNodeS	*tmp;

	list.lock.acquire();

	// If removing last item:
	if (list.rsrc.head == list.rsrc.tail) { list.rsrc.head = __KNULL; };

	if (list.rsrc.tail != __KNULL)
	{
		tmp = list.rsrc.tail;
		list.rsrc.tail->next = __KNULL;
		list.rsrc.tail = list.rsrc.tail->prev;
		list.rsrc.nItems--;

		list.lock.release();

		ret = tmp->item;
		delete tmp;
		return ret;
	}
	else
	{
		list.lock.release();
		return __KNULL;
	};
}

template <class T>
T *pointerDoubleListC<T>::getHead(void)
{
	T	*ret=__KNULL;

	list.lock.acquire();

	if (list.rsrc.head != __KNULL) {
		ret = list.rsrc.head->item;
	};

	list.lock.release();

	return ret;
}

template <class T>
T *pointerDoubleListC<T>::getTail(void)
{
	T	*ret=__KNULL;

	list.lock.acquire();

	if (list.rsrc.tail != __KNULL) {
		ret = list.rsrc.tail->item;
	};

	list.lock.release();

	return ret;
}

#endif
