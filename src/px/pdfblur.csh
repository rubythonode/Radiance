#!/bin/csh -f
# SCCSid "$SunId$ LBL"
#
# Do depth-of-field blurring on picture
#
if ($#argv < 5) then
	echo "Usage: $0 aperture distance nsamp picfile zfile [pinterp opts]" >/dev/tty
	exit 1
endif
set a = "$1"
set d = "$2"
set n = "$3"
set pict = "$4"
set zbf = "$5"
set piopt = ($argv[6-]:q)
cnt $n | rcalc -e `vwright i < $pict` \
-e "M:19;a:$a/2;d:$d;N:$n;" -e 'tmax:PI*a*(M+1)' \
-e 't=tmax/N*($1+rand($1))' \
-e 'theta=2*M*PI/(M-1)*(M-sqrt(M*M-(M-1)/(PI*a)*t))' \
-e 'r=a*(1-(M-1)/(2*M*M*PI)*theta)' \
-e 'rcost=r*cos(theta);rsint=r*sin(theta)' \
-e 'opx=ipx+rcost*ihx+rsint*ivx' \
-e 'opy=ipy+rcost*ihy+rsint*ivy' \
-e 'opz=ipz+rcost*ihz+rsint*ivz' \
-e 'os=is-rcost/(d*ihn);ol=il-rsint/(d*ivn)' \
-o 'VIEW= -vp ${opx} ${opy} ${opz} -vs ${os} -vl ${ol}' \
| pinterp -vf $pict $piopt -B $pict $zbf
