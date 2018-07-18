#!/bin/sh
# get rid of everything
make clean
# CONFIG += silent is important, shell.sh needs it
~/code/qt/qtbase/bin/qmake CONFIG+=silent
# remove last run
rm /tmp/tmp.json
# start this run
echo "[" > /tmp/tmp.json
# go go go
make SHELL=/tmp/shell.sh -j8
