/* task2_cuda.cu
 * CUDA решение задачи N тел
 * Моделирует движение N материальных точек под действием взаимного гравитационного притяжения.
 * Использует метод Эйлера 1-го порядка для численного интегрирования на GPU.
 *
 * Выход: CSV файл с траекториями всех частиц и файл с метриками производительности.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <cuda_runtime.h>

/* Физические константы */
#define G 6.67430e-11f  /* Гравитационная постоянная (м^3 кг^-1 с^-2) */
#define DT 0.01        /* Шаг по времени (секунды) */
#define OUTPUT_STEP 10 /* Записывать каждый N-ый шаг */

/* CUDA параметры */
#define BLOCK_SIZE 256
#define SOFTENING 1e-9f /* Параметр сглаживания для избежания сингулярностей */

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
void get_gpu_info(char *gpu_info, size_t size) {
    int device;
    cudaGetDevice(&device);
    
    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, device);
    
    snprintf(gpu_info, size, "%s (Compute %d.%d)", 
             prop.name, prop.major, prop.minor);
}

/* --- Структура для хранения состояния частицы --- */
typedef struct {
    float x, y, z;     /* Позиция */
    float vx, vy, vz;  /* Скорость */
    float mass;        /* Масса */
} Body;

/* --- Структура для хранения метрик производительности --- */
typedef struct {
    int nbodies;
    double tend;
    int total_steps;
    int output_steps;
    double computation_time;
    double min_time;
    double max_time;
    double avg_time;
    int num_runs;
    double dt;
    int block_size;
    int grid_size;
} PerformanceMetrics;

/* --- CUDA kernel для вычисления сил --- */
__global__ void compute_forces_kernel(Body *bodies, float *fx, float *fy, float *fz, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (i >= n) return;
    
    float xi = bodies[i].x;
    float yi = bodies[i].y;
    float zi = bodies[i].z;
    float mi = bodies[i].mass;
    
    for (int j = i + 1; j < n; j++) {
        
        float dx = bodies[j].x - xi;
        float dy = bodies[j].y - yi;
        float dz = bodies[j].z - zi;
        
        float r_sq = dx*dx + dy*dy + dz*dz + SOFTENING;
        float r = sqrtf(r_sq);
        float r_cubed = r_sq * r;

        if (r_cubed == 0.0f) continue;
        
        float force_factor = (G * mi * bodies[j].mass) / r_cubed;
        
        float Fx = force_factor * dx;
        float Fy = force_factor * dy;
        float Fz = force_factor * dz;

        /* Обновляем F[i] и F[j] с учётом направления:
           F_i += +F (направление r_j - r_i)
           F_j += -F (противоположно)
           Используем atomicAdd, т.к. несколько нитей могут одновременно писать в один элемент.
           (заметка: atomicAdd медленнее, но безопасен) */
        atomicAdd(&fx[i], Fx);
        atomicAdd(&fy[i], Fy);
        atomicAdd(&fz[i], Fz);

        atomicAdd(&fx[j], -Fx);
        atomicAdd(&fy[j], -Fy);
        atomicAdd(&fz[j], -Fz);
    }
}

/* --- CUDA kernel для обновления позиций и скоростей --- */
__global__ void update_bodies_kernel(Body *bodies, float *fx, float *fy, float *fz, int n, float dt) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (idx >= n) return;
    
    /* Обновляем позиции */
    bodies[idx].x += bodies[idx].vx * dt;
    bodies[idx].y += bodies[idx].vy * dt;
    bodies[idx].z += bodies[idx].vz * dt;

    /* Обновляем скорости */
    bodies[idx].vx += (fx[idx] / bodies[idx].mass) * dt;
    bodies[idx].vy += (fy[idx] / bodies[idx].mass) * dt;
    bodies[idx].vz += (fz[idx] / bodies[idx].mass) * dt;
}

/* --- Чтение входных данных из файла --- */
int read_bodies(const char *filename, Body **bodies, int *n) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open input file %s: %s\n", filename, strerror(errno));
        return 0;
    }
    
    if (fscanf(f, "%d", n) != 1 || *n <= 0) {
        fprintf(stderr, "Error: Invalid number of bodies in input file\n");
        fclose(f);
        return 0;
    }
    
    *bodies = (Body*)malloc(*n * sizeof(Body));
    if (!*bodies) {
        fprintf(stderr, "Error: Failed to allocate memory for bodies\n");
        fclose(f);
        return 0;
    }
    
    for (int i = 0; i < *n; i++) {
        double x, y, z, vx, vy, vz, mass;
        if (fscanf(f, "%lf %lf %lf %lf %lf %lf %lf", &x, &y, &z, &vx, &vy, &vz, &mass) != 7) {
            fprintf(stderr, "Error: Invalid data for body %d\n", i);
            free(*bodies);
            fclose(f);
            return 0;
        }
        
        (*bodies)[i].x = (float)x;
        (*bodies)[i].y = (float)y;
        (*bodies)[i].z = (float)z;
        (*bodies)[i].vx = (float)vx;
        (*bodies)[i].vy = (float)vy;
        (*bodies)[i].vz = (float)vz;
        (*bodies)[i].mass = (float)mass;
    }
    
    fclose(f);
    return 1;
}

/* --- Запись состояния в CSV файл --- */
void write_snapshot(FILE *f, double t, Body *bodies, int n) {
    fprintf(f, "%.6f", t);
    for (int i = 0; i < n; i++) {
        fprintf(f, ",%.15f,%.15f,%.15f", bodies[i].x, bodies[i].y, bodies[i].z);
    }
    fprintf(f, "\n");
}

/* --- Запись метрик производительности в CSV --- */
void write_performance_metrics(const char *csv_dir, const char *prefix,
                                PerformanceMetrics *metrics, const char *gpu_info) {
    char fname[512];
    snprintf(fname, sizeof(fname), "%s/%s_performance.csv", csv_dir, prefix);
    
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
    
    if (!file_exists) {
        fprintf(f, "timestamp,gpu_info,nbodies,tend,dt,total_steps,output_steps,");
        fprintf(f, "block_size,grid_size,computation_time,min_time,max_time,avg_time,num_runs\n");
    }
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(f, "%s,\"%s\",%d,%.6f,%.6f,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%d\n",
            timestamp, gpu_info,
            metrics->nbodies, metrics->tend, metrics->dt,
            metrics->total_steps, metrics->output_steps,
            metrics->block_size, metrics->grid_size,
            metrics->computation_time, metrics->min_time, metrics->max_time,
            metrics->avg_time, metrics->num_runs);
    
    fclose(f);
    printf("Performance metrics written to %s\n", fname);
}

/* --- Основная функция симуляции на GPU --- */
double simulate_nbody_cuda(Body *bodies_host, int n, double tend, double dt,
                           const char *output_file, int should_write,
                           int *block_size_out, int *grid_size_out) {
    int total_steps = (int)(tend / dt);
    
    /* Выделяем память на GPU */
    Body *bodies_dev;
    float *fx_dev, *fy_dev, *fz_dev;
    
    cudaMalloc(&bodies_dev, n * sizeof(Body));
    cudaMalloc(&fx_dev, n * sizeof(float));
    cudaMalloc(&fy_dev, n * sizeof(float));
    cudaMalloc(&fz_dev, n * sizeof(float));
    
    /* Копируем данные на GPU */
    cudaMemcpy(bodies_dev, bodies_host, n * sizeof(Body), cudaMemcpyHostToDevice);
    
    /* Настройка размеров блоков и сетки */
    int block_size = BLOCK_SIZE;
    int grid_size = (n + block_size - 1) / block_size;
    
    *block_size_out = block_size;
    *grid_size_out = grid_size;
    
    FILE *f = NULL;
    if (should_write) {
        f = fopen(output_file, "w");
        if (!f) {
            fprintf(stderr, "Error: Cannot open %s for writing: %s\n", 
                    output_file, strerror(errno));
            cudaFree(bodies_dev);
            cudaFree(fx_dev);
            cudaFree(fy_dev);
            cudaFree(fz_dev);
            return -1.0;
        }
        
        /* Записываем заголовок CSV */
        fprintf(f, "t");
        for (int i = 0; i < n; i++) {
            fprintf(f, ",x%d,y%d,z%d", i+1, i+1, i+1);
        }
        fprintf(f, "\n");
        
        /* Записываем начальное состояние */
        write_snapshot(f, 0.0, bodies_host, n);
    }
    
    /* Создаем события для замера времени */
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    
    cudaEventRecord(start);
    
    /* Основной цикл симуляции */
    for (int step = 1; step <= total_steps; step++) {
        double t = step * dt;

        cudaMemset(fx_dev, 0, n * sizeof(float));
        cudaMemset(fy_dev, 0, n * sizeof(float));
        cudaMemset(fz_dev, 0, n * sizeof(float));
        
        /* Вычисляем силы на GPU */
        compute_forces_kernel<<<grid_size, block_size>>>(bodies_dev, fx_dev, fy_dev, fz_dev, n);
        
        /* Обновляем позиции и скорости на GPU */
        update_bodies_kernel<<<grid_size, block_size>>>(bodies_dev, fx_dev, fy_dev, fz_dev, n, (float)dt);
        
        /* Записываем состояние с заданным интервалом */
        if (should_write && (step % OUTPUT_STEP == 0 || step == total_steps)) {
            cudaMemcpy(bodies_host, bodies_dev, n * sizeof(Body), cudaMemcpyDeviceToHost);
            write_snapshot(f, t, bodies_host, n);
        }
    }
    
    /* Синхронизируем GPU */
    cudaDeviceSynchronize();
    
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);
    
    float milliseconds = 0;
    cudaEventElapsedTime(&milliseconds, start, stop);
    double elapsed = milliseconds / 1000.0;
    
    /* Копируем финальное состояние обратно на хост */
    cudaMemcpy(bodies_host, bodies_dev, n * sizeof(Body), cudaMemcpyDeviceToHost);
    
    if (f) fclose(f);
    
    /* Освобождаем память GPU */
    cudaFree(bodies_dev);
    cudaFree(fx_dev);
    cudaFree(fy_dev);
    cudaFree(fz_dev);
    
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    
    return elapsed;
}

int main(int argc, char *argv[]) {
    /* Разбор аргументов командной строки */
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <tend> <input_file> [num_runs] [prefix]\n", argv[0]);
        fprintf(stderr, "  tend:       end time of simulation (seconds)\n");
        fprintf(stderr, "  input_file: file with masses, positions and velocities\n");
        fprintf(stderr, "  num_runs:   number of runs for averaging (default: 1)\n");
        fprintf(stderr, "  prefix:     output file prefix (default: task2_cuda)\n");
        return 1;
    }
    
    double tend = atof(argv[1]);
    const char *input_file = argv[2];
    int num_runs = (argc >= 4) ? atoi(argv[3]) : 1;
    const char *prefix = (argc >= 5) ? argv[4] : "task2_cuda";
    
    if (tend <= 0.0) {
        fprintf(stderr, "Error: tend must be positive, got %s\n", argv[1]);
        return 1;
    }
    
    if (num_runs <= 0) {
        fprintf(stderr, "Error: num_runs must be positive, got %d\n", num_runs);
        return 1;
    }
    
    /* Получаем информацию о GPU */
    char gpu_info[256];
    get_gpu_info(gpu_info, sizeof(gpu_info));
    
    /* Создаём директорию для вывода */
    const char *csv_dir = "./task2/data";
    ensure_dir_exists(csv_dir);
    
    /* Читаем входные данные */
    Body *bodies_original = NULL;
    int n = 0;
    
    if (!read_bodies(input_file, &bodies_original, &n)) {
        return 1;
    }
    
    int total_steps = (int)(tend / DT);
    int output_steps = (total_steps / OUTPUT_STEP) + 1;
    
    printf("=== CUDA N-Body Simulation Benchmark ===\n");
    printf("GPU: %s\n", gpu_info);
    printf("Number of bodies: %d\n", n);
    printf("Simulation time: %.6f seconds\n", tend);
    printf("Time step (dt): %.6f seconds\n", DT);
    printf("Total steps: %d\n", total_steps);
    printf("Output steps: %d\n", output_steps);
    printf("Number of runs: %d\n", num_runs);
    printf("Measurement method: %s\n", num_runs > 1 ? "Average over multiple runs" : "Single run");
    printf("==========================================\n\n");
    
    /* Метрики производительности */
    PerformanceMetrics metrics;
    metrics.nbodies = n;
    metrics.tend = tend;
    metrics.dt = DT;
    metrics.total_steps = total_steps;
    metrics.output_steps = output_steps;
    metrics.num_runs = num_runs;
    metrics.min_time = 1e9;
    metrics.max_time = 0.0;
    metrics.avg_time = 0.0;
    
    /* Создаём рабочую копию тел для симуляции */
    Body *bodies = (Body*)malloc(n * sizeof(Body));
    if (!bodies) {
        fprintf(stderr, "Error: Failed to allocate working copy of bodies\n");
        free(bodies_original);
        return 1;
    }
    
    char output_file[512];
    snprintf(output_file, sizeof(output_file), "%s/result_cuda.csv", csv_dir);
    
    /* Выполняем несколько запусков для усреднения */
    for (int run = 0; run < num_runs; run++) {
        printf("Run %d/%d: ", run + 1, num_runs);
        fflush(stdout);
        
        /* Копируем начальное состояние */
        memcpy(bodies, bodies_original, n * sizeof(Body));
        
        /* Запускаем симуляцию (записываем результаты только в последнем запуске) */
        int should_write = (run == num_runs - 1);
        int block_size, grid_size;
        double elapsed = simulate_nbody_cuda(bodies, n, tend, DT, output_file, should_write,
                                            &block_size, &grid_size);
        
        if (elapsed < 0.0) {
            fprintf(stderr, "Simulation failed\n");
            free(bodies);
            free(bodies_original);
            return 1;
        }
        
        if (run == 0) {
            metrics.block_size = block_size;
            metrics.grid_size = grid_size;
        }
        
        /* Обновляем метрики */
        if (elapsed < metrics.min_time) metrics.min_time = elapsed;
        if (elapsed > metrics.max_time) metrics.max_time = elapsed;
        metrics.avg_time += elapsed;
        
        printf("Time = %.6f s\n", elapsed);
    }
    
    /* Вычисляем среднее время */
    metrics.avg_time /= num_runs;
    metrics.computation_time = metrics.avg_time;
    
    printf("\n=== Performance Summary ===\n");
    printf("Block size: %d\n", metrics.block_size);
    printf("Grid size: %d\n", metrics.grid_size);
    if (num_runs > 1) {
        printf("Min time:     %.6f seconds\n", metrics.min_time);
        printf("Max time:     %.6f seconds\n", metrics.max_time);
        printf("Avg time:     %.6f seconds\n", metrics.avg_time);
        printf("Std dev:      %.6f seconds\n", metrics.max_time - metrics.min_time);
    } else {
        printf("Elapsed time: %.6f seconds\n", metrics.computation_time);
    }
    printf("Steps/second: %.2f\n", total_steps / metrics.avg_time);
    printf("===========================\n\n");
    
    printf("Results written to %s\n", output_file);
    
    /* Записываем метрики производительности */
    write_performance_metrics(csv_dir, prefix, &metrics, gpu_info);
    
    /* Очистка */
    free(bodies);
    free(bodies_original);
    
    return 0;
}
