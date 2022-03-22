#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <thread.h>
#include "a.h"

void*
emalloc(ulong size)
{
	void *p;

	p = malloc(size);
	if(p == nil)
		sysfatal("malloc: %r");
	return p;
}

void*
erealloc(void *p, ulong size)
{
	void *q;

	q = realloc(p, size);
	if(q == nil)
		sysfatal("realloc: %r");
	return q;
}

