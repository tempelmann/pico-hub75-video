#!/bin/sh

mkdir build 2>/dev/null

cd build

if [ ! -f "Makefile" ]; then
	# rebuild the cache
	cmake ..
fi

cmake --build .

#env
