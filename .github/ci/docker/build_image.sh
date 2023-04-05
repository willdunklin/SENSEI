#!/bin/bash

if [[ ! -f $(which docker) ]]; then
  echo "Cannot find docker."
  exit 1
fi

if [[ -z $1 ]]; then
  echo "Usage: build_image.sh <image-base-name>"
  exit 1
fi

if [ -f "$PWD/$1/Dockerfile" ]; then
  wdir=$PWD/$1
elif [ -f "$PWD/Dockerfile" ]; then
  wdir=$PWD
else
  echo "Cannot find Dockerfile: $1"
  exit 1
fi

base_name=$1
tag_date=$(date +%Y%m%d)
tag=$1-$tag_date

echo "Building container: senseiinsitu/ci:$tag"
echo "  Dockerfile: $wdir/Dockerfile"

# setup python server for remote buildcache
HOST_IP=$(docker network inspect bridge | python3 -c "import sys, json; print(json.load(sys.stdin)[0]['IPAM']['Config'][0]['Gateway'])")
# HOST_IP=127.0.0.1
HOST_PORT=12345
echo "$HOST_IP:$HOST_PORT" > $wdir/host-ip.txt
python3 -m http.server $HOST_PORT -b $HOST_IP &
pid=$!

docker build -t senseiinsitu/ci:$tag $wdir |& tee $1-build-log.txt

# kill the python server
kill $pid

# docker push senseiinsitu/ci:$tag
