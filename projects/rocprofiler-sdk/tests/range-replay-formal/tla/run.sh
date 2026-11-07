#!/usr/bin/env bash
# Runs TLC configurations: ./run.sh <spec> [cfg...]; configs live in <spec>.d/
cd "$(dirname "$0")"
SPEC=${1:-MultiRankReplay}; shift || true
CFGS=("$@"); [ ${#CFGS[@]} -eq 0 ] && CFGS=($(cd $SPEC.d && ls *.cfg | sed 's/\.cfg$//'))
JAR=../tools/tla2tools-1.5.7.jar
mkdir -p out/$SPEC
for c in "${CFGS[@]}"; do
  log=out/$SPEC/$c.log
  java -XX:+UseParallelGC -Xmx16g -cp $JAR tlc2.TLC -workers 16 -config $SPEC.d/$c.cfg \
    -metadir out/$SPEC/$c-states $SPEC.tla > $log 2>&1
  verdict=$(grep -oE 'No error has been found|Invariant [A-Za-z]+ is violated|Temporal properties were violated|Deadlock reached|Error: .*' $log | head -1)
  states=$(grep -oE '[0-9,]+ distinct states found' $log | tail -1)
  printf '%-34s %-40s %s\n' "$c" "$verdict" "$states"
done
