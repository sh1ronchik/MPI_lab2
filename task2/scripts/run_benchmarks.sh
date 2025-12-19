#!/bin/bash

# Скрипт для запуска бенчмарков task2 OpenMP (N-body)

echo "Компиляция task2 OpenMP версии..."
gcc -fopenmp -O3 -o task2/scripts/task2 task2/scripts/task2.c -lm

if [ $? -ne 0 ]; then
    echo "Ошибка компиляции! Убедитесь, что gcc и OpenMP доступны."
    exit 1
fi

echo "Компиляция успешна!"
echo "======================================"
echo "Запуск бенчмарков для task2 OpenMP..."
echo "======================================"

# Параметры тестирования
TEND=10.0                          # время симуляции
INPUT_FILE="task2/data/input/three_body.txt"
NUM_RUNS=3                         # число запусков для усреднения

# Тесты с разным количеством потоков
for THREADS in 1 2 4 8 16; do
    echo ""
    echo "Запуск с $THREADS потоками..."
    ./task2/scripts/task2 $THREADS $TEND $INPUT_FILE $NUM_RUNS task2_openmp
done

echo ""
echo "======================================"
echo "Бенчмарки завершены!"
echo "Результаты сохранены в task2/data/task2_openmp_performance.csv"
echo "Файл с траекториями (последний прогон): task2/data/result.csv"
echo "======================================"
