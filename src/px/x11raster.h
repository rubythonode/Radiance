/* Copyright 1990 Regents of the University of California */

/* SCCSid "$SunId$ LBL" */

/*
 * x11raster.h - header file for X routines using images.
 *
 *	3/1/90
 */

typedef struct {
	Display	*disp;				/* the display */
	int	screen;				/* the screen */
	Visual	*visual;			/* pointer to visual used */
	XImage	*image;				/* the X image */
	GC	gc;				/* private graphics context */
	int	ncolors;			/* number of colors */
	XColor	*cdefs;				/* color definitions */
	short	*pmap;				/* inverse pixel mapping */
	unsigned long	*pixels;		/* allocated table entries */
	Colormap	cmap;			/* installed color map */
	Pixmap	pm;				/* storage on server side */
}	XRASTER;

extern unsigned long	*map_rcolors();

extern Pixmap	make_rpixmap();

extern XRASTER	*make_raster();

#define put_raster(d,xdst,ydst,xr) \
		patch_raster(d,0,0,xdst,ydst, \
				(xr)->image->width,(xr)->image->height,xr)

#define patch_raster(d,xsrc,ysrc,xdst,ydst,width,height,xr) \
		(((xr)->pm == 0) \
		? XPutImage((xr)->disp,d,(xr)->gc,(xr)->image,xsrc,ysrc, \
				xdst,ydst,width,height) \
		: XCopyArea((xr)->disp,(xr)->pm,d,(xr)->gc,xsrc,ysrc, \
				width,height,xdst,ydst))
