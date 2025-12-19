#!/bin/bash

# Универсальный скрипт компиляции всех заданий

echo "======================================"
echo "Компиляция всех заданий лабораторной"
echo "======================================"

# Проверка наличия GCC
if ! command -v gcc &> /dev/null; then
    echo "ОШИБКА: GCC не найден"
    exit 1
fi

# Task 1: Mandelbrot (OpenMP)
echo ""
echo "[1/5] Компиляция Task1 (Mandelbrot OpenMP)..."
gcc -fopenmp -O3 -o task1/scripts/task1 task1/scripts/task1.c -lm
if [ $? -eq 0 ]; then
    echo "✓ Task1 скомпилирована успешно"
else
    echo "✗ Ошибка компиляции Task1"
fi

# Task 2: N-body (OpenMP)
echo ""
echo "[2/5] Компиляция Task2 (N-body OpenMP)..."
gcc -fopenmp -O3 -o task2/scripts/task2 task2/scripts/task2.c -lm
if [ $? -eq 0 ]; then
    echo "✓ Task2 OpenMP скомпилирована успешно"
else
    echo "✗ Ошибка компиляции Task2 OpenMP"
fi

# Task 2: N-body (CUDA) - опционально
echo ""
echo "[3/5] Компиляция Task2 (N-body CUDA)..."
if command -v nvcc &> /dev/null; then
    nvcc -O3 -o task2/scripts/task2_cuda task2/scripts/task2_cuda.cu -lm
    if [ $? -eq 0 ]; then
        echo "✓ Task2 CUDA скомпилирована успешно"
    else
        echo "✗ Ошибка компиляции Task2 CUDA"
    fi
else
    echo "⚠ CUDA не найдена, пропускаем компиляцию CUDA версии"
fi

# Task 3: RWLock (пользовательская)
echo ""
echo "[4/5] Компиляция Task3 (Custom RWLock)..."
gcc -pthread -O3 -o task3/scripts/task3_my_rwlock \
    task3/scripts/task3_my_rwlock.c task3/scripts/my_rwlock.c -lm
if [ $? -eq 0 ]; then
    echo "✓ Task3 Custom RWLock скомпилирована успешно"
else
    echo "✗ Ошибка компиляции Task3 Custom"
fi

# Task 3: RWLock (библиотечная)
echo ""
echo "[5/5] Компиляция Task3 (Pthread RWLock)..."
gcc -pthread -O3 -o task3/scripts/task3_pthread_rwlock \
    task3/scripts/task3_pthread_rwlock.c -lm
if [ $? -eq 0 ]; then
    echo "✓ Task3 Pthread RWLock скомпилирована успешно"
else
    echo "✗ Ошибка компиляции Task3 Pthread"
fi

echo ""
echo "======================================"
echo "Компиляция завершена!"
echo "======================================"
echo ""
echo "Доступные команды для запуска:"
echo "  Task 1: ./task1/scripts/task1 <threads> <npoints>"
echo "  Task 2: ./task2/scripts/task2 <threads> <tend> <input_file>"
echo "  Task 2 CUDA: ./task2/scripts/task2_cuda <tend> <input_file>"
echo "  Task 3 Custom: ./task3/scripts/task3_my_rwlock <threads>"
echo "  Task 3 Pthread: ./task3/scripts/task3_pthread_rwlock <threads>"
echo ""
echo "Или используйте скрипты бенчмарков:"
echo "  ./task1/scripts/run_benchmarks.sh"
echo "  ./task2/scripts/run_benchmarks.sh"
echo "  ./task2/scripts/run_benchmarks_cuda.sh"
echo "  ./task3/scripts/run_comparison.sh"
