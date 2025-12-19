#!/bin/bash

# Скрипт для сравнения производительности пользовательской и библиотечной реализаций rwlock

echo "======================================"
echo "Компиляция программ..."
echo "======================================"

# Компиляция пользовательской реализации
gcc -pthread -O3 -o task3/scripts/task3_my_rwlock task3/scripts/task3_my_rwlock.c task3/scripts/my_rwlock.c -lm

if [ $? -ne 0 ]; then
    echo "Ошибка компиляции my_rwlock!"
    exit 1
fi

# Компиляция библиотечной реализации
gcc -pthread -O3 -o task3/scripts/task3_pthread_rwlock task3/scripts/task3_pthread_rwlock.c -lm

if [ $? -ne 0 ]; then
    echo "Ошибка компиляции pthread_rwlock!"
    exit 1
fi

echo "Компиляция успешна!"
echo ""

# Создание входных данных для тестов
create_input() {
    echo "$1"    # Initial keys
    echo "$2"    # Total ops
    echo "$3"    # Search percent
    echo "$4"    # Insert percent
}

echo "======================================"
echo "Тест 1: Много читателей (90% чтение)"
echo "======================================"

# Тест с 4 потоками
for THREADS in 1 2 4 8; do
    echo ""
    echo "--- $THREADS потоков ---"
    echo ""
    echo "Пользовательская реализация:"
    create_input "1000" "100000" "0.9" "0.05" | ./task3/scripts/task3_my_rwlock $THREADS
    
    echo ""
    echo "Библиотечная реализация:"
    create_input "1000" "100000" "0.9" "0.05" | ./task3/scripts/task3_pthread_rwlock $THREADS
done

echo ""
echo "======================================"
echo "Тест 2: Сбалансированная нагрузка"
echo "======================================"

for THREADS in 1 2 4 8; do
    echo ""
    echo "--- $THREADS потоков ---"
    echo ""
    echo "Пользовательская реализация:"
    create_input "1000" "100000" "0.5" "0.25" | ./task3/scripts/task3_my_rwlock $THREADS
    
    echo ""
    echo "Библиотечная реализация:"
    create_input "1000" "100000" "0.5" "0.25" | ./task3/scripts/task3_pthread_rwlock $THREADS
done

echo ""
echo "======================================"
echo "Сравнение завершено!"
echo "======================================"
