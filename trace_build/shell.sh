#!/bin/zsh
zmodload zsh/datetime # for EPOCHREALTIME
START_TIME=$((EPOCHREALTIME*1000))
sh "$@" # run the thing
END_TIME=$((EPOCHREALTIME*1000))
ELAPSED_TIME=$(($END_TIME - $START_TIME))
words=$(echo $@ | cut -d' ' -f3-4) # try give some meaningful text for this. this fails hard, atm...
if [[ "$words" == "-pipe -stdlib=libc++" ]]; then
    # an example of how it fails, but there are fuckloads more
    words="dummy"
fi
# just debug
echo "$words started: $START_TIME elapsed $ELAPSED_TIME"
# the real interesting part
echo "{\"pid\":0, \"tid\":0, \"ts\":$START_TIME, \"dur\":$ELAPSED_TIME, \"ph\":\"X\", \"cat\":\"app\", \"name\":\"$words\"}," >> /tmp/tmp.json
