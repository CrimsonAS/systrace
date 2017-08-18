#!/usr/bin/env python
# Copyright (C) 2017 Crimson AS <info@crimson.no>
# Author: Robin Burchell <robin.burchell@crimson.no>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# to run:
# - qmake CONFIG+=silent
# - make SHELL=/path/to/this.py
#
# to analyse results, grab /tmp/tmp.json
#
# between runs:
# rm /tmp/buildtime.dat; rm /tmp/tmp.json
#
# things to fix:
# - would be nice to not require CONFIG+=silent
# - definitely not very resilient approach
# - would be nice to automatically clean up somehow each run
#   perhaps a starter script that rm's paths for you
# - I haven't written python in years, so this is probably all awful

import os
import errno
import sys
import datetime
import time

startTime = datetime.datetime.now()
os.system(' '.join(sys.argv[2:]))
endTime = datetime.datetime.now()

words=sys.argv[2].split(" ")

desc=""
ignore=False

if words[0] == "echo":
    if words[1] == "compiling":
        pass
    elif words[1] == "linking":
        pass
    elif words[1] == "generating":
        pass
    elif words[1] == "moc":
        pass
    else:
        print("wtf: " + " ".join(words))
        sys.exit(1)

if words[0] == "test":
    if words[1] == "-d":
        desc="mkdir " + words[len(words) - 1]
elif words[0] == "cd":
    desc = "cd " + words[1]
elif words[0] == "rm":
    i = 1
    while i < len(words) and words[i][0] == "-":
        i += 1
    if i == len(words):
        i = len(words)-1
    desc="rm " + words[i]
elif words[0] == "ar":
    i = 1
    while i < len(words) and words[i][0] != "/":
        i += 1
    if i == len(words):
        i = len(words)-1
    path_parts = words[i].split("/")
    desc = "ar " + path_parts[len(path_parts) - 1]
elif words[1] == "compiling":
    i=1
    while i < len(words) and words[i] != "&&":
        i += 1
    if i == len(words):
        i -= 1
    desc="compiling " + " ".join(words[2:i])
elif words[1] == "linking":
    i=1
    while i < len(words) and words[i] != "-o":
        i += 1
    if i == len(words):
        print("I can't deal with this")
        sys.exit(1)
    desc="linking " + words[i + 1]
elif words[1] == "generating":
    desc="generating " + words[2]
elif words[1] == "moc":
    desc="moc " + words[2]
elif words[0][0] == "/":
    # assume it's a command of some sort we don't know about and just put it
    # directly.
    desc=words[0]
elif words[0] == "g++" or words[0] == "gcc":
    # sigh
    desc=words[len(words) - 1]

if desc == "": # and ignore != True:
    print("UNHANDLED: " + " ".join(words))
    sys.exit(1)
else:
    print("SCRIPTY SCRIPT " + desc + " done in " + str(endTime-startTime) + " " + str(int((endTime-startTime).total_seconds() * 1000)) + " milliseconds")


epoch = None
loopy = True
tries = 0
while loopy == True:
    if (datetime.datetime.now() - endTime).seconds > 5:
        print("couldn't acquire build time")
        sys.exit(1)

    f = None
    try:
        f = open("/tmp/buildtime.dat", "r")
    except IOError as e:
        if e.errno == errno.ENOENT:
            f = None
            try:
                fd = os.open("/tmp/buildtime.dat", os.O_CREAT | os.O_WRONLY | os.O_EXCL)
                f = os.fdopen(fd, "w")
            except OSError as e:
                if e.errno == errno.EEXIST:
                    print("Looping! %d" % ++tries)
                    continue # retry
                else:
                    raise

            f.write(str(time.time()))
            print("Wrote time! %d" % ++tries)
            continue
        else:
            raise
    epoch = datetime.datetime.fromtimestamp(float(f.read()))
    loopy = False

# delta since start of trace (ts)
sdelta = (startTime-epoch)
sts=str((sdelta.total_seconds() * 1000000))

# delta since start of event (dur)
edelta = (endTime-startTime)
ets=str((edelta.total_seconds() * 1000000))

line="echo \"{\\\"pid\\\":0, \\\"tid\\\":0, \\\"ts\\\":%s, \\\"dur\\\":%s, \\\"ph\\\":\\\"X\\\", \\\"cat\\\":\\\"app\\\", \\\"name\\\":\\\"%s\\\"},\" >> /tmp/tmp.json" % (sts, ets, desc)
os.system(line)
