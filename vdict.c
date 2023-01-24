#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "a.h"

Dictc *dict;
Mousectl *mc;
Keyboardctl *kc;
Entry *entry;
Cols *cols;
Rectangle searchr;
Rectangle entryr;
Rectangle viewr;
char *db;

enum
{
	Padding = 4,
};

void
redraw(void)
{
	draw(screen, screen->r, cols->back, nil, ZP);
	string(screen, addpt(searchr.min, Pt(Padding, 2*Padding+2)), cols->text, ZP, font, "Search:");
	entryredraw(entry);
	dviewredraw();
	flushimage(display, 1);
}

char*
dictmenu(int index)
{
	Element *e;

	if(index < 0 || index >= dvlen(dict->db) + 2)
		return nil;
	if(index == 0)
		return "First matching result";
	else if(index == 1)
		return "All matching results";
	e = dvref(dict->db, index - 2);
	return e->desc;
}

void
emouse(Mouse m)
{
	Menu menu;
	Element *e;
	int n;

	if(ptinrect(m.xy, viewr) && m.buttons == 2){
		menu.gen = dictmenu;
		n = menuhit(2, mc, &menu, nil);
		if(n < 0)
			return;
		switch(n){
		case 0:
			db = Dfirstmatch;
			break;
		case 1:
			db = Dallmatches;
			break;
		default:
			e = dvref(dict->db, n - 2);
			db = e->name;
			break;
		}
		return;
	}
	entrymouse(entry, m);
	dviewmouse(m);
}

void
ekeyboard(Rune k)
{
	switch(k){
	case Kdel:
		threadexitsall(nil);
		break;
	case Kstx:
		if(!entryhasfocus(entry))
			entryfocus(entry, 1);
		break;
	default:
		entrykey(entry, k);
		if(!entryhasfocus(entry))
			dviewkey(k);
		break;
	}
}

void
eresize(void)
{
	searchr = screen->r;
	searchr.max.y = searchr.min.y + 2*Padding + 2 + font->height + 2 + Padding;
	entryr = searchr;
	entryr.min.x += Padding + stringwidth(font, "Search:") + Padding;
	entryr.max.x = searchr.max.x - Padding;
	entryr.min.y += 2*Padding;
	entryr.max.y -= Padding;
	entryresize(entry, entryr);
	viewr = screen->r;
	viewr.min.y = searchr.max.y + 1;
	dviewresize(viewr);
	redraw();
}

void
esearch(char *s)
{
	Dvec *v;

	v = dictdefine(dict, db, s);
	if(v == nil)
		sysfatal("dictdefine: %r");
	dviewset(v);
	dviewredraw();
	flushimage(display, 1);
}

void
elink(char *s)
{
	entrysettext(entry, s);
	entryredraw(entry);
	esearch(s);
}

void
initcolors(int reverse)
{
	cols = emalloc(sizeof *cols);
	if(reverse){
		cols->back  = display->black;
		cols->text  = display->white;
		cols->focus = allocimage(display, Rect(0, 0, 1, 1), RGBA32, 1, DPurpleblue);
		cols->sel   = allocimage(display, Rect(0, 0, 1, 1), RGBA32, 1, DPurpleblue);
		cols->scrl  = allocimage(display, Rect(0, 0, 1, 1), RGBA32, 1, 0x999999FF^reverse);
	}else{
		cols->back  = display->white;
		cols->text  = display->black;
		cols->focus = allocimage(display, Rect(0, 0, 1, 1), RGBA32, 1, DGreygreen);
		cols->sel   = allocimage(display, Rect(0, 0, 1, 1), RGBA32, 1, 0xCCCCCCFF);
		cols->scrl  = allocimage(display, Rect(0, 0, 1, 1), RGBA32, 1, 0x999999FF);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-b] [-h <host>] [-p <port>]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	enum { Emouse, Eresize, Ekeyboard, Eentry, Elink };
	Mouse m;
	Rune k;
	char *s, *l, *host;
	int port, reverse;
	Channel *lchan;
	Alt a[] = {
		{ nil, &m,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, &k,  CHANRCV },
		{ nil, &s,  CHANRCV },
		{ nil, &l,  CHANRCV },
		{ nil, nil, CHANEND },
	};

	host = "dict.org";
	port = 2628;
	reverse = 0;
	ARGBEGIN{
	case 'b':
		reverse = ~0xFF;
		break;
	case 'h':
		host = EARGF(usage());
		break;
	case 'p':
		port = atoi(EARGF(usage()));
		break;
	}ARGEND;
	if(host == nil)
		usage();
	db = Dfirstmatch;
	dict = dictdial(host, port);
	if(dict == nil)
		sysfatal("initdict: %r");
	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");
	display->locking = 0;
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	initcolors(reverse);
	entry = emalloc(sizeof *entry);
	entryinit(entry, cols);
	lchan = chancreate(sizeof(char*), 1);
	dviewinit(lchan, cols);
	a[Emouse].c = mc->c;
	a[Eresize].c = mc->resizec;
	a[Ekeyboard].c = kc->c;
	a[Eentry].c = entry->c;
	a[Elink].c = lchan;
	eresize();
	entryfocus(entry, 0);
	for(;;){
		switch(alt(a)){
		case Emouse:
			emouse(m);
			break;
		case Eresize:
			if(getwindow(display, Refnone) < 0)
				sysfatal("getwindow: %r");
			eresize();
			break;
		case Ekeyboard:
			ekeyboard(k);
			break;
		case Eentry:
			esearch(s);
			break;
		case Elink:
			elink(l);
			break;
		}
	}
}

