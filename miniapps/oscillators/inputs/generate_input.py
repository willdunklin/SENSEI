import sys
import random
from math import sqrt
pi = 3.1415

# script to generate new oscillator configs
# using random values within a set of ranges

# nosc -- number of osciallators to genrate
# nx,ny -- grid dimensions
# r0,r1 -- min/max width of the Guassian splat
# w0,w1 -- min/max frequency
# z0,z1 -- min/max damping coeficient
# dfrac -- ratio of damped to periodic type

nosc = 128
nx = ny = 2048

r0 = 8
r1 = 96

w0 = pi/4.
w1 = 16.*pi

z0 = 0.0
z1 = 1.0

dfrac = 0.75

x0 = r1
x1 = nx - r1

def rng(x0, x1):
    return x0 + (x1 - x0)*random.random()

sys.stderr.write('#type    center    r    omega0    zeta\n')


max_dist = 0.
min_dist = x1-x0

pts = []

i = 0
while i < nosc:
    x = rng(x0, x1)
    y = rng(x0, x1)
    w = rng(w0, w1)
    r = rng(r0, r1)
    z = rng(z0, z1)
    t = 'damped' if random.random() < dfrac else 'periodic'
    sys.stderr.write('%s    %d %d %d    %f    %f    %f\n'%(t,x,y,0.,r,w,z))
    pts += [x,y,z]
    i += 1

i = 0
while i < nosc:
    xa = pts[3*i]
    ya = pts[3*i + 1]
    za = pts[3*i + 2]
    j = 0
    while j < nosc:
        if i != j:
            xb = pts[3*j]
            yb = pts[3*j + 1]
            zb = pts[3*j + 2]
            dx = xb - xa
            dy = yb - ya
            dz = zb - za
            max_dist = max(max_dist, sqrt(dx**2+dy**2+dz**2))
            min_dist = min(min_dist, sqrt(dx**2+dy**2+dz**2))
        j += 1
    i += 1

sys.stderr.write('# max_dist = %f\n'%(max_dist))
sys.stderr.write('# min_dist = %f\n'%(min_dist))

