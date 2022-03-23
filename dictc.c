#include <u.h>
#include <libc.h>
#include <bio.h>
#include <String.h>
#include <draw.h>
#include <thread.h>
#include "a.h"

typedef struct Response Response;

struct Response
{
	int code;
	char *msg;
};

struct Dvec
{
	void **elts;
	usize  len;
	usize  sz;
};

enum
{
	Eok,
	Eeof,
	Eunexpected,
	Ebadformat,
	Enoshow,
};

const char* Errors[] = {
	[Eeof]        = "no response from server",
	[Eunexpected] = "unexpected response from server",
	[Ebadformat]  = "bad response format",
	[Enoshow]     = "show command did not return any result",
};

Dvec*
mkdvec(usize size)
{
	Dvec *v;

	v = emalloc(sizeof *v);
	v->len  = 0;
	v->sz   = size;
	v->elts = emalloc(size * sizeof(void*));
	return v;
}

void
dvadd(Dvec *v, void *p)
{
	if(v->len == v->sz){
		v->sz  *= 1.5;
		v->elts = erealloc(v->elts, v->sz * sizeof(void*));
	}
	v->elts[v->len++] = p;
}

usize
dvlen(Dvec *v)
{
	return v->len;
}

void*
dvref(Dvec *v, usize i)
{
	return v->elts[i];
}

int
readstatus(char *s)
{
	char *e;
	long l;

	l = strtol(s, &e, 10);
	if(l == 0 || e == s)
		return -1;
	return l;
}

int
expectline(Dictc *c, char *l)
{
	char *s;

	s = Brdstr(c->bin, '\n', 1);
	if(s == nil)
		return Eeof;
	if(strncmp(s, l, strlen(l)) == 0)
		return Eok;
	return Ebadformat;
}

int
sendcmd(Dictc *c, const char *cmd, Response *r)
{
	char *s;

	write(c->fd, cmd, strlen(cmd));
	write(c->fd, "\n", 1);
	s = Brdstr(c->bin, '\n', 1);
	if(s == nil)
		return Eeof;
	if(r != nil){
		r->code = readstatus(s);
		if(r->code == -1){
			free(s);
			return Eunexpected;
		}
		r->msg = strdup(s+4);
	}
	free(s);
	return Eok;
}

Dvec*
show(Dictc *c, char *cmd, int ok, int ko, int *err)
{
	Response r;
	Element *e;
	Dvec *v;
	char *s, *p;
	int rc, n, i;

	rc = sendcmd(c, cmd, &r);
	if(rc != Eok){
		*err = rc;
		return nil;
	}
	if(r.code == ko){
		*err = Enoshow;
		return nil;
	}else if(r.code != ok){
		*err = Eunexpected;
		return nil;
	}
	n = readstatus(r.msg);
	free(r.msg);
	v = mkdvec(n);
	for(i = 0; i < n; i++){
		s = Brdstr(c->bin, '\n', 1);
		if(s == nil){
			*err = Eeof;
			free(v); /* FIXME: free elts */
			return nil;
		}
		p = strchr(s, ' ');
		if(p == nil){
			*err = Ebadformat;
			free(v); /* FIXME: free elts */
			return nil;
		}
		e = emalloc(sizeof(Element));
		p += 2; /* skip <space>" */
		p[strlen(p) - 2] = 0; /* remove "\r */
		e->desc = strdup(p);
		p -= 2;
		*p = '\0';
		e->name = strdup(s);
		dvadd(v, e);
		free(s);
	}
	if((n = expectline(c, ".")) != Eok){
		*err = n;
		free(v); /* FIXME: free elts */
		return nil; 
	}
	if((n = expectline(c, "250 ok")) != Eok){
		*err = n;
		free(v); /* FIXME: free elts */
		return nil; 
	}
	*err = Eok;
	return v;
}

void
freedictc(Dictc *c)
{
	Element *e;
	int i;

	Bterm(c->bin);
	close(c->fd);
	if(c->db != nil){
		for(i = 0; i < dvlen(c->db); i++){
			e = dvref(c->db, i);
			free(e->name);
			free(e->desc);
			free(e);
		}
		free(c->db);
	}
	free(c);
}

Dictc*
dictdial(const char *addr, int port)
{
	Dictc *c;
	char *s, buf[255];
	int n;

	if(port == 0)
		port = 2628;
	snprint(buf, sizeof buf, "tcp!%s!%d", addr, port);
	c = malloc(sizeof *c);
	if(c == nil)
		sysfatal("malloc: %r");
	c->fd = dial(buf, nil, nil, nil);
	if(c->fd <= 0)
		sysfatal("dial: %r");
	c->bin = Bfdopen(c->fd, OREAD);
	if(c->bin == nil)
		sysfatal("Bfdopen: %r");
	s = Brdstr(c->bin, '\n', 1);
	if(s == nil){
		werrstr("no status sent by server");
		freedictc(c);
		return nil;
	}
	c->db = show(c, "SHOW DB", 110, 554, &n);
	if(n != Eok){
		werrstr(Errors[n]);
		freedictc(c);
		return nil;
	}
	c->strat = show(c, "SHOW STRAT", 111, 555, &n);
	if(n != Eok){
		werrstr(Errors[n]);
		freedictc(c);
		return nil;
	}
	return c;
}

void
dictquit(Dictc *c)
{
	sendcmd(c, "QUIT", nil);
	freedictc(c);
}

char*
dbdesc(Dictc *c, char *db)
{
	Element *e;
	int i;

	for(i = 0; i < dvlen(c->db); i++){
		e = dvref(c->db, i);
		if(strcmp(e->name, db) == 0)
			return e->desc;
	}
	return nil;
}

Definition*
parsedefinition(Dictc *c)
{
	Definition *d;
	char *s, *p, *q, *src;
	String *sb;
	int n;

	s = Brdstr(c->bin, '\n', 1);
	if(s == nil){
		werrstr(Errors[Eeof]);
		return nil;
	}
	n = readstatus(s);
	if(n != 151){
		werrstr(Errors[Eunexpected]);
		return nil;
	}
	src = nil;
	p = strchr(s+5, '"'); /* skip NNN<space>" */
	if(p != nil){
		p += 2;
		q = strchr(p, ' ');
		*q = '\0';
		src = dbdesc(c, p);
	}
	free(s);
	sb = s_newalloc(255);
	for(;;){
		s = Brdstr(c->bin, '\n', 1);
		if(s == nil){
			s_free(sb);
			werrstr(Errors[Eeof]);
			return nil;
		}
		if(*s == '.'){
			free(s);
			break;
		}
		s[Blinelen(c->bin) - 1] = '\n'; /* replace \r with \n */
		s_append(sb, s);
		free(s);
	}
	s_terminate(sb);
	d = emalloc(sizeof *d);
	d->db   = src;
	d->text = strdup(s_to_c(sb));
	s_free(sb);
	return d;
}

Dvec*
dictdefine(Dictc* c, char *db, char *word)
{
	Dvec *v;
	Response r;
	Definition *d;
	char buf[1024] = {0};
	int rc, n, i;

	snprint(buf, sizeof buf, "DEFINE %s \"%s\"", db, word);
	rc = sendcmd(c, buf, &r);
	if(rc != Eok){
		werrstr(Errors[rc]);
		return nil;
	}
	if(r.code == 552)
		return mkdvec(1);
	if(r.code != 150){
		werrstr(Errors[Eunexpected]);
		return nil;
	}
	n = readstatus(r.msg);
	v = mkdvec(n);
	for(i = 0; i < n; i++){
		d = parsedefinition(c);
		if(d == nil)
			return nil; /* FIXME: cleanup vec */
		dvadd(v, d);
	}
	if((n = expectline(c, "250 ok")) != Eok){
		werrstr(Errors[n]);
		return nil;
	}
	return v;
}

