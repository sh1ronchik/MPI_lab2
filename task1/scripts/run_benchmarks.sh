#!/bin/bash

# Скрипт для запуска бенчмарков task1 (Mandelbrot)

# Компиляция программы
echo "Компиляция task1..."
gcc -fopenmp -O3 -o task1/scripts/task1 task1/scripts/task1.c -lm

if [ $? -ne 0 ]; then
    echo "Ошибка компиляции!"
    exit 1
fi

echo "Компиляция успешна!"
echo "======================================"
echo "Запуск бенчмарков для task1..."
echo "======================================"

# Параметры тестирования
NPOINTS=10000000  # 10 миллионов точек
NUM_RUNS=3        # 3 запуска для усреднения

# Тесты с разным количеством потоков
for THREADS in 1 2 4 8 16; do
    echo ""
    echo "Запуск с $THREADS потоками..."
    ./task1/scripts/task1 $THREADS $NPOINTS $NUM_RUNS task1
done

echo ""
echo "======================================"
echo "Бенчмарки завершены!"
echo "Результаты сохранены в task1/data/task1_performance.csv"
echo "======================================"
