# Состав команды
Работу выполнили студенты группы **22ПИ1**:

| Участник | Вклад в проект |
|---------|----------------|
| **Овсянников Артём Сергеевич** | Реализовал задание **1**: вычисление множества Мандельброта с использованием OpenMP, а также задание **2.1**: решение задачи N тел с использованием OpenMP. |
| **Шейх Руслан Халедович** |  |

# Руководство по настройке OpenMP проекта

## 1. Установка (Ubuntu)
```bash
sudo apt update
sudo apt install -y build-essential gcc libomp-dev
```

### Проверка поддержки OpenMP
```bash
gcc --version
echo | gcc -fopenmp -x c -E -dM - | grep -i openmp
```

## 2. Структура проекта
```
task1/
├── scripts/
│   ├── task1.c
│   └── task1_benchmarks.sh         # Тест производительности
├── data/
│   ├── result.csv                  # Результирующий файл
│   └── task1_performance.csv       # Метрики производительности

task2/
├── scripts/
│   └── task2.c
├── data/
│   ├── result.csv                  # Результирующий файл
│   ├── task2_performance.csv       # Метрики производительности
│   └── input/
│       ├── three_body.txt          # Пример: задача трех тел
```

## 3. Компиляция

### Task 1 (Множество Мандельброта)
```bash
gcc -fopenmp -O3 -std=c99 -Wall task1/scripts/task1.c -o task1/scripts/task1 -lm
```

### Task 2 (Задача N тел)
```bash
gcc -fopenmp -O3 -std=c99 -Wall task2/scripts/task2.c -o task2/scripts/task2 -lm
```

## 4. Запуск

### Task 1: Множество Мандельброта
```bash
./task1/scripts/task1 <nthreads> <npoints> 
```

**Параметры:**
- `nthreads` — количество потоков OpenMP
- `npoints` — количество точек выборки (программа использует сетку √npoints × √npoints)

**Примеры:**
```bash
./task1/scripts/task1 8 10000000 
```

---

### Task 2: Задача N тел
```bash
./task2/scripts/task2 <nthreads> <tend> <input_file>
```

**Параметры:**
- `nthreads` — количество потоков OpenMP
- `tend` — конечное время симуляции (секунды)
- `input_file` — путь к входному файлу с начальными условиями

**Формат входного файла:**
```
n
x1 y1 z1 vx1 vy1 vz1 mass1
x2 y2 z2 vx2 vy2 vz2 mass2
...
```

**Примеры запуска:**
```bash
./task2/scripts/task2 8 1000.0 task2/data/input/three_body.txt 
```

## 5. Формат выходных CSV

### Task 1: Множество Мандельброта
**Файл:** `task1/data/result.csv`
```csv
real,imaginary
-0.750000000000000,-0.100000000000000
-0.500000000000000,0.000000000000000
0.000000000000000,0.000000000000000
```

**Столбцы:**
- `real` — действительная часть комплексного числа c
- `imaginary` — мнимая часть комплексного числа c

**Файл:** `task1/data/task1_performance.csv`
```csv
timestamp,cpu_info,nthreads,requested_points,grid_dim,actual_points,points_found,found_percentage,computation_time,min_time,max_time,avg_time,num_runs
```

---

### Task 2: Задача N тел
**Файл:** `task2/data/result.csv`
```csv
t,x1,y1,z1,x2,y2,z2,...,xn,yn,zn
0.000000,0.0,0.0,0.0,149600000000.0,0.0,0.0,...
0.100000,0.0,100.0,0.0,149600000000.0,-100.0,0.0,...
```

**Столбцы:**
- `t` — время (секунды)
- `x1, y1, z1` — координаты первого тела (метры)
- `x2, y2, z2` — координаты второго тела (метры)
- ... для всех n тел

**Файл:** `task2/data/task2_performance.csv`
```csv
timestamp,cpu_info,nthreads,nbodies,tend,dt,total_steps,output_steps,computation_time,min_time,max_time,avg_time,num_runs
```

## 6. Используемые OpenMP функции и директивы

### 6.1. `omp_set_num_threads(int num_threads)`
**Назначение**: Устанавливает количество потоков для последующих параллельных регионов.

**Использование в проектах:**
```c
omp_set_num_threads(nthreads);
```
Задает количество потоков, передаваемое через аргумент командной строки в обеих задачах.

---

### 6.2. `omp_get_wtime()`
**Назначение**: Возвращает время в секундах с некоторого момента в прошлом (wall-clock time).

**Использование в проектах:**
```c
double start_time = omp_get_wtime();
// ... вычисления ...
double end_time = omp_get_wtime();
double elapsed = end_time - start_time;
```
Используется для измерения времени выполнения параллельных вычислений в обеих задачах.

---

### 6.3. `omp_get_thread_num()`
**Назначение**: Возвращает номер текущего потока (от 0 до num_threads-1).

**Использование в проектах:**
```c
fprintf(stderr, "Thread %d: malloc failed\n", omp_get_thread_num());
```
Используется для идентификации потока при выводе отладочной информации и сообщений об ошибках.

---

### 6.4. `#pragma omp parallel`
**Назначение**: Создает параллельный регион, в котором код выполняется несколькими потоками.

**Использование в Task 1:**
```c
#pragma omp parallel
{
    // Локальные буферы для каждого потока
    MandelbrotPoint *local_results = malloc(...);
    
    #pragma omp for schedule(dynamic, 100)
    for (long long i = 0; i < grid_dim; i++) {
        // Вычисление точек Мандельброта
    }
    
    #pragma omp critical
    {
        // Объединение результатов
    }
}
```

**Использование в Task 2:**
```c
#pragma omp parallel for schedule(dynamic, 1)
for (int q = 0; q < n; q++) {
    // Вычисление сил для каждого тела
}
```

Создает параллельный регион для распределенных вычислений.

---

### 6.5. `#pragma omp for schedule(type, chunk_size)`
**Назначение**: Распределяет итерации цикла между потоками.

**Типы планирования:**
- `schedule(dynamic, chunk)` — динамическое распределение итераций порциями

**Использование в Task 1:**
```c
#pragma omp for schedule(dynamic, 100)
for (long long i = 0; i < grid_dim; i++) {
    // Итерации распределяются динамически порциями по 100
}
```

**Обоснование для Task 1:** Вычисление принадлежности точки множеству Мандельброта имеет переменную сложность (разное количество итераций до достижения радиуса убегания). Динамическое распределение обеспечивает балансировку нагрузки.

**Использование в Task 2:**
```c
#pragma omp parallel for schedule(dynamic, 1)
for (int q = 0; q < n; q++) {
    // Вычисление силы, действующей на тело q
}

#pragma omp parallel for
for (int i = 0; i < n; i++) {
    // Обновление позиций и скоростей
}
```

**Обоснование для Task 2:** 
- Вычисление сил: динамическое распределение по одному телу для балансировки
- Обновление позиций: статическое распределение (по умолчанию), так как работа равномерна

---

### 6.6. `#pragma omp critical`
**Назначение**: Определяет критическую секцию — блок кода, который может выполняться только одним потоком одновременно.

**Использование в Task 1:**
```c
#pragma omp critical
{
    // Слияние локальных результатов потока в глобальный массив
    while (result_count + local_count > result_capacity) {
        result_capacity *= 2;
        results = realloc(results, result_capacity * sizeof(MandelbrotPoint));
    }
    memcpy(&results[result_count], local_results, local_count * sizeof(MandelbrotPoint));
    result_count += local_count;
}
```

**Обоснование:** Критическая секция необходима для безопасного объединения результатов из локальных буферов потоков в общий массив результатов, предотвращая гонки данных (race conditions).

---
