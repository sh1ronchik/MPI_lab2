#!/bin/bash

# Скрипт для запуска бенчмарков task2 CUDA (N-body)

echo "Компиляция task2 CUDA версии..."
nvcc -O3 -o task2/scripts/task2_cuda task2/scripts/task2_cuda.cu -lm

if [ $? -ne 0 ]; then
    echo "Ошибка компиляции! Убедитесь, что CUDA установлена."
    exit 1
fi

echo "Компиляция успешна!"
echo "======================================"
echo "Запуск бенчмарков для task2 CUDA..."
echo "======================================"

# Параметры тестирования
TEND=10.0        # Время симуляции
INPUT_FILE="task2/data/input/three_body.txt"
NUM_RUNS=3        # 3 запуска для усреднения

echo ""
echo "Запуск CUDA версии..."
./task2/scripts/task2_cuda $TEND $INPUT_FILE $NUM_RUNS task2_cuda

echo ""
echo "======================================"
echo "Бенчмарки завершены!"
echo "Результаты сохранены в task2/data/task2_cuda_performance.csv"
echo "======================================"
