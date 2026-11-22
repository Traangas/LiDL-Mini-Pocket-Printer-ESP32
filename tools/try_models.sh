#!/bin/zsh
# Try TiMini-Print model profiles against the Mini Pocket Printer one at a time.
# Logs go to logs/<model>.log so Claude can read them. Stops when you say it printed.
# Usage: tools/try_models.sh [model ...]
cd "${0:A:h}/.." || exit 1
mkdir -p logs
PY="$PWD/.venv/bin/python"
TM="$PWD/ref/TiMini-Print"
NAME="Mini Pocket Printer_BLE"

models=("$@")
if (( ${#models} == 0 )); then
  models=(phomemo_m02 phomemo_t02 phomemo_m02x luck_a2 gb01 gb03)
fi

for m in $models; do
  echo "\n=== $m ==="
  (cd "$TM" && "$PY" timiniprint_command_line.py -vv --printer-model "$m" \
      --bluetooth "$NAME" --text "TEST $m 0123456789") > "logs/$m.log" 2>&1
  echo "exit $? (log: logs/$m.log)"
  tail -5 "logs/$m.log"
  read "ans?Did anything print or the paper move? [y/N] "
  echo "$m printed=$ans" >> logs/results.txt
  [[ $ans == [yY]* ]] && { echo "Stopping: $m works."; break; }
  sleep 2
done
echo "\nResults:"; cat logs/results.txt
