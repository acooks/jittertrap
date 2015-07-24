#!/bin/sh

curr_branch=$(git rev-parse --abbrev-ref HEAD);
curr_remote=$(git config branch.$curr_branch.remote || echo "unknown");
remote_url=$(git ls-remote --get-url $curr_remote || echo "unknown");

echo $remote_url
