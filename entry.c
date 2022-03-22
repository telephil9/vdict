#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <thread.h>
#include <ctype.h>
#include <bio.h>
#include "a.h"

enum
{
	Senabled = 1 << 0,
	Sfocused = 1 << 1,
};

enum
{
	Padding = 4,
};

static void einsert(Entry *entry, char *s);
static void edelete(Entry *entry, int bs);

static char *menu2str[] =
{
	"cut",
	"paste",
	"snarf",
	0 
};

enum
{
	Mcut,
	Mpaste,
	Msnarf,
};
static Menu menu2 = { menu2str };
static Image *tick = nil;

int
min(int x, int y)
{
	return x <= y ? x : y;
}

int
max(int x, int y)
{
	return x >= y ? x : y;
}

static Image*
createtick(Image *bg, Image *fg)
{
	enum { Tickw = 3 };
	Image *t;

	t = allocimage(display, Rect(0, 0, Tickw, font->height), screen->chan, 0, DWhite);
	if(t == nil)
		return 0;
	/* background color */
	draw(t, t->r, bg, nil, ZP);
	/* vertical line */
	draw(t, Rect(Tickw/2, 0, Tickw/2+1, font->height), fg, nil, ZP);
	/* box on each end */
	draw(t, Rect(0, 0, Tickw, Tickw), fg, nil, ZP);
	draw(t, Rect(0, font->height-Tickw, Tickw, font->height), fg, nil, ZP);
	return t;
}

void
entryinit(Entry *e, Cols *cols)
{
	if(tick == nil)
		tick = createtick(cols->back, cols->text);
	e->state = Senabled;
	e->buttons = 0;
	e->tickx = 0;
	e->size = 255;
	e->text = emalloc(e->size * sizeof(char));
	e->text[0] = 0;
	e->p0 = 0;
	e->p1 = 0;
	e->len = 0;
	e->c = chancreate(sizeof(char*), 1);
	e->cols = cols;
}

int
entryhasfocus(Entry *e)
{
	return (e->state & Sfocused);
}

static void
entryunfocus(Entry *e)
{
	if(!entryhasfocus(e))
		return;
	e->state ^= Sfocused;
	e->buttons = 0;
	e->p0 = e->len;
	e->p1 = e->len;
	entryredraw(e);
}

void
entryfocus(Entry *e, int sel)
{
	if(entryhasfocus(e))
		return;
	e->state |= Sfocused;
	if(sel){
		e->p0 = 0;
		e->p1 = e->len;
	}
	entryredraw(e);
}

void
entrysettext(Entry *e, const char *text)
{
	int l;
	
	l = strlen(text);
	if(l >= e->size) {
		e->size = l;
		e->text = erealloc(e->text, e->size * sizeof(char));
	}
	strncpy(e->text, text, l);
	e->text[l] = 0;
	e->len = l;
	e->p0 = e->len;
	e->p1 = e->p0;
	e->tickx = stringnwidth(font, e->text, e->len);
}

void 
entryresize(Entry *e, Rectangle r)
{
	e->r = r;
}

void
entryredraw(Entry *e)
{
	Rectangle r, clipr;
	Point p;
	int y, sels, sele;

	clipr = screen->clipr;
	replclipr(screen, 0, e->r);
	draw(screen, e->r, e->cols->back, nil, ZP);
	if(entryhasfocus(e))
		border(screen, e->r, 1, e->cols->focus, ZP);
	else
		border(screen, e->r, 1, e->cols->text, ZP);
	y = (Dy(e->r) - font->height) / 2;
	p = Pt(e->r.min.x + Padding, e->r.min.y + y);
	stringn(screen, p, e->cols->text, ZP, font, e->text, e->len);
	if (e->p0 != e->p1) {
		sels = min(e->p0, e->p1);
		sele = max(e->p0, e->p1);
		p.x += stringnwidth(font, e->text, sels);
		stringnbg(screen, p, e->cols->text, ZP, font, e->text+sels, sele-sels, e->cols->sel, ZP);
	} else if (e->state & Sfocused) {
		e->tickx = stringnwidth(font, e->text, e->p0);
		p.x += e->tickx;
		r = Rect(p.x, p.y, p.x + Dx(tick->r), p.y + Dy(tick->r));
		draw(screen, r, tick, nil, ZP);
	}	
	flushimage(display, 1);
	replclipr(screen, 0, clipr);
}

static int
ptpos(Entry *e, Mouse m)
{
	int i, x, prev, cur;

	x = m.xy.x - e->r.min.x - Padding;
	prev = 0;
	for(i = 0; i < e->len; i++){
		cur = stringnwidth(font, e->text, i);
		if ((prev+cur)/2 >= x){
			i--;
			break;
		}else if (prev <= x && cur >= x)
			break;
		prev = cur;
	}
	return i;
}

static int
issep(char c)
{
	return c == 0 || c == '/' || (!isalnum(c) && c != '-');
}

static void
entryclicksel(Entry *e)
{
	int s, t;

	if(e->p0 == 0)
		e->p1 = e->len;
	else if(e->p0 == e->len)
		e->p0 = 0;
	else{
		s = e->p0;
		t = e->p0;
		while((s - 1) >= 0 && !issep(e->text[s - 1]))
			--s;
		while(t < e->len && !issep(e->text[t]))
			++t;
		e->p0 = s;
		e->p1 = t;
	}
}

int
entrymouse(Entry *e, Mouse m)
{
	static int lastn = -1;
	static ulong lastms = 0;
	int in, n, sels, sele;
	char *s;
	usize len;

	s = nil;
	len = 0;
	in = ptinrect(m.xy, e->r);
	if(in && !e->buttons && m.buttons)
		e->state |= Sfocused;
	if(e->state & Sfocused){
		n = ptpos(e, m);
		if(!in && !e->buttons && m.buttons){
			entryunfocus(e);
			return -1;
		}
		if(m.buttons & 1){ /* holding left button */
			sels = min(e->p0, e->p1);
			sele = max(e->p0, e->p1);
			if(m.buttons == (1|2) && e->buttons == 1){
				if(sels != sele){
					/* TODO: snarf */
					edelete(e, 0);
				}
			}else if(m.buttons == (1|4) && e->buttons == 1){
				/* TODO: paste */
				//plan9_paste(&s, &len);
				if(len > 0 && s != nil)
					einsert(e, s);
				free(s);
			}else if(m.buttons == 1 && e->buttons <= 1){
				e->p0 = n;
				if (e->buttons == 0){
					e->p1 = n;
					if(n == lastn && lastms > 0 && (m.msec - lastms)<=250)
						entryclicksel(e);
				}
			}
			entryredraw(e);
			lastn = n;
			lastms = m.msec;
		} else if (m.buttons & 2) {
			//sels = min(e->p0, e->p1);
			//sele = max(e->p0, e->p1);
			/* TODO
			//n = emenuhit(2, &e.mouse, &menu2);
			n = -1;
			switch(n) {
			case Mcut:
				if (sels != sele) {
					plan9_snarf(entry->text+sels, sele-sels);
					text_delete(entry, 0);
				}
				break;
			case Mpaste:
				plan9_paste(&s, &len);
				if (len >= 0 && s != NULL)
					text_insert(entry, s);
				free(s);
				break;
			case Msnarf:
				if (sels != sele) {
					plan9_snarf(entry->text+sels, sele-sels);
				}
				break;
			}
			entryredraw(e);
			*/
		}
		e->buttons = m.buttons;
		return 0;
	}
	return -1;
}

void
entrykey(Entry *e, Rune k)
{
	int sels, sele, n;
	char s[UTFmax+1];

	if(!entryhasfocus(e))
		return;
	sels = min(e->p0, e->p1);
	sele = max(e->p0, e->p1);
	switch (k) {
	case Keof:
	case '\n':
		e->p0 = e->p1 = e->len;
		nbsendp(e->c, strdup(e->text));
		break;
	case Knack:	/* ^U: delete selection, if any, and everything before that */
		memmove(e->text, e->text + sele, e->len - sele);
		e->len = e->len - sele;
		e->p0 = 0;
		e->text[e->len] = 0;
		break;
	case Kleft:
		e->p0 = max(0, sels-1);
		break;
	case Kright:
		e->p0 = min(e->len, sele+1);
		break;
	case Ksoh:	/* ^A: start of line */
	case Khome:
		e->p0 = 0;
		break;
	case Kenq:	/* ^E: end of line */
	case Kend:
		e->p0 = e->len;
		break;
	case Kdel:
		edelete(e, 0);
		break;
	case Kbs:
		edelete(e, 1);
		break;
	case Ketb:
		while(sels > 0 && !isalnum(e->text[sels-1]))
			sels--;
		while(sels > 0 && isalnum(e->text[sels-1]))
			sels--;
		e->p0 = sels;
		e->p1 = sele;
		edelete(e, 0);
		break;
	case Kesc:
		if (sels == sele) {
			sels = e->p0 = 0;
			sele = e->p1 = e->len;
		}
		/* TODO */
		//plan9_snarf(e->text+sels, sele-sels);
		edelete(e, 0);
		break;
	case 0x7: /* ^G: remove focus */
		entryunfocus(e);
		return;
	default:
		if(k < 0x20 || (k & 0xFF00) == KF || (k & 0xFF00) == Spec || (n = runetochar(s, &k)) < 1)
			return;
		s[n] = 0;
		einsert(e, s);
	}
	e->p1 = e->p0;
	entryredraw(e);
}

static void
einsert(Entry *e, char *s)
{
	int sels, sele, n;
	char *p;

	n = strlen(s);
	if(e->size <= e->len + n){
		e->size = (e->len + n)*2 + 1;
		if((p = realloc(e->text, e->size)) == nil)
			return;
		e->text = p;
	}
	sels = min(e->p0, e->p1);
	sele = max(e->p0, e->p1);
	if(sels != sele){
		memmove(e->text + sels + n, e->text + sele, e->len - sele);
		e->len -= sele - sels;
		e->p0 = sels;
	}else if (e->p0 != e->len)
		memmove(e->text + e->p0 + n, e->text + e->p0, e->len - e->p0);
	memmove(e->text + sels, s, n);
	e->len += n;
	e->p1 = sels;
	e->p0 = sels + n;
	e->text[e->len] = 0;		
}

static void
edelete(Entry *e, int bs)
{
	int sels, sele;

	sels = min(e->p0, e->p1);
	sele = max(e->p0, e->p1);
	if(sels == sele && sels == 0)
		return;
	memmove(e->text + sels - bs, e->text + sele, e->len - sele);
	e->p0 = sels - bs;
	e->len -= sele - sels + bs;
	e->p1 = e->p0;
	e->text[e->len] = 0;
}

