typedef struct Dictc Dictc;
typedef struct Dvec  Dvec;
typedef struct Element Element;
typedef struct Definition Definition;
typedef struct Entry Entry;
typedef struct Cols Cols;

#pragma incomplete Dvec;

struct Dictc
{
	int     fd;
	Biobuf* bin;
	Dvec*   db;
	Dvec*   strat;
};

struct Element
{
	char *name;
	char *desc;
};

struct Definition
{
	char *db;
	char *text;
};

struct Cols
{
	Image *back;
	Image *text;
	Image *focus;
	Image *sel;
	Image *scrl;
};

struct Entry
{
	Rectangle r;
	ushort state;
	int tickx;
	int p0, p1;
	int len;
	int size;
	int buttons;
	char *text;
	Channel *c;
	Cols *cols;
};

/* DICT client */
#define Dfirstmatch "!"
#define Dallmatches "*"

Dictc* dictdial(const char*, int);
void   dictquit(Dictc*);
Dvec*  dictdefine(Dictc*, char*, char*);

usize  dvlen(Dvec*);
void*  dvref(Dvec*, usize);

/* dview */
void dviewinit(Channel*, Cols*);
void dviewresize(Rectangle);
void dviewredraw(void);
void dviewmouse(Mouse);
void dviewkey(Rune);
void dviewset(Dvec*);

/* entry */
void entryinit(Entry*, Cols*);
void entryresize(Entry*, Rectangle);
void entryredraw(Entry*);
int  entrymouse(Entry*, Mouse);
void entrykey(Entry*, Rune);
int  entryhasfocus(Entry*);
void entryfocus(Entry*, int);
void entrysettext(Entry*, char*);

/* utils */
void *emalloc(ulong);
void *erealloc(void*, ulong);


