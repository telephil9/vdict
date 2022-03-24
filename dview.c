#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <thread.h>
#include <bio.h>
#include "a.h"

typedef struct Box Box;
typedef struct Link Link;

struct Box
{
	Rectangle r;
	Rectangle sr;
	Image *b;
};

struct Link
{
	Box *b;
	Rectangle r;
	char text[255];
};

enum
{
	Padding = 4,
	Scrollwidth = 12,
};

Channel *chan;
Dvec *defs;
Box **boxes;
usize nboxes;
Link links[1024];
usize nlinks;
Cols *cols;
Rectangle sr;
Rectangle scrollr;
Rectangle boxr;
int totalh;
int viewh;
int offset;
int scrollsize;

Box*
renderbox(Definition *d)
{
	Box *b;
	int i, l, n, w, mw, inlink, cl;
	Point p, lp;
	Image *c;
	char buf[1024] = {0};
	Rune r;

	n = 0;
	w = 0;
	mw = 0;
	l = strlen(d->text);
	for(i = 0; i < l; i++){
		if(d->text[i] == '\n'){
			++n;
			if(w > mw)
				mw = w;
			w = 0;
		}else{
			w += stringnwidth(font, d->text+i, 1);
		}
	}
	snprint(buf, sizeof buf, "From %s", d->db);
	w = stringwidth(font, buf);
	if(w > mw)
		mw = w;
	b = emalloc(sizeof *b);
	b->r = Rect(0, 0, Padding + mw + Padding, Padding+(n+2)*font->height+2*Padding);
	b->b = allocimage(display, b->r, screen->chan, 0, DNofill);
	draw(b->b, b->r, cols->back, nil, ZP);
	p = Pt(Padding, Padding);
	inlink = 0;
	cl = 0;
	for(i = 0; i < l; ){
		switch(d->text[i]){
		case '\n':
			p.x = Padding;
			p.y += font->height;
			++i;
			break;
		case '{':
			cl = 0;
			lp = p;
			inlink = 1;
			++i;
			break;
		case '}':
			links[nlinks].b = b;
			links[nlinks].r = Rpt(lp, addpt(p, Pt(0, font->height)));
			links[nlinks].text[cl] = '\0';
			nlinks += 1;
			cl = 0;
			inlink = 0;
			++i;
			break;
		default:
			c = cols->text;
			if(inlink){
				c = cols->focus;
				links[nlinks].text[cl++] = d->text[i];
			}
			i += chartorune(&r, d->text + i);
			p = runestringn(b->b, p, c, ZP, font, &r, 1);
			break;
		}
	}
	p.x = Padding;
	p.y += Padding;
	string(b->b, p, cols->scrl, ZP, font, buf);
	return b;
}

void
layout(void)
{
	Box *b;
	int i;
	Point p;

	totalh = 0;
	p = addpt(boxr.min, Pt(Padding, Padding));
	for(i = 0; i < nboxes; i++){
		b = boxes[i];
		b->sr = rectaddpt(b->r, p);
		p.y += Dy(b->r) + Padding;
		totalh += Dy(b->r) + Padding;
	}
	scrollsize = 10*totalh/100.0;
	for(i = 0; i < nlinks; i++)
		links[i].r = rectaddpt(links[i].r, links[i].b->sr.min);
}

void
dviewset(Dvec *d)
{
	Definition *def;
	int i;

	if(defs != nil){
		for(i = 0; i < nboxes; i++){
			freeimage(boxes[i]->b);
			free(boxes[i]);
		}
		nboxes = 0;
		for(i = 0; i < dvlen(defs); i++){
			def = dvref(defs, i);
			free(def->text);
			free(def);
		}
		free(defs);
	}
	offset = 0;
	nlinks = 0;
	defs = d;
	nboxes = dvlen(defs);
	boxes = emalloc(nboxes * sizeof(Box*));
	for(i = 0; i < nboxes; i++){
		def = dvref(defs, i);
		boxes[i] = renderbox(def);
	}
	layout();
}

void
dviewredraw(void)
{
	Box *b;
	Rectangle clipr, scrposr;
	int i, h, y, ye, vmin, vmax;

	clipr = screen->clipr;
	draw(screen, sr, cols->back, nil, ZP);
	draw(screen, scrollr, cols->scrl, nil, ZP);
	border(screen, scrollr, 1, cols->text, ZP);
	if(viewh < totalh){
		h = ((double)viewh/totalh) * Dy(scrollr);
		y = ((double)offset/totalh) * Dy(scrollr);
		ye = scrollr.min.y + y + h - 1;
		if(ye >= scrollr.max.y)
			ye = scrollr.max.y - 1;
		scrposr = Rect(scrollr.min.x + 1, scrollr.min.y + y + 1, scrollr.max.x - 1, ye);
	}else
		scrposr = insetrect(scrollr, -1);
	draw(screen, scrposr, cols->back, nil, ZP);
	if(boxes != nil && nboxes == 0){
		string(screen, addpt(sr.min, Pt(Padding, Padding)), cols->text, ZP, font, "No result found.");
		return;
	}
	replclipr(screen, 0, boxr);
	vmin = boxr.min.y + offset;
	vmax = boxr.max.y + offset;
	if(boxes != nil){
		for(i = 0; i < nboxes; i++){
			b = boxes[i];
			if(b->sr.min.y <= vmax && b->sr.max.y >= vmin)
				draw(screen, rectaddpt(b->sr, Pt(0, -offset)), b->b, nil, ZP);
		}
	}
	replclipr(screen, 0, clipr);
}

void
dviewresize(Rectangle r)
{
	sr = r;
	scrollr = sr;
	scrollr.min.x += Padding;
	scrollr.max.x = scrollr.min.x + Padding + Scrollwidth;
	scrollr.max.y -= Padding;
	boxr = sr;
	boxr.min.x = scrollr.max.x + Padding;
	boxr.max.x -= Padding;
	boxr.max.y -= Padding;
	viewh = Dy(boxr);
	if(boxes != nil)
		layout();
}

void
scroll(int d)
{
	if(d < 0 && offset <= 0)
			return;
	if(d > 0 && offset + viewh > totalh)
			return;
	offset += d;
	if(offset < 0)
		offset = 0;
	if((offset + viewh ) > totalh)
		offset = totalh - viewh;
	dviewredraw();
	flushimage(display, 1);
}

void
clicklink(Point p)
{
	int i;

	p = addpt(p, Pt(0, offset));
	for(i = 0; i < nlinks; i++){
		if(ptinrect(p, links[i].r)){
			nbsendp(chan, strdup(links[i].text));
			return;
		}
	}
}

void
dviewmouse(Mouse m)
{
	if(!ptinrect(m.xy, sr))
		return;
	if(m.buttons == 1)
		clicklink(m.xy);
	else if(m.buttons == 8)
		scroll(-scrollsize);
	else if(m.buttons == 16)
		scroll(scrollsize);
}

void
dviewkey(Rune k)
{
	switch(k){
	case Kup:
		scroll(-scrollsize);
		break;
	case Kdown:
		scroll(scrollsize);
		break;
	case Kpgup:
		scroll(-viewh);
		break;
	case Kpgdown:
		scroll(viewh);
		break;
	case Khome:
		scroll(-totalh);
		break;
	case Kend:
		scroll(totalh);
		break;
	}
}


void
dviewinit(Channel *ch, Cols *c)
{
	chan = ch;
	defs = nil;
	boxes = nil;
	nboxes = 0;
	nlinks = 0;
	totalh = -1;
	offset = 0;
	cols = c;
}
