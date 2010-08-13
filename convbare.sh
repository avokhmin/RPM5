#!/bin/bash
# Convert in a crazy way a non bare git repository
# Use at you risk
[ -z "$1"   ] && echo "Usage: $0 projectdir" >&2 && exit 1
if [ ! -d "$1" ] 
then
echo "$0: not found directory $1" >&2 && exit 1
fi
proj="$1"
pd="/etc/puppet/data"
[ ! -d $pd/$proj/gitrepo ] && echo "$0: $pd/$proj/gitrepo doesn't not exists" >&2 && exit 1
cd $proj 
cd gitrepo || { exit "$0: not found gitrepo" >&2 && exit 1 ; }
ris="$(git config  core.bare)"
[ -z "$ris" ] && echo "$0: git config core.bare broke $1" >&2 && exit 1
[ x$ris != xtrue ] && git reset --hard HEAD && git config core.bare true
if [ -d .git ] 
then
{ [ -f .git/config ] && cp -a .git/* . && rm -rf .git && [ -d "$proj" ] && rm -rf "$proj" ; } || { echo "$0: error in dropping .git in gitrepo" >&2 && exit 1 ; }
fi
{ cd ../testing && git  config --replace-all remote.origin.url $pd/$proj/gitrepo ; } || { echo "$0: error cd ../testing" >&2 && exit 1 ; } 
{ cd ../production && git config --replace-all remote.origin.url $pd/$proj/gitrepo ;  } || { echo "$0: error cd ../production" >&2 && exit 1 ; } 

