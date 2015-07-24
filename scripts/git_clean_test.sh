#!/bin/sh

CLEAN=$(git diff-index --quiet HEAD && git diff-index --quiet --cached HEAD || echo dirty)

echo $CLEAN
