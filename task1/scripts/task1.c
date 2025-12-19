/* task1.c
 * OpenMP вычисление множества Мандельброта
 * Вычисляет точки, принадлежащие множеству Мандельброта, используя параллельные потоки.
 *
 * Выход: CSV файл с координатами точек множества и файл с метриками производительности.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#include <time.h>

/* Конфигурационные константы */
#define MAX_ITERATIONS 1000   
#define ESCAPE_RADIUS 2.0     
#define REAL_MIN -2.5         
#define REAL_MAX 1.0
#define IMAG_MIN -1.0
#define IMAG_MAX 1.0

/* --- Утилиты работы с файловой системой --- */
void ensure_dir_exists(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp)-1] = '\0';
    
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* --- Получение информации о системе --- */
void get_cpu_info(char *cpu_info, size_t size) {
#ifdef __linux__
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    colon += 2; // skip ": "
                    strncpy(cpu_info, colon, size - 1);
                    cpu_info[size - 1] = '\0';
                    // удаляем перевод строки
                    char *newline = strchr(cpu_info, '\n');
                    if (newline) *newline = '\0';
                    fclose(f);
                    return;
                }
            }
        }
        fclose(f);
    }
#endif
    snprintf(cpu_info, size, "Unknown CPU");
}

/* --- Тест принадлежности множеству Mandelbrot --- */
/* Возвращает 1, если c = (real, imag) принадлежит множеству Mandelbrot, иначе 0 */
int is_in_mandelbrot(double c_real, double c_imag) {
    double z_real = 0.0;
    double z_imag = 0.0;
    
    for (int n = 0; n < MAX_ITERATIONS; n++) {
        double z_real_sq = z_real * z_real;
        double z_imag_sq = z_imag * z_imag;
        
        if (z_real_sq + z_imag_sq > ESCAPE_RADIUS * ESCAPE_RADIUS) {
            return 0;  
        }
        
        double new_z_imag = 2.0 * z_real * z_imag + c_imag;
        z_real = z_real_sq - z_imag_sq + c_real;
        z_imag = new_z_imag;
    }
    
    return 1;
}

/* --- Структура для хранения результатов --- */
typedef struct {
    double real;
    double imag;
} MandelbrotPoint;

/* --- Структура для хранения метрик производительности --- */
typedef struct {
    int nthreads;
    long long npoints;
    long long grid_dim;
    long long points_found;
    double computation_time;
    double min_time;
    double max_time;
    double avg_time;
    int num_runs;
} PerformanceMetrics;

/* --- Запись метрик производительности в CSV --- */
void write_performance_metrics(const char *csv_dir, const char *prefix, 
                                PerformanceMetrics *metrics, const char *cpu_info) {
    char fname[512];
    snprintf(fname, sizeof(fname), "%s/%s_performance.csv", csv_dir, prefix);
    
    /* Проверяем, существует ли файл (для добавления заголовка) */
    int file_exists = 0;
    FILE *test = fopen(fname, "r");
    if (test) {
        file_exists = 1;
        fclose(test);
    }
    
    FILE *f = fopen(fname, "a");
    if (!f) {
        fprintf(stderr, "Cannot open %s for writing\n", fname);
        return;
    }
    
    /* Записываем заголовок, если файл новый */
    if (!file_exists) {
        fprintf(f, "timestamp,cpu_info,nthreads,requested_points,grid_dim,actual_points,points_found,");
        fprintf(f, "found_percentage,computation_time,min_time,max_time,avg_time,num_runs\n");
    }
    
    /* Получаем текущее время */
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    /* Записываем данные */
    fprintf(f, "%s,\"%s\",%d,%lld,%lld,%lld,%lld,%.2f,%.6f,%.6f,%.6f,%.6f,%d\n",
            timestamp,
            cpu_info,
            metrics->nthreads,
            metrics->npoints,
            metrics->grid_dim,
            metrics->grid_dim * metrics->grid_dim,
            metrics->points_found,
            100.0 * metrics->points_found / (metrics->grid_dim * metrics->grid_dim),
            metrics->computation_time,
            metrics->min_time,
            metrics->max_time,
            metrics->avg_time,
            metrics->num_runs);
    
    fclose(f);
    printf("Performance metrics written to %s\n", fname);
}

/* --- Основная функция вычисления --- */
long long compute_mandelbrot(long long grid_dim, double real_step, double imag_step,
                              MandelbrotPoint **results_ptr, long long *result_capacity_ptr) {
    long long result_count = 0;
    MandelbrotPoint *results = *results_ptr;
    long long result_capacity = *result_capacity_ptr;
    
    #pragma omp parallel
    {
        /* Локальный буфер результатов для потока */
        long long local_capacity = 1000;
        long long local_count = 0;
        MandelbrotPoint *local_results = (MandelbrotPoint*)malloc(local_capacity * sizeof(MandelbrotPoint));
        
        if (!local_results) {
            fprintf(stderr, "Thread %d: malloc failed\n", omp_get_thread_num());
            exit(1);
        }
        
        /* Распределяем работу между потоками */
        #pragma omp for schedule(dynamic, 100)
        for (long long i = 0; i < grid_dim; i++) {
            double c_real = REAL_MIN + i * real_step;
            
            for (long long j = 0; j < grid_dim; j++) {
                double c_imag = IMAG_MIN + j * imag_step;
                
                /* Проверяем, принадлежит ли точка множеству Mandelbrot */
                if (is_in_mandelbrot(c_real, c_imag)) {
                    /* Расширяем локальный буфер при необходимости */
                    if (local_count >= local_capacity) {
                        local_capacity *= 2;
                        MandelbrotPoint *new_buf = (MandelbrotPoint*)realloc(local_results, 
                                                                              local_capacity * sizeof(MandelbrotPoint));
                        if (!new_buf) {
                            fprintf(stderr, "Thread %d: realloc failed\n", omp_get_thread_num());
                            free(local_results);
                            exit(1);
                        }
                        local_results = new_buf;
                    }
                    
                    /* Сохраняем точку */
                    local_results[local_count].real = c_real;
                    local_results[local_count].imag = c_imag;
                    local_count++;
                }
            }
        }
        
        /* Объединяем локальные результаты в глобальный массив */
        #pragma omp critical
        {
            /* Увеличиваем глобальный буфер при необходимости */
            while (result_count + local_count > result_capacity) {
                result_capacity *= 2;
                MandelbrotPoint *new_buf = (MandelbrotPoint*)realloc(results, 
                                                                      result_capacity * sizeof(MandelbrotPoint));
                if (!new_buf) {
                    fprintf(stderr, "Failed to grow global result buffer\n");
                    free(local_results);
                    exit(1);
                }
                results = new_buf;
            }
            
            /* Копируем локальные результаты в глобальный массив */
            memcpy(&results[result_count], local_results, local_count * sizeof(MandelbrotPoint));
            result_count += local_count;
        }
        
        free(local_results);
    }
    
    *results_ptr = results;
    *result_capacity_ptr = result_capacity;
    return result_count;
}

int main(int argc, char *argv[]) {
    /* Разбор аргументов командной строки */
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <nthreads> <npoints> [num_runs] [prefix]\n", argv[0]);
        fprintf(stderr, "  nthreads:  number of OpenMP threads\n");
        fprintf(stderr, "  npoints:   number of sample points (square root taken for grid dimension)\n");
        fprintf(stderr, "  num_runs:  number of runs for averaging (default: 1)\n");
        fprintf(stderr, "  prefix:    output file prefix (default: task1)\n");
        return 1;
    }
    
    int nthreads = atoi(argv[1]);
    long long npoints = atoll(argv[2]);
    int num_runs = (argc >= 4) ? atoi(argv[3]) : 1;
    const char *prefix = (argc >= 5) ? argv[4] : "task1";
    
    if (nthreads <= 0) {
        fprintf(stderr, "Error: nthreads must be positive, got %s\n", argv[1]);
        return 1;
    }
    
    if (npoints <= 0) {
        fprintf(stderr, "Error: npoints must be positive, got %s\n", argv[2]);
        return 1;
    }
    
    if (num_runs <= 0) {
        fprintf(stderr, "Error: num_runs must be positive, got %d\n", num_runs);
        return 1;
    }
    
    /* Устанавливаем число потоков OpenMP */
    omp_set_num_threads(nthreads);
    
    /* Получаем информацию о CPU */
    char cpu_info[256];
    get_cpu_info(cpu_info, sizeof(cpu_info));
    
    /* Создаём директорию для вывода */
    const char *csv_dir = "./task1/data";
    ensure_dir_exists(csv_dir);
    
    /* Вычисляем размеры сетки — возьмём сетку sqrt(npoints) x sqrt(npoints) */
    long long grid_dim = (long long)sqrt((double)npoints);
    long long actual_points = grid_dim * grid_dim;
    
    printf("=== OpenMP Mandelbrot Set Benchmark ===\n");
    printf("CPU: %s\n", cpu_info);
    printf("Threads: %d\n", nthreads);
    printf("Requested points: %lld\n", npoints);
    printf("Grid: %lld x %lld\n", grid_dim, grid_dim);
    printf("Actual points: %lld\n", actual_points);
    printf("Number of runs: %d\n", num_runs);
    printf("Measurement method: %s\n", num_runs > 1 ? "Average over multiple runs" : "Single run");
    printf("========================================\n\n");
    
    /* Вычисляем шаги для выборки комплексной плоскости */
    double real_step = (REAL_MAX - REAL_MIN) / (double)grid_dim;
    double imag_step = (IMAG_MAX - IMAG_MIN) / (double)grid_dim;
    
    /* Метрики производительности */
    PerformanceMetrics metrics;
    metrics.nthreads = nthreads;
    metrics.npoints = npoints;
    metrics.grid_dim = grid_dim;
    metrics.num_runs = num_runs;
    metrics.min_time = 1e9;
    metrics.max_time = 0.0;
    metrics.avg_time = 0.0;
    
    /* Переменные для хранения результатов */
    MandelbrotPoint *results = NULL;
    long long result_count = 0;
    long long result_capacity = actual_points / 10;
    
    /* Выполняем несколько запусков для усреднения */
    for (int run = 0; run < num_runs; run++) {
        printf("Run %d/%d: ", run + 1, num_runs);
        fflush(stdout);
        
        /* Выделяем память для результатов (или используем существующий буфер) */
        if (run == 0) {
            results = (MandelbrotPoint*)malloc(result_capacity * sizeof(MandelbrotPoint));
            if (!results) {
                fprintf(stderr, "Error: Failed to allocate memory for results\n");
                return 1;
            }
        } else {
            /* Для последующих запусков сбрасываем счётчик */
            result_count = 0;
        }
        
        /* Запускаем таймер */
        double start_time = omp_get_wtime();
        
        /* Выполняем вычисление */
        result_count = compute_mandelbrot(grid_dim, real_step, imag_step, &results, &result_capacity);
        
        /* Останавливаем таймер */
        double end_time = omp_get_wtime();
        double elapsed = end_time - start_time;
        
        /* Обновляем метрики */
        if (elapsed < metrics.min_time) metrics.min_time = elapsed;
        if (elapsed > metrics.max_time) metrics.max_time = elapsed;
        metrics.avg_time += elapsed;
        
        printf("Time = %.6f s, Found = %lld points (%.2f%%)\n",
               elapsed, result_count, 100.0 * result_count / actual_points);
    }
    
    /* Вычисляем среднее время */
    metrics.avg_time /= num_runs;
    metrics.computation_time = metrics.avg_time;
    metrics.points_found = result_count;
    
    printf("\n=== Performance Summary ===\n");
    printf("Points found: %lld (%.2f%% of samples)\n",
           result_count, 100.0 * result_count / actual_points);
    if (num_runs > 1) {
        printf("Min time:     %.6f seconds\n", metrics.min_time);
        printf("Max time:     %.6f seconds\n", metrics.max_time);
        printf("Avg time:     %.6f seconds\n", metrics.avg_time);
        printf("Std dev:      %.6f seconds\n", metrics.max_time - metrics.min_time);
    } else {
        printf("Elapsed time: %.6f seconds\n", metrics.computation_time);
    }
    printf("===========================\n\n");
    
    /* Записываем результаты в CSV файл */
    char csv_path[512];
    snprintf(csv_path, sizeof(csv_path), "%s/result.csv", csv_dir);
    
    FILE *f = fopen(csv_path, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s for writing: %s\n", csv_path, strerror(errno));
        free(results);
        return 1;
    }
    
    fprintf(f, "real,imaginary\n");
    
    /* Записываем все точки */
    for (long long i = 0; i < result_count; i++) {
        fprintf(f, "%.15f,%.15f\n", results[i].real, results[i].imag);
    }
    
    fclose(f);
    printf("Results written to %s\n", csv_path);
    
    /* Записываем метрики производительности */
    write_performance_metrics(csv_dir, prefix, &metrics, cpu_info);
    
    /* Очистка */
    free(results);
    
    return 0;
}
