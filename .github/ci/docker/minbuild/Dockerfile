FROM fedora:35

ENV SPACK_PYTHON=/usr/bin/python3
ENV SPACK_ROOT=/opt/spack
ENV SENSEI_BUILDCACHE=/sensei/buildcache
ENV SENSEI_ENV=sensei

# copy over buildcache | TODO: make this served online, not via copying
COPY buildcache ${SENSEI_BUILDCACHE}

# Install Pre-reqs and Spack
COPY install-prereqs.sh /sensei/bin/
RUN /sensei/bin/install-prereqs.sh

# Install SENSEI deps with spack
COPY spack.yaml /sensei/bin
COPY modules.yaml $SPACK_ROOT/etc/spack/
COPY install-deps.sh /sensei/bin/
RUN /sensei/bin/install-deps.sh

WORKDIR /sensei/
COPY configure-env.sh /sensei/bin/
RUN echo ". /sensei/bin/configure-env.sh" >> ~/.bashrc

ENTRYPOINT []
CMD ["bash","--login"]
