/* Copyright (c) 1997 Silicon Graphics, Inc. */

#ifndef lint
static char SCCSid[] = "$SunId$ SGI";
#endif

/*
 * OpenGL GLX driver for holodeck display.
 * Based on rview driver.
 */

#include "standard.h"
#include "tonemap.h"
#include "rhdriver.h"

#include  <GL/glx.h>

#include  "x11icon.h"

#ifndef FEQ
#define FEQ(a,b)	((a)-(b) <= FTINY && (a)-(b) >= -FTINY)
#endif

#ifndef int4
#define int4	int
#endif

#ifndef FREEPCT
#define FREEPCT		25		/* percentage of values to free */
#endif

#ifndef NCONEV
#define NCONEV		7		/* number of cone base vertices */
#endif
#ifndef CONEH
#define CONEH		0.25		/* cone height (fraction of depth) */
#endif
#ifndef CONEW
#define CONEW		0.035		/* cone width (fraction of screen) */
#endif

#define GAMMA		2.2		/* default gamma correction */

#define MOVPCT		7		/* percent distance to move /frame */
#define MOVDIR(b)	((b)==Button1 ? 1 : (b)==Button2 ? 0 : -1)
#define MOVDEG		(-5)		/* degrees to orbit CW/down /frame */
#define MOVORB(s)	((s)&ShiftMask ? 1 : (s)&ControlMask ? -1 : 0)

#define MINWIDTH	480		/* minimum graphics window width */
#define MINHEIGHT	400		/* minimum graphics window height */

#define VIEWDIST	356		/* assumed viewing distance (mm) */

#define BORWIDTH	5		/* border width */

#define  ourscreen	DefaultScreen(ourdisplay)
#define  ourroot	RootWindow(ourdisplay,ourscreen)
#define  ourmask	(StructureNotifyMask|ExposureMask|KeyPressMask|\
			ButtonPressMask|ButtonReleaseMask)

#define  levptr(etype)	((etype *)&currentevent)

struct driver	odev;			/* global device driver structure */

static XEvent  currentevent;		/* current event */

static int  mapped = 0;			/* window is mapped? */
static unsigned long  *pixval = NULL;	/* allocated pixels */
static unsigned long  ourblack=0, ourwhite=~0;

static Display  *ourdisplay = NULL;	/* our display */
static XVisualInfo  *ourvinf;		/* our visual information */
static Window  gwind = 0;		/* our graphics window */
static GLXContext	gctx;		/* our GLX context */

static double	mindepth = FHUGE;	/* minimum depth value so far */
static double	maxdepth = 0.;		/* maximum depth value */

static double	pwidth, pheight;	/* pixel dimensions (mm) */

static FVECT	conev[NCONEV];		/* drawing cone */
static double	coneh;			/* cone height */

static int	inpresflags;		/* input result flags */

static int	headlocked = 0;		/* lock vertical motion */

static struct {
	float		(*wp)[3];	/* world intersection point array */
	int4		*wd;		/* world direction array */
	TMbright	*brt;		/* encoded brightness array */
	BYTE		(*chr)[3];	/* encoded chrominance array */
	BYTE		(*rgb)[3];	/* tone-mapped color array */
	BYTE		*alpha;		/* alpha values */
	int		nl;		/* count of values */
	int		bl, tl;		/* bottom and top (next) value index */
	int		tml;		/* next value needing tone-mapping */
	int		drl;		/* next value in need of drawing */
	char		*base;		/* base of allocated memory */
}	rV;			/* our collection of values */

#define redraw()	(rV.drl = rV.bl)

static int  resizewindow(), getevent(), getkey(), moveview(),
		setGLview(), getmove(), fixwindow(), mytmflags(),
		newvalue(), drawvalue(), Compost(),
		FindValue(), AllocValues(), TMapValues(), FreeValues();


dev_open(id)			/* initialize X11 driver */
char  *id;
{
	extern char	*getenv();
	static int	atlBest[] = {GLX_RGBA, GLX_RED_SIZE,8,
				GLX_GREEN_SIZE,8, GLX_BLUE_SIZE,8,
				GLX_ALPHA_SIZE,8, GLX_DEPTH_SIZE,23,
				/*GLX_DOUBLEBUFFER,*/ None};
	static int	atlOK[] = {GLX_RGBA, GLX_RED_SIZE,4,
				GLX_GREEN_SIZE,4, GLX_BLUE_SIZE,4,
				GLX_ALPHA_SIZE,4, GLX_DEPTH_SIZE,15,
				/*GLX_DOUBLEBUFFER,*/ None};
	char	*gv;
	double	gamval = GAMMA;
	XSetWindowAttributes	ourwinattr;
	XWMHints	ourxwmhints;
	XSizeHints	oursizhints;
					/* open display server */
	ourdisplay = XOpenDisplay(NULL);
	if (ourdisplay == NULL)
		error(USER, "cannot open X-windows; DISPLAY variable set?\n");
					/* find a usable visual */
	ourvinf = glXChooseVisual(ourdisplay, ourscreen, atlBest);
	if (ourvinf == NULL)
		ourvinf = glXChooseVisual(ourdisplay, ourscreen, atlOK);
	if (ourvinf == NULL)
		error(USER, "no suitable visuals available");
					/* get a context */
	gctx = glXCreateContext(ourdisplay, ourvinf, NULL, GL_TRUE);
					/* set gamma and tone mapping */
	if ((gv = XGetDefault(ourdisplay, "radiance", "gamma")) != NULL
			|| (gv = getenv("DISPLAY_GAMMA")) != NULL)
		gamval = atof(gv);
	if (tmInit(mytmflags(), stdprims, gamval) == NULL)
		error(SYSTEM, "not enough memory in dev_open");
					/* open window */
	ourwinattr.background_pixel = ourblack;
	ourwinattr.border_pixel = ourblack;
	ourwinattr.event_mask = ourmask;
					/* this is stupid */
	ourwinattr.colormap = XCreateColormap(ourdisplay, ourroot,
				ourvinf->visual, AllocNone);
	gwind = XCreateWindow(ourdisplay, ourroot, 0, 0,
		DisplayWidth(ourdisplay,ourscreen)-2*BORWIDTH,
		DisplayHeight(ourdisplay,ourscreen)-2*BORWIDTH,
		BORWIDTH, ourvinf->depth, InputOutput, ourvinf->visual,
		CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &ourwinattr);
	if (gwind == 0)
		error(SYSTEM, "cannot create window\n");
   	XStoreName(ourdisplay, gwind, id);
					/* set window manager hints */
	ourxwmhints.flags = InputHint|IconPixmapHint;
	ourxwmhints.input = True;
	ourxwmhints.icon_pixmap = XCreateBitmapFromData(ourdisplay,
			gwind, x11icon_bits, x11icon_width, x11icon_height);
	XSetWMHints(ourdisplay, gwind, &ourxwmhints);
	oursizhints.min_width = MINWIDTH;
	oursizhints.min_height = MINHEIGHT;
	oursizhints.flags = PMinSize;
	XSetNormalHints(ourdisplay, gwind, &oursizhints);
					/* map the window and get its size */
	XMapWindow(ourdisplay, gwind);
	dev_input();
					/* allocate our value list */
	if (!AllocValues(DisplayWidth(ourdisplay,ourscreen) *
			DisplayHeight(ourdisplay,ourscreen) / 4))
		error(SYSTEM, "insufficient memory for value storage");
					/* figure out sensible view */
	pwidth = (double)DisplayWidthMM(ourdisplay, ourscreen) /
			DisplayWidth(ourdisplay, ourscreen);
	pheight = (double)DisplayHeightMM(ourdisplay, ourscreen) /
			DisplayHeight(ourdisplay, ourscreen);
	copystruct(&odev.v, &stdview);
	odev.v.type = VT_PER;
	odev.v.horiz = 2.*180./PI * atan(0.5/VIEWDIST*pwidth*odev.hres);
	odev.v.vert = 2.*180./PI * atan(0.5/VIEWDIST*pheight*odev.vres);
	odev.name = id;
	odev.ifd = ConnectionNumber(ourdisplay);
					/* set GLX context and clear buffers */
	glXMakeCurrent(ourdisplay, gwind, gctx);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glShadeModel(GL_FLAT);
	glDisable(GL_DITHER);
	glDrawBuffer(GL_FRONT_AND_BACK);
	glReadBuffer(GL_BACK);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	setGLview();			/* initialize view */
}


dev_close()			/* close our display and free resources */
{
	glXMakeCurrent(ourdisplay, None, NULL);
	glXDestroyContext(ourdisplay, gctx);
	XDestroyWindow(ourdisplay, gwind);
	gwind = 0;
	XCloseDisplay(ourdisplay);
	ourdisplay = NULL;
	tmDone(NULL);
	FreeValues();
	odev.v.type = 0;
	odev.hres = odev.vres = 0;
	odev.ifd = -1;
}


int
dev_view(nv)			/* assign new driver view */
register VIEW	*nv;
{
	if (nv->type != VT_PER ||		/* check view legality */
			nv->horiz > 120. || nv->vert > 120.) {
		error(COMMAND, "illegal view type/angle");
		nv->type = VT_PER;
		nv->horiz = odev.v.horiz;
		nv->vert = odev.v.vert;
		return(0);
	}
	if (nv != &odev.v) {
		if (!FEQ(nv->horiz,odev.v.horiz) ||	/* resize window? */
				!FEQ(nv->vert,odev.v.vert)) {
			int	dw = DisplayWidth(ourdisplay,ourscreen);
			int	dh = DisplayHeight(ourdisplay,ourscreen);

			dw -= 25;	/* for window frame */
			dh -= 50;
			odev.hres = 2.*VIEWDIST/pwidth *
					tan(PI/180./2.*nv->horiz);
			odev.vres = 2.*VIEWDIST/pheight *
					tan(PI/180./2.*nv->vert);
			if (odev.hres > dw) {
				odev.vres = dw * odev.vres / odev.hres;
				odev.hres = dw;
			}
			if (odev.vres > dh) {
				odev.hres = dh * odev.hres / odev.vres;
				odev.vres = dh;
			}
			XResizeWindow(ourdisplay, gwind, odev.hres, odev.vres);
		}
		copystruct(&odev.v, nv);
		setGLview();
	}
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	redraw();
	return(1);
}


int
dev_input()			/* get X11 input */
{
	inpresflags = 0;

	do
		getevent();

	while (XQLength(ourdisplay) > 0);

	return(inpresflags);
}


dev_value(c, p, v)		/* add a pixel value to our list */
COLR	c;
FVECT	p, v;
{
	register int	li;

	li = newvalue();
	VCOPY(rV.wp[li], p);
	rV.wd[li] = encodedir(v);
	tmCvColrs(&rV.brt[li], rV.chr[li], c, 1);
}


int
dev_flush()			/* flush output */
{
	if (mapped) {
		TMapValues(0);
		while (rV.drl != rV.tl) {
			drawvalue(rV.drl);
			if (++rV.drl >= rV.nl)
				rV.drl = 0;
		}
		glFlush();
	}
	return(XPending(ourdisplay));
}


static int
mytmflags()			/* figure out tone mapping flags */
{
	extern char	*progname;
	register char	*cp, *tail;
					/* find basic name */
	for (cp = tail = progname; *cp; cp++)
		if (*cp == '/')
			tail = cp+1;
	for (cp = tail; *cp && *cp != '.'; cp++)
		;
	if (cp-tail == 3 && !strncmp(tail, "glx", 3))
		return(TM_F_CAMERA);
	if (cp-tail == 4 && !strncmp(tail, "glxh", 4))
		return(TM_F_HUMAN);
	error(USER, "illegal driver name");
}


static
setGLview()			/* set our GL view */
{
	double	xmin, xmax, ymin, ymax, zmin, zmax;
	double	d, cx, sx, crad;
	FVECT	vx, vy;
	register int	i, j;
					/* compute view frustum */
	if (normalize(odev.v.vdir) == 0.0)
		return;
	if (mindepth < maxdepth) {
		zmin = 0.25*mindepth;
		zmax = 8.0*maxdepth;
	} else {
		zmin = 0.01;
		zmax = 1000.;
	}
	if (odev.v.vfore > FTINY)
		zmin = odev.v.vfore;
	if (odev.v.vaft > FTINY)
		zmax = odev.v.vaft;
	xmax = zmin * tan(PI/180./2. * odev.v.horiz);
	xmin = -xmax;
	d = odev.v.hoff * (xmax - xmin);
	xmin += d; xmax += d;
	ymax = zmin * tan(PI/180./2. * odev.v.vert);
	ymin = -ymax;
	d = odev.v.voff * (ymax - ymin);
	ymin += d; ymax += d;
					/* set view matrix */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(xmin, xmax, ymin, ymax, zmin, zmax);
	gluLookAt(odev.v.vp[0], odev.v.vp[1], odev.v.vp[2],
		odev.v.vp[0] + odev.v.vdir[0],
		odev.v.vp[1] + odev.v.vdir[1],
		odev.v.vp[2] + odev.v.vdir[2],
		odev.v.vup[0], odev.v.vup[1], odev.v.vup[2]);
					/* set viewport */
	glViewport(0, 0, odev.hres, odev.vres);
					/* initialize cone for Vornoi polys */
	coneh = CONEH*(zmax - zmin);
	crad = 0.5 * CONEW * 0.5*(xmax-xmin + ymax-ymin) * (zmin+coneh)/zmin;
	vy[0] = vy[1] = vy[2] = 0.;
	for (i = 0; i < 3; i++)
		if (odev.v.vdir[i] < 0.6 && odev.v.vdir[i] > -0.6)
			break;
	vy[i] = 1.;
	fcross(vx, vy, odev.v.vdir);
	normalize(vx);
	fcross(vy, odev.v.vdir, vx);
	for (j = 0, d = 0.; j < NCONEV; j++, d += 2.*PI/NCONEV) {
		cx = crad*cos(d); sx = crad*sin(d);
		for (i = 0; i < 3; i++)
			conev[j][i] = coneh*odev.v.vdir[i] +
					cx*vx[i] + sx*vy[i];
	}
}


static int
newvalue()			/* allocate a leaf from our pile */
{
	int	li;
	
	li = rV.tl++;
	if (rV.tl >= rV.nl)	/* get next leaf in ring */
		rV.tl = 0;
	if (rV.tl == rV.bl)	/* need to shake some free */
		Compost(FREEPCT);
	return(li);
}


static
drawvalue(li)			/* draw a pixel value as a cone */
register int	li;
{
	static FVECT	apex, disp;
	double	d, h, v;
	register int	i;
					/* compute cone's base displacement */
	VCOPY(apex, rV.wp[li]);
	disp[0] = apex[0] - odev.v.vp[0];
	disp[1] = apex[1] - odev.v.vp[1];
	disp[2] = apex[2] - odev.v.vp[2];
	if ((d = DOT(disp,odev.v.vdir)) <= odev.v.vfore || d <= FTINY)
		return;
	if (d > 1e5) {		/* background region */
		if (odev.v.vaft > FTINY)
			return;
		h = maxdepth/d;
		disp[0] *= h; disp[1] *= h; disp[2] *= h;
		apex[0] = odev.v.vp[0] + disp[0];
		apex[1] = odev.v.vp[1] + disp[1];
		apex[2] = odev.v.vp[2] + disp[2];
		d = maxdepth;
	} else if (d > maxdepth)
		maxdepth = d;
	h = DOT(disp,odev.v.hvec)/(d*odev.v.hn2);
	v = DOT(disp,odev.v.vvec)/(d*odev.v.vn2);
	if (fabs(h - odev.v.hoff) < 0.5 && fabs(v - odev.v.voff) < 0.5 &&
			d < mindepth)
{
fprintf(stderr, "min depth now %lf (%lf,%lf,%lf)\n", d, apex[0], apex[1], apex[2]);
		mindepth = d;
}
	for (i = 0; i < 3; i++)
		disp[i] = apex[i] + coneh*(h*odev.v.hvec[i] + v*odev.v.vvec[i]);
					/* draw pyramid (cone approx.) */
	glColor4ub(rV.rgb[li][0], rV.rgb[li][1], rV.rgb[li][2], rV.alpha[li]);
	glBegin(GL_TRIANGLE_FAN);
	glVertex3f(apex[0], apex[1], apex[2]);
	for (i = 0; i < NCONEV; i++)
		glVertex3d(conev[i][0] + disp[0], conev[i][1] + disp[1],
				conev[i][2] + disp[2]);
					/* connect last side to first */
	glVertex3d(conev[0][0] + disp[0], conev[0][1] + disp[1],
			conev[0][2] + disp[2]);
	glEnd();			/* done */
}


#define	LEAFSIZ		(3*sizeof(float)+sizeof(int4)+\
			sizeof(TMbright)+7*sizeof(BYTE))

static
AllocValues(n)			/* allocate space for n values */
register int	n;
{
	unsigned	nbytes;
	register unsigned	i;

	if (n <= 0)
		return(0);
	if (rV.nl >= n)
		return(rV.nl);
	else if (rV.nl > 0)
		free(rV.base);
				/* round space up to nearest power of 2 */
	nbytes = n*LEAFSIZ + 8;
	for (i = 1024; nbytes > i; i <<= 1)
		;
	n = (i - 8) / LEAFSIZ;	/* should we make sure n is even? */
	rV.base = (char *)malloc(n*LEAFSIZ);
	if (rV.base == NULL)
		return(0);
				/* assign larger alignment types earlier */
	rV.wp = (float (*)[3])rV.base;
	rV.wd = (int4 *)(rV.wp + n);
	rV.brt = (TMbright *)(rV.wd + n);
	rV.chr = (BYTE (*)[3])(rV.brt + n);
	rV.rgb = (BYTE (*)[3])(rV.chr + n);
	rV.alpha = (BYTE *)(rV.rgb + n);
	rV.nl = n;
	rV.drl = rV.tml = rV.bl = rV.tl = 0;
	return(n);
}

#undef	LEAFSIZ


static
FreeValues()			/* free our allocated values */
{
	if (rV.nl <= 0)
		return;
	free(rV.base);
	rV.base = NULL;
	rV.nl = 0;
}


static
TMapValues(redo)		/* map our values to RGB */
int	redo;
{
	int	aorg, alen, borg, blen;
					/* recompute mapping? */
	if (redo)
		rV.tml = rV.bl;
					/* already done? */
	if (rV.tml == rV.tl)
		return(1);
					/* compute segments */
	aorg = rV.tml;
	if (rV.tl >= aorg) {
		alen = rV.tl - aorg;
		blen = 0;
	} else {
		alen = rV.nl - aorg;
		borg = 0;
		blen = rV.tl;
	}
					/* (re)compute tone mapping? */
	if (rV.tml == rV.bl) {
		tmClearHisto();
		tmAddHisto(rV.brt+aorg, alen, 1);
		if (blen > 0)
			tmAddHisto(rV.brt+borg, blen, 1);
		if (tmComputeMapping(0., 0., 0.) != TM_E_OK)
			return(0);
		rV.drl = rV.bl;
	}
	if (tmMapPixels(rV.rgb+aorg, rV.brt+aorg,
			rV.chr+aorg, alen) != TM_E_OK)
		return(0);
	if (blen > 0)
		tmMapPixels(rV.rgb+borg, rV.brt+borg,
				rV.chr+borg, blen);
					/* compute unique alpha values */
	rV.tml = rV.tl;
	return(1);
}


static int
Compost(pct)			/* free up some values */
int	pct;
{
	int	nused, nclear, nmapped, ndrawn;
				/* figure out how many values to clear */
	nclear = rV.nl * pct / 100;
	nused = rV.tl - rV.bl;
	if (nused <= 0) nused += rV.nl;
	nclear -= rV.nl - nused;
	if (nclear <= 0)
		return(0);
	if (nclear >= nused) {	/* clear them all */
		rV.drl = rV.tml = rV.bl = rV.tl = 0;
		return(nused);
	}
				/* else clear values from bottom */
	ndrawn = rV.drl - rV.bl;
	if (ndrawn < 0) ndrawn += rV.nl;
	nmapped = rV.tml - rV.bl;
	if (nmapped < 0) nmapped += rV.nl;
	rV.bl += nclear;
	if (rV.bl >= rV.nl) rV.bl -= rV.nl;
	if (ndrawn < nclear) rV.drl = rV.bl;
	if (nmapped < nclear) rV.tml = rV.bl;
	return(nclear);
}


static
getevent()			/* get next event */
{
	XNextEvent(ourdisplay, levptr(XEvent));
	switch (levptr(XEvent)->type) {
	case ConfigureNotify:
		resizewindow(levptr(XConfigureEvent));
		break;
	case UnmapNotify:
		mapped = 0;
		break;
	case MapNotify:
		mapped = 1;
		break;
	case Expose:
		fixwindow(levptr(XExposeEvent));
		break;
	case KeyPress:
		getkey(levptr(XKeyPressedEvent));
		break;
	case ButtonPress:
		getmove(levptr(XButtonPressedEvent));
		break;
	}
}


static
draw3dline(wp)			/* draw 3d line in world coordinates */
register FVECT	wp[2];
{
	glVertex3d(wp[0][0], wp[0][1], wp[0][2]);
	glVertex3d(wp[1][0], wp[1][1], wp[1][2]);
}


static
draw_grids()			/* draw holodeck section grids */
{
	static GLubyte	gridrgba[4] = {0x0, 0xff, 0xff, 0x00};

	if (!mapped)
		return;
	glColor4ub(gridrgba[0], gridrgba[1], gridrgba[2], gridrgba[3]);
					/* draw each grid line */
	glBegin(GL_LINES);
	gridlines(draw3dline);
	glEnd();
}


static int
FindValue(dx, dy)		/* find a value on the display */
int	dx, dy;
{
	return(-1);		/* not found */
}


static
moveview(dx, dy, mov, orb)	/* move our view */
int	dx, dy, mov, orb;
{
	VIEW	nv;
	FVECT	odir, v1;
	double	d;
	register int	li;
				/* start with old view */
	copystruct(&nv, &odev.v);
				/* change view direction */
	if (mov | orb) {
		if ((li = FindValue(dx, dy)) < 0)
			return(0);	/* not on window */
		VSUM(odir, rV.wp[li], nv.vp, -1.);
	} else {
		if (viewray(nv.vp, nv.vdir, &odev.v,
				(dx+.5)/odev.hres, (dy+.5)/odev.vres) < -FTINY)
			return(0);	/* outside view */
	}
	if (orb && mov) {		/* orbit left/right */
		spinvector(odir, odir, nv.vup, d=MOVDEG*PI/180.*mov);
		VSUM(nv.vp, rV.wp[li], odir, -1.);
		spinvector(nv.vdir, nv.vdir, nv.vup, d);
	} else if (orb) {		/* orbit up/down */
		fcross(v1, odir, nv.vup);
		if (normalize(v1) == 0.)
			return(0);
		spinvector(odir, odir, v1, d=MOVDEG*PI/180.*orb);
		VSUM(nv.vp, rV.wp[li], odir, -1.);
		spinvector(nv.vdir, nv.vdir, v1, d);
	} else if (mov) {		/* move forward/backward */
		d = MOVPCT/100. * mov;
		VSUM(nv.vp, nv.vp, odir, d);
	}
	if (!mov ^ !orb && headlocked) {	/* restore head height */
		VSUM(v1, odev.v.vp, nv.vp, -1.);
		d = DOT(v1, odev.v.vup);
		VSUM(nv.vp, nv.vp, odev.v.vup, d);
	}
	if (setview(&nv) != NULL)
		return(0);	/* illegal view */
	dev_view(&nv);
	inpresflags |= DFL(DC_SETVIEW);
	return(1);
}


static
getmove(ebut)				/* get view change */
XButtonPressedEvent	*ebut;
{
	int	movdir = MOVDIR(ebut->button);
	int	movorb = MOVORB(ebut->state);
	Window	rootw, childw;
	int	rootx, rooty, wx, wy;
	unsigned int	statemask;

	XNoOp(ourdisplay);

	while (!XCheckMaskEvent(ourdisplay,
			ButtonReleaseMask, levptr(XEvent))) {

		if (!XQueryPointer(ourdisplay, gwind, &rootw, &childw,
				&rootx, &rooty, &wx, &wy, &statemask))
			break;		/* on another screen */

		if (!moveview(wx, odev.vres-1-wy, movdir, movorb)) {
			sleep(1);
			continue;
		}
		draw_grids();
		dev_flush();
	}
	if (!(inpresflags & DFL(DC_SETVIEW))) {	/* do final motion */
		movdir = MOVDIR(levptr(XButtonReleasedEvent)->button);
		wx = levptr(XButtonReleasedEvent)->x;
		wy = levptr(XButtonReleasedEvent)->y;
		moveview(wx, odev.vres-1-wy, movdir, movorb);
	}
	dev_flush();
}


static
getkey(ekey)				/* get input key */
register XKeyPressedEvent  *ekey;
{
	int  n;
	char	buf[8];

	n = XLookupString(ekey, buf, sizeof(buf), NULL, NULL);
	if (n != 1)
		return;
	switch (buf[0]) {
	case 'h':			/* turn on height motion lock */
		headlocked = 1;
		return;
	case 'H':			/* turn off height motion lock */
		headlocked = 0;
		return;
	case 'l':			/* retrieve last view */
		inpresflags |= DFL(DC_LASTVIEW);
		return;
	case 'p':			/* pause computation */
		inpresflags |= DFL(DC_PAUSE);
		return;
	case 'v':			/* spit out view */
		inpresflags |= DFL(DC_GETVIEW);
		return;
	case '\n':
	case '\r':			/* resume computation */
		inpresflags |= DFL(DC_RESUME);
		return;
	case CTRL('R'):			/* redraw screen */
		TMapValues(1);
		redraw();
		return;
	case CTRL('L'):			/* refresh from server */
		if (inpresflags & DFL(DC_REDRAW))
			return;
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDrawBuffer(GL_FRONT);
		glDisable(GL_DEPTH_TEST);
		draw_grids();
		glEnable(GL_DEPTH_TEST);
		glDrawBuffer(GL_FRONT_AND_BACK);
		glFlush();
		Compost(100);			/* get rid of old values */
		inpresflags |= DFL(DC_REDRAW);	/* resend values from server */
		return;
	case 'K':			/* kill rtrace process(es) */
		inpresflags |= DFL(DC_KILL);
		break;
	case 'R':			/* restart rtrace */
		inpresflags |= DFL(DC_RESTART);
		break;
	case 'C':			/* clobber holodeck */
		inpresflags |= DFL(DC_CLOBBER);
		break;
	case 'q':			/* quit the program */
		inpresflags |= DFL(DC_QUIT);
		return;
	default:
		XBell(ourdisplay, 0);
		return;
	}
}


static
fixwindow(eexp)				/* repair damage to window */
register XExposeEvent  *eexp;
{
	if (odev.hres == 0 || odev.vres == 0) {	/* first exposure */
		odev.hres = eexp->width;
		odev.vres = eexp->height;
	}
	if (eexp->width == odev.hres && eexp->height == odev.vres)
		TMapValues(1);
	if (!eexp->count)
		redraw();
}


static
resizewindow(ersz)			/* resize window */
register XConfigureEvent  *ersz;
{
	if (ersz->width == odev.hres && ersz->height == odev.vres)
		return;

	odev.hres = ersz->width;
	odev.vres = ersz->height;

	odev.v.horiz = 2.*180./PI * atan(0.5/VIEWDIST*pwidth*odev.hres);
	odev.v.vert = 2.*180./PI * atan(0.5/VIEWDIST*pheight*odev.vres);

	setGLview();

	inpresflags |= DFL(DC_SETVIEW);
}