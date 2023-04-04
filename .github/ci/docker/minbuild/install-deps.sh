#!/bin/bash

set -xe

source ${SPACK_ROOT}/share/spack/setup-env.sh

# make environment
spack env create ${SENSEI_ENV}
cp /sensei/bin/spack.yaml ${SPACK_ROOT}/var/spack/environments/${SENSEI_ENV}
spack env activate ${SENSEI_ENV}

# buildcache
spack mirror add sensei ${SENSEI_BUILDCACHE}
spack buildcache update-index sensei

# install
N_THREADS=$(grep -c '^processor' /proc/cpuinfo)
spack concretize -f
spack install -v --use-cache --no-check-signature -j ${N_THREADS} --only dependencies
spack install -v --use-cache --no-check-signature -j ${N_THREADS} sensei

spack clean -a
rm -rf /root/.spack

spack -e sensei env loads -m lmod
spack module lmod refresh -y
