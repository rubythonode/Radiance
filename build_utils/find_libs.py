from __future__ import print_function

import os

from SCons.SConf import SConf # aka Configure

def find_radlib(env):
	v = env.FindFile('helvet.fnt', './lib')
	if not v:
		print('''
	Radiance auxiliary support files not found.
	-> Download from radiance-online.org and extract.
	''')
		env.Exit()

def find_pyinstaller(env):
	if os.name != 'nt':
		return
	conf = SConf(env)
	oldpath = (env['ENV'].get('PATH'))
	try:
		env['ENV']['PATH'] = os.environ['PATH']
		pyinst = conf.CheckProg('pyinstaller.exe')
		if pyinst:
			env['PYINSTALLER'] = pyinst
			env['PYSCRIPTS'] = []
		env = conf.Finish()
	finally:
		env['ENV']['PATH'] = oldpath

def find_x11(env):
	# Search for libX11, remember the X11 library and include dirs
	for d in ('/usr/X11R6', '/usr/X11', '/usr/openwin'):
		if os.path.isdir (d):
			incdir = os.path.join(d, 'include')
			libdir = os.path.join(d, 'lib')
			env.Prepend(CPPPATH=[incdir]) # add temporarily
			env.Prepend(LIBPATH=[libdir]) 
			conf = SConf(env)
			if conf.CheckLibWithHeader('X11', 'X11/X.h', 'C', autoadd=0):
				env.Replace(X11INCLUDE=incdir)
				env.Replace(X11LIB=libdir)
			env['CPPPATH'].remove(incdir) # not needed for now
			env['LIBPATH'].remove(libdir)
			if env['X11INCLUDE']:
				# Check for SGI stereo extension
				if conf.CheckCHeader('X11/extensions/SGIStereo.h'):
					env['RAD_STEREO'] = '-DSTEREO'
				else: env['RAD_STEREO'] = '-DNOSTEREO'
				env = conf.Finish ()
				break
			env = conf.Finish ()


def find_gl(env):
	# Check for libGL, set flag
	dl = [(None,None)] # standard search path
	if env.has_key('X11INCLUDE'): # sometimes found there (Darwin)
		dl.append((env['X11INCLUDE'], env['X11LIB']))
	for incdir, libdir in dl:
		if incdir: env.Prepend(CPPPATH=[incdir]) # add temporarily
		if libdir: env.Prepend(LIBPATH=[libdir])
		conf = SConf(env)
		if (conf.CheckLib('GL')
				or conf.CheckLib('opengl32')
				or conf.CheckCHeader('OpenGL/gl.h')
				or conf.CheckCHeader('GL/gl.h')):
			env['OGL'] = 1
		if os.name == 'nt':
			if (conf.CheckLib('GLU') # for winrview
					or conf.CheckLib('glu32')
					or conf.CheckCHeader('OpenGL/glu.h')):
				env['GLU'] = 1
		if incdir: env['CPPPATH'].remove(incdir) # not needed for now
		if libdir: env['LIBPATH'].remove(libdir)
		if env.has_key('OGL'):
			if incdir: env.Replace(OGLINCLUDE=[incdir])
			if env.has_key('GLU'):
				if incdir: env.Replace(GLUINCLUDE=[incdir])
			#if libdir: env.Replace(OGLLIB=[libdir])
			conf.Finish()
			break
		conf.Finish()


def find_libtiff(env):
	# Check for libtiff, set flag and include/lib directories
	dl = [ (None,None), ] # standard search path
	cfgi = env.get('TIFFINCLUDE')
	cfgl = env.get('TIFFLIB')
	if cfgi or cfgl:
		dl.insert(0,(cfgi, cfgl))
	for incdir, libdir in dl:
		xenv = env.Clone()
		if incdir: xenv.Prepend(CPPPATH=[incdir]) # add temporarily
		if libdir:
			xenv.Prepend(LIBPATH=[libdir])
			xenv.Prepend(PATH=[libdir])
		conf = SConf(xenv)
		libname = 'tiff'
		if os.name == 'nt':
			xenv['INCPREFIX'] = '/I ' # Bug in SCons (uses '/I')
			libname = 'libtiff'
		if conf.CheckLib(libname, 'TIFFInitSGILog',
				header='''#include "tiff.h"''', autoadd=0):
			env['TIFFLIB_INSTALLED'] = 1
		if env.has_key('TIFFLIB_INSTALLED'):
			env.Replace(RAD_LIBTIFF=libname)
			if incdir: env.Replace(RAD_TIFFINCLUDE=[incdir])
			if libdir: env.Replace(RAD_TIFFLIB=[libdir])
			conf.Finish()
			break
		conf.Finish()

