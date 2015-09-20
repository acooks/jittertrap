#!/bin/sh

is_git_repo=$(git status) || exit 0;

CLEAN=$(git diff-index --quiet HEAD && git diff-index --quiet --cached HEAD || echo " *dirty*")

echo $CLEAN
