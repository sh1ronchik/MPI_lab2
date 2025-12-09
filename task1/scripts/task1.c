/* task1.c
 * OpenMP вычисление множества Мандельброта
 * Вычисляет точки, принадлежащие множеству Мандельброта, используя параллельные потоки.
 *
 * Выход: CSV файл с координатами точек множества.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>

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

int main(int argc, char *argv[]) {
    /* Разбор аргументов командной строки */
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <nthreads> <npoints>\n", argv[0]);
        fprintf(stderr, "  nthreads: number of OpenMP threads\n");
        fprintf(stderr, "  npoints:  number of sample points (square root taken for grid dimension)\n");
        return 1;
    }
    
    int nthreads = atoi(argv[1]);
    long long npoints = atoll(argv[2]);
    
    if (nthreads <= 0) {
        fprintf(stderr, "Error: nthreads must be positive, got %s\n", argv[1]);
        return 1;
    }
    
    if (npoints <= 0) {
        fprintf(stderr, "Error: npoints must be positive, got %s\n", argv[2]);
        return 1;
    }
    
    /* Устанавливаем число потоков OpenMP */
    omp_set_num_threads(nthreads);
    
    /* Создаём директорию для вывода */
    const char *csv_dir = "./task1/data";
    ensure_dir_exists(csv_dir);
    
    /* Вычисляем размеры сетки — возьмём сетку sqrt(npoints) x sqrt(npoints) */
    long long grid_dim = (long long)sqrt((double)npoints);
    long long actual_points = grid_dim * grid_dim;
    
    printf("OpenMP Mandelbrot Set: threads=%d, requested_points=%lld, grid=%lldx%lld, actual_points=%lld\n",
           nthreads, npoints, grid_dim, grid_dim, actual_points);
    
    /* Вычисляем шаги для выборки комплексной плоскости */
    double real_step = (REAL_MAX - REAL_MIN) / (double)grid_dim;
    double imag_step = (IMAG_MAX - IMAG_MIN) / (double)grid_dim;
    
    /* Выделяем память для результатов */
    MandelbrotPoint *results = NULL;
    long long result_count = 0;
    long long result_capacity = actual_points / 10; 
    
    results = (MandelbrotPoint*)malloc(result_capacity * sizeof(MandelbrotPoint));
    if (!results) {
        fprintf(stderr, "Error: Failed to allocate memory for results\n");
        return 1;
    }
    
    /* Запускаем таймер */
    double start_time = omp_get_wtime();
    
    /* Параллельный расчёт множества Mandelbrot */
    /* Каждый поток обрабатывает подмножество точек сетки */
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
    
    /* Останавливаем таймер */
    double end_time = omp_get_wtime();
    double elapsed = end_time - start_time;
    
    printf("Computation complete: found %lld points in Mandelbrot set (%.2f%% of samples)\n",
           result_count, 100.0 * result_count / actual_points);
    printf("Elapsed time: %.6f seconds\n", elapsed);
    
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
    
    /* Очистка */
    free(results);
    
    return 0;
}