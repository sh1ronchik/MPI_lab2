#!/usr/bin/env bash
set -euo pipefail

# Запускается из корня проекта
ROOT_DIR="$(pwd)"
EXE="$ROOT_DIR/task1/scripts/task1"           
DATA_DIR="$ROOT_DIR/task1/data"
FINAL_CSV="$DATA_DIR/task1_performance.csv"

# Параметры
THREADS=(2 4 8)                          # список количеств потоков
POINTS=(100000 400000 1000000 10000000)  # список npoints
NUM_RUNS=3                               # num_runs для каждого запуска

mkdir -p "$DATA_DIR"

if [ ! -x "$EXE" ]; then
  cat >&2 <<EOF
Executable not found or not executable: $EXE
Скомпилируйте ваш код, например:
  gcc -fopenmp -O2 -o task1/task1 task1/task1.c
и запустите скрипт снова.
EOF
  exit 1
fi

echo "timestamp,nthreads,requested_points,grid_dim,actual_points,points_found,found_percentage,computation_time,min_time,max_time,avg_time,num_runs" > "$FINAL_CSV"

for t in "${THREADS[@]}"; do
  for p in "${POINTS[@]}"; do
    echo ">>> run: threads=$t points=$p runs=$NUM_RUNS"

    # Формируем уникальный префикс
    UNIQUE_PREFIX="run_${t}p_${p}pts_$(date +%s)_$$"
    RAW_CSV="$DATA_DIR/${UNIQUE_PREFIX}_performance.csv"

    "$EXE" "$t" "$p" "$NUM_RUNS" "$UNIQUE_PREFIX"

    # Проверяем, что файл создан
    if [ ! -f "$RAW_CSV" ]; then
      echo "Warning: expected file not found: $RAW_CSV" >&2
      continue
    fi

    # Берём последнюю непустую строку, которая содержит данные
    simplified_line=$(python3 - <<PY
import csv,sys
fname = r"$RAW_CSV"
rows = []
with open(fname, newline='', encoding='utf-8') as f:
    reader = csv.reader(f)
    for row in reader:
        if not row:
            continue
        rows.append(row)
if len(rows) <= 1:
    # нет данных
    sys.exit(0)
last = rows[-1]
if len(last) > 1:
    del last[1]
out = ",".join('\"{}\"'.format(x.replace('\"','\"\"')) if (',' in x or '"' in x or ' ' in x) else x for x in last)
print(out)
PY
)

    if [ -z "$simplified_line" ]; then
      echo "Warning: no data row found in $RAW_CSV" >&2
      rm -f "$RAW_CSV"
      continue
    fi

    # Дописываем упрощённую строку в итоговый CSV
    echo "$simplified_line" >> "$FINAL_CSV"
    echo "Appended simplified metrics to $FINAL_CSV"

    # Удаляем временный файл
    rm -f "$RAW_CSV"

  done
done

echo "All done. Simplified metrics at: $FINAL_CSV"
