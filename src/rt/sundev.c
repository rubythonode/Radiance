/* Copyright (c) 1988 Regents of the University of California */

#ifndef lint
static char SCCSid[] = "$SunId$ LBL";
#endif

/*
 *  sundev.c - standalone driver for Sunwindows.
 *
 *     10/3/88
 */

#include  <stdio.h>
#include  <fcntl.h>
#include  <suntool/sunview.h>
#include  <suntool/canvas.h>
#include  <suntool/tty.h>

#include  "color.h"

#include  "driver.h"

short	icon_image[] = {
#include  "suntools.icon"
};
DEFINE_ICON_FROM_IMAGE(sun_icon, icon_image);

#ifndef TTYPROG
#define TTYPROG		"sun.com"	/* tty program */
#endif

#define GAMMA		2.0		/* exponent for color correction */

#define COMLH		3		/* number of command lines */

#define  hashcolr(c)	((67*(c)[RED]+59*(c)[GRN]+71*(c)[BLU])%NCOLORS)

int  colres = 128;			/* color resolution */
COLR  colrmap[256];			/* our color mapping */

#define NCOLORS		251		/* color table size (prime) */
#define FIRSTCOLOR	2		/* first usable entry */

COLR  colrtbl[NCOLORS];			/* our color table */

int  sun_clear(), sun_paintr(), sun_getcur(),
		sun_comout(), sun_comin();

int  (*dev_func[NREQUESTS])() = {		/* request handlers */
	sun_clear, sun_paintr, sun_getcur,
	sun_comout, sun_comin
};

FILE  *ttyin;

Frame  frame = 0;
Tty  tty = 0;
Canvas  canvas = 0;

int  xres = 0, yres = 0;

char  *progname;


main(argc, argv)
int	argc;
char	*argv[];
{
	extern Notify_value	my_notice_destroy();
	char	*ttyargv[4], arg1[10], arg2[10];
	int	pd[2];
	int	com;
	register Pixwin	*pw;

	progname = argv[0];

	frame = window_create(0, FRAME,
			FRAME_LABEL, argc > 1 ? argv[1] : argv[0],
			FRAME_ICON, &sun_icon,
			WIN_X, 0, WIN_Y, 0,
			0);
	if (frame == 0)
		quit("cannot create frame");
	if (pipe(pd) == -1)
		quit("cannot create pipe");
	sprintf(arg1, "%d", getppid());
	sprintf(arg2, "%d", pd[1]);
	ttyargv[0] = TTYPROG;
	ttyargv[1] = arg1;
	ttyargv[2] = arg2;
	ttyargv[3] = NULL;
#ifdef BSD
	fcntl(pd[0], F_SETFD, 1);
#endif
	tty = window_create(frame, TTY,
			TTY_ARGV, ttyargv,
			WIN_ROWS, COMLH,
			0);
	if (tty == 0)
		quit("cannot create tty subwindow");
	close(pd[1]);
	if ((ttyin = fdopen(pd[0], "r")) == NULL)
		quit("cannot open tty");
	canvas = window_create(frame, CANVAS,
			CANVAS_RETAINED, FALSE,
			WIN_INPUT_DESIGNEE, window_get(tty,WIN_DEVICE_NUMBER),
			WIN_CONSUME_PICK_EVENTS, WIN_NO_EVENTS,
				MS_LEFT, MS_MIDDLE, MS_RIGHT, 0,
			0);
	if (canvas == 0)
		quit("cannot create canvas");
	if (getmap() < 0)
		quit("not a color screen");
	window_set(canvas, CANVAS_RETAINED, TRUE, 0);
	notify_interpose_destroy_func(frame, my_notice_destroy);

	while ((com = getc(stdin)) != EOF) {	/* loop on requests */
		if (com >= NREQUESTS || dev_func[com] == NULL)
			quit("invalid request");
		(*dev_func[com])();
	}
	quit(NULL);
}


quit(msg)					/* exit */
char  *msg;
{
	if (msg != NULL) {
		fprintf(stderr, "%s: %s\n", progname, msg);
		kill(getppid(), SIGPIPE);
		exit(1);
	}
	exit(0);
}


Notify_value
my_notice_destroy(fr, st)		/* we're on our way out */
Frame	fr;
Destroy_status	st;
{
	if (st != DESTROY_CHECKING) {
		kill((int)window_get(tty, TTY_PID), SIGHUP);
		kill(getppid(), SIGHUP);
	}
	return(notify_next_destroy_func(fr, st));
}


sun_clear()				/* clear our canvas */
{
	register Pixwin  *pw;
	int  nwidth, nheight;

	fread(&nwidth, sizeof(nwidth), 1, stdin);
	fread(&nheight, sizeof(nheight), 1, stdin);
	pw = canvas_pixwin(canvas);
	if (nwidth != xres || nheight != yres) {
		window_set(frame, WIN_SHOW, FALSE, 0);
		window_set(canvas, CANVAS_AUTO_SHRINK, TRUE,
				CANVAS_AUTO_EXPAND, TRUE, 0);
		window_set(canvas, WIN_WIDTH, nwidth, WIN_HEIGHT, nheight, 0);
		window_set(canvas, CANVAS_AUTO_SHRINK, FALSE,
				CANVAS_AUTO_EXPAND, FALSE, 0);
		window_fit(frame);
		window_set(frame, WIN_SHOW, TRUE, 0);
		notify_do_dispatch();
		xres = nwidth;
		yres = nheight;
	}
	pw_writebackground(pw, 0, 0, xres, xres, PIX_SRC);
	newmap();
}


sun_paintr()				/* fill a rectangle */
{
	COLOR  col;
	int  pval;
	int  xmin, ymin, xmax, ymax;
	register Pixwin  *pw;

	fread(col, sizeof(COLOR), 1, stdin);
	fread(&xmin, sizeof(xmin), 1, stdin);
	fread(&ymin, sizeof(ymin), 1, stdin);
	fread(&xmax, sizeof(xmax), 1, stdin);
	fread(&ymax, sizeof(ymax), 1, stdin);
	pval = pixel(col);
	pw = canvas_pixwin(canvas);
	pw_rop(pw, xmin, yres-ymax, xmax-xmin, ymax-ymin,
			PIX_SRC|PIX_COLOR(pval), NULL, 0, 0);
}


sun_comout()			/* output a string to command line */
{
	char	out[256];

	mygets(out, stdin);
	ttyputs(out);
}


sun_comin()			/* input a string from the command line */
{
	char	buf[256];
						/* echo characters */
	do {
		mygets(buf, ttyin);
		ttyputs(buf);
	} while (buf[0]);
						/* get result */
	mygets(buf, ttyin);
						/* send reply */
	putc(COM_COMIN, stdout);
	myputs(buf, stdout);
	fflush(stdout);
}


sun_getcur()			/* get cursor position */
{
	Event	*ep;
	int	xpos, ypos;
	int	c;

	notify_no_dispatch();			/* allow read to block */
	window_set(canvas, WIN_CONSUME_KBD_EVENT, WIN_ASCII_EVENTS, 0);
	while (window_read_event(canvas, ep) == 0) {
		switch (event_id(ep)) {
		case MS_LEFT:
		case MB1:
			c = MB1;
			break;
		case MS_MIDDLE:
		case MB2:
			c = MB2;
			break;
		case MS_RIGHT:
		case MB3:
			c = MB3;
			break;
		case ABORT:
			c = ABORT;
			break;
		default:
			continue;
		}
		xpos = event_x(ep);
		ypos = yres-1 - event_y(ep);
		break;
	}
	window_set(canvas, WIN_IGNORE_KBD_EVENT, WIN_ASCII_EVENTS, 0);
	notify_do_dispatch();
	putc(COM_GETCUR, stdout);
	putc(c, stdout);
	fwrite(&xpos, sizeof(xpos), 1, stdout);
	fwrite(&ypos, sizeof(ypos), 1, stdout);
	fflush(stdout);
}


mygets(s, fp)				/* get string from file (with nul) */
register char	*s;
register FILE	*fp;
{
	register int	c;

	while ((c = getc(fp)) != EOF)
		if ((*s++ = c) == '\0')
			return;
	*s = '\0';
}


myputs(s, fp)				/* put string to file (with nul) */
register char	*s;
register FILE	*fp;
{
	do
		putc(*s, fp);
	while (*s++);
}


ttyputs(s)			/* put string out to tty subwindow */
register char	*s;
{
	char	buf[256];
	register char	*cp;

	for (cp = buf; *s; s++) {
		if (*s == '\n')
			*cp++ = '\r';
		*cp++ = *s;
	}
	ttysw_output(tty, buf, cp-buf);
}


int
pixel(col)			/* return pixel value for color */
COLOR  col;
{
	COLR  clr;
	register int  ndx;

	mapcolor(clr, col);
	while ((ndx = insertcolr(clr)) < 0) {
		reduce_colres();
		mapcolor(clr, col);
	}
	return(ndx+FIRSTCOLOR);
}


int
insertcolr(clr)			/* insert a new color */
register COLR  clr;
{
	int  hval;
	register int  ndx, i;

	hval = ndx = hashcolr(clr);
	
	for (i = 1; i < NCOLORS; i++) {
		if (colrtbl[ndx][EXP] == 0) {
			colrtbl[ndx][RED] = clr[RED];
			colrtbl[ndx][GRN] = clr[GRN];
			colrtbl[ndx][BLU] = clr[BLU];
			colrtbl[ndx][EXP] = COLXS;
			newcolr(ndx, clr);
			return(ndx);
		}
		if (		colrtbl[ndx][RED] == clr[RED] &&
				colrtbl[ndx][GRN] == clr[GRN] &&
				colrtbl[ndx][BLU] == clr[BLU]	)
			return(ndx);
		ndx = (hval + i*i) % NCOLORS;
	}
	return(-1);
}


newcolr(ndx, clr)		/* enter a color into hardware table */
int  ndx;
COLR  clr;
{
	register Pixwin  *pw;
	
	pw = canvas_pixwin(canvas);
	pw_putcolormap(pw, ndx+FIRSTCOLOR, 1,
			&clr[RED], &clr[GRN], &clr[BLU]);
	pw = (Pixwin *)window_get(tty, WIN_PIXWIN);
	pw_putcolormap(pw, ndx+FIRSTCOLOR, 1,
			&clr[RED], &clr[GRN], &clr[BLU]);
}


reduce_colres()				/* reduce the color resolution */
{
	COLR	oldtbl[NCOLORS];
	unsigned char	pixmap[256];
	Pixwin	*pw;
	register int	i, j;
	register unsigned char	*p;

	ttyputs("remapping colors...\n");
	bcopy(colrtbl, oldtbl, sizeof(oldtbl));
	colres >>= 1;
	newmap();
	for (i = 0; i < 256; i++)
		pixmap[i] = i;
	for (i = 0; i < NCOLORS; i++)
		if (oldtbl[i][EXP] != 0) {
			for (j = 0; j < 3; j++)
				oldtbl[i][j] = ((oldtbl[i][j]*colres/256) *
							256 + 128)/colres;
			pixmap[i+FIRSTCOLOR] = insertcolr(oldtbl[i])+FIRSTCOLOR;
		}
	pw = canvas_pixwin(canvas);
				/* I probably should get my own memory */
	p = (unsigned char *)mpr_d(pw->pw_prretained)->md_image;
	for (j = 0; j < yres; j++) {
		for (i = 0; i < xres; i++) {
			*p = pixmap[*p];
			p++;
		}
		if (xres&1)		/* 16-bit boundaries */
			p++;
	}
	pw_rop(pw, 0, 0, xres, yres, PIX_SRC, pw->pw_prretained, 0, 0);
}


mapcolor(clr, col)			/* map to our color space */
COLR  clr;
COLOR  col;
{
	register int  i, p;
					/* compute color table value */
	for (i = 0; i < 3; i++) {
		p = colval(col,i) * 255.0 + 0.5;
		if (p < 0) p = 0;
		else if (p > 255) p = 255;
		clr[i] = colrmap[p][i];
	}
	clr[EXP] = COLXS;
}


getmap()				/* allocate color map segments */
{
	char  cmsname[20];
	unsigned char  red[256], green[256], blue[256];
	register Pixwin  *pw;
	register int  i;

	for (i = 0; i < 256; i++)
		red[i] = green[i] = blue[i] = 128;
	red[0] = green[0] = blue[0] = 255;
	red[1] = green[1] = blue[1] = 0;
	red[255] = green[255] = blue[255] = 0;
	red[254] = green[254] = blue[254] = 255;
	red[253] = green[253] = blue[253] = 0;
						/* name shared segment */
	sprintf(cmsname, "rv%d", getpid());
						/* set canvas */
	pw = canvas_pixwin(canvas);
	if (pw->pw_pixrect->pr_depth < 8)
		return(-1);
	pw_setcmsname(pw, cmsname);
	pw_putcolormap(pw, 0, 256, red, green, blue);
						/* set tty subwindow */
	pw = (Pixwin *)window_get(tty, WIN_PIXWIN);
	pw_setcmsname(pw, cmsname);
	pw_putcolormap(pw, 0, 256, red, green, blue);
						/* set frame */
	pw = (Pixwin *)window_get(frame, WIN_PIXWIN);
	pw_setcmsname(pw, cmsname);
	pw_putcolormap(pw, 0, 256, red, green, blue);

	return(0);
}


newmap()				/* initialize our color table */
{
	double  pow();
	int  val;
	register int  i;

	for (i = 0; i < 256; i++) {
		val = pow(i/256.0, 1.0/GAMMA) * colres;
		val = (val*256 + 128) / colres;
		colrmap[i][RED] = colrmap[i][GRN] = colrmap[i][BLU] = val;
		colrmap[i][EXP] = COLXS;
	}
	for (i = 0; i < NCOLORS; i++)
		colrtbl[i][EXP] = 0;
}
