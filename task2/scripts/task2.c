/* task2.c
 * OpenMP решение задачи N тел
 * Моделирует движение N материальных точек под действием взаимного гравитационного притяжения.
 * Использует метод Эйлера 1-го порядка для численного интегрирования.
 *
 * Выход: CSV файл с траекториями всех частиц и файл с метриками производительности.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#include <time.h>

/* Физические константы */
#define G 6.67430e-11  /* Гравитационная постоянная (м^3 кг^-1 с^-2) */
#define DT 0.01        /* Шаг по времени (секунды) - можно менять для точности */
#define OUTPUT_STEP 10 /* Записывать каждый N-ый шаг (для уменьшения размера файла) */

/* Мелкая константа для предотвращения деления на ноль при близких вкладах */
#define SOFTENING 1e-9

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
                    colon += 2;
                    strncpy(cpu_info, colon, size - 1);
                    cpu_info[size - 1] = '\0';
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

/* --- Структура для хранения состояния частицы --- */
typedef struct {
    double x, y, z;     /* Позиция */
    double vx, vy, vz;  /* Скорость */
    double mass;        /* Масса */
} Body;

/* --- Структура для хранения метрик производительности --- */
typedef struct {
    int nthreads;
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
} PerformanceMetrics;

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
        if (fscanf(f, "%lf %lf %lf %lf %lf %lf %lf",
                   &(*bodies)[i].x, &(*bodies)[i].y, &(*bodies)[i].z,
                   &(*bodies)[i].vx, &(*bodies)[i].vy, &(*bodies)[i].vz,
                   &(*bodies)[i].mass) != 7) {
            fprintf(stderr, "Error: Invalid data for body %d\n", i);
            free(*bodies);
            fclose(f);
            return 0;
        }
    }
    
    fclose(f);
    return 1;
}

/* --- Вычисление сил между всеми телами --- */
/* Использует третий закон Ньютона: Fpq = -Fqp для оптимизации */
void compute_forces(Body *bodies, int n, double *fx, double *fy, double *fz,
                    double *fx_all, double *fy_all, double *fz_all, int nthreads) {

    /* Обнуляем глобальные силы (глобальный буфер результата) */
    for (int i = 0; i < n; i++) {
        fx[i] = 0.0;
        fy[i] = 0.0;
        fz[i] = 0.0;
    }

    size_t per_thread = (size_t)n;

    /* Однократная параллельная область: вычисление + редукция внутри */
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        double *fx_loc = fx_all + (size_t)tid * per_thread;
        double *fy_loc = fy_all + (size_t)tid * per_thread;
        double *fz_loc = fz_all + (size_t)tid * per_thread;

        /* Сбрасываем локальную область */
        memset(fx_loc, 0, per_thread * sizeof(double));
        memset(fy_loc, 0, per_thread * sizeof(double));
        memset(fz_loc, 0, per_thread * sizeof(double));

        /* Вычисляем вклады пар (i,j) в локальные буферы — третий закон Ньютона соблюдается */
        #pragma omp for schedule(static)
        for (int i = 0; i < n - 1; i++) {
            double xi = bodies[i].x;
            double yi = bodies[i].y;
            double zi = bodies[i].z;
            double mi = bodies[i].mass;

            for (int j = i + 1; j < n; j++) {
                double dx = bodies[j].x - xi;
                double dy = bodies[j].y - yi;
                double dz = bodies[j].z - zi;

                double r_sq = dx*dx + dy*dy + dz*dz + SOFTENING;
                double inv_r = 1.0 / sqrt(r_sq);
                double inv_r3 = inv_r * inv_r * inv_r;

                double force_factor = G * mi * bodies[j].mass * inv_r3;

                double Fx = force_factor * dx;
                double Fy = force_factor * dy;
                double Fz = force_factor * dz;

                fx_loc[i] += Fx;
                fy_loc[i] += Fy;
                fz_loc[i] += Fz;

                fx_loc[j] -= Fx;
                fy_loc[j] -= Fy;
                fz_loc[j] -= Fz;
            }
        } /* end omp for (compute) */

        /* Барьер — все потоки закончили записывать в свои локальные буферы */
        #pragma omp barrier

        #pragma omp for schedule(static)
        for (int i = 0; i < n; i++) {
            double sfx = 0.0, sfy = 0.0, sfz = 0.0;
            size_t base = (size_t)i;
            for (int t = 0; t < nthreads; t++) {
                size_t idx = (size_t)t * per_thread + base;
                sfx += fx_all[idx];
                sfy += fy_all[idx];
                sfz += fz_all[idx];
            }
            fx[i] = sfx;
            fy[i] = sfy;
            fz[i] = sfz;
        }
    } 
}



/* --- Обновление позиций и скоростей методом Эйлера --- */
void update_bodies(Body *bodies, int n, double *fx, double *fy, double *fz, double dt) {
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        /* Обновляем позиции: x^n = x^(n-1) + v^(n-1) * dt */
        bodies[i].x += bodies[i].vx * dt;
        bodies[i].y += bodies[i].vy * dt;
        bodies[i].z += bodies[i].vz * dt;

        /* Обновляем скорости: v^n = v^(n-1) + F^(n-1)/m * dt */
        bodies[i].vx += (fx[i] / bodies[i].mass) * dt;
        bodies[i].vy += (fy[i] / bodies[i].mass) * dt;
        bodies[i].vz += (fz[i] / bodies[i].mass) * dt;
    }
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
                                PerformanceMetrics *metrics, const char *cpu_info) {
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
        fprintf(f, "timestamp,cpu_info,nthreads,nbodies,tend,dt,total_steps,output_steps,");
        fprintf(f, "computation_time,min_time,max_time,avg_time,num_runs\n");
    }
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(f, "%s,\"%s\",%d,%d,%.6f,%.6f,%d,%d,%.6f,%.6f,%.6f,%.6f,%d\n",
            timestamp, cpu_info,
            metrics->nthreads, metrics->nbodies, metrics->tend, metrics->dt,
            metrics->total_steps, metrics->output_steps,
            metrics->computation_time, metrics->min_time, metrics->max_time,
            metrics->avg_time, metrics->num_runs);
    
    fclose(f);
    printf("Performance metrics written to %s\n", fname);
}

/* --- Основная функция симуляции --- */
double simulate_nbody(Body *bodies, int n, double tend, double dt, 
                      const char *output_file, int should_write) {
    int total_steps = (int)(tend / dt);
    
    /* Массивы для хранения сил */
    double *fx = (double*)malloc(n * sizeof(double));
    double *fy = (double*)malloc(n * sizeof(double));
    double *fz = (double*)malloc(n * sizeof(double));
    
    if (!fx || !fy || !fz) {
        fprintf(stderr, "Error: Failed to allocate force arrays\n");
        free(fx); free(fy); free(fz);
        return -1.0;
    }
    
    FILE *f = NULL;
    if (should_write) {
        f = fopen(output_file, "w");
        if (!f) {
            fprintf(stderr, "Error: Cannot open %s for writing: %s\n", 
                    output_file, strerror(errno));
            free(fx); free(fy); free(fz);
            return -1.0;
        }
        
        /* Записываем заголовок CSV */
        fprintf(f, "t");
        for (int i = 0; i < n; i++) {
            fprintf(f, ",x%d,y%d,z%d", i+1, i+1, i+1);
        }
        fprintf(f, "\n");
        
        /* Записываем начальное состояние */
        write_snapshot(f, 0.0, bodies, n);
    }
    
    /* Добавляем: выделяем пер-поточные буферы один раз (не каждый шаг) */
    int nthreads_runtime = omp_get_max_threads();
    size_t per_thread = (size_t)n;
    size_t total_elems = (size_t)nthreads_runtime * per_thread;

    double *fx_all = (double*)malloc(total_elems * sizeof(double));
    double *fy_all = (double*)malloc(total_elems * sizeof(double));
    double *fz_all = (double*)malloc(total_elems * sizeof(double));
    if (!fx_all || !fy_all || !fz_all) {
        fprintf(stderr, "Error: Failed to allocate per-thread buffers (n=%d, nthreads=%d)\n", n, nthreads_runtime);
        free(fx_all); free(fy_all); free(fz_all);
        free(fx); free(fy); free(fz);
        if (f) fclose(f);
        return -1.0;
    }


    /* Запускаем таймер */
    double start_time = omp_get_wtime();
    
    /* Основной цикл симуляции */
    for (int step = 1; step <= total_steps; step++) {
        double t = step * dt;
        
        /* Вычисляем силы */
        compute_forces(bodies, n, fx, fy, fz, fx_all, fy_all, fz_all, nthreads_runtime);;
        
        /* Обновляем позиции и скорости */
        update_bodies(bodies, n, fx, fy, fz, dt);
        
        /* Записываем состояние с заданным интервалом */
        if (should_write && (step % OUTPUT_STEP == 0 || step == total_steps)) {
            write_snapshot(f, t, bodies, n);
        }
    }
    
    /* Останавливаем таймер */
    double end_time = omp_get_wtime();
    double elapsed = end_time - start_time;
       
    free(fx_all);
    free(fy_all);
    free(fz_all);
    
    if (f) fclose(f);
    free(fx); free(fy); free(fz);
    
    return elapsed;
}

int main(int argc, char *argv[]) {
    /* Разбор аргументов командной строки */
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <nthreads> <tend> <input_file> [num_runs] [prefix]\n", argv[0]);
        fprintf(stderr, "  nthreads:   number of OpenMP threads\n");
        fprintf(stderr, "  tend:       end time of simulation (seconds)\n");
        fprintf(stderr, "  input_file: file with masses, positions and velocities\n");
        fprintf(stderr, "  num_runs:   number of runs for averaging (default: 1)\n");
        fprintf(stderr, "  prefix:     output file prefix (default: task2)\n");
        return 1;
    }
    
    int nthreads = atoi(argv[1]);
    double tend = atof(argv[2]);
    const char *input_file = argv[3];
    int num_runs = (argc >= 5) ? atoi(argv[4]) : 1;
    const char *prefix = (argc >= 6) ? argv[5] : "task2";
    
    if (nthreads <= 0) {
        fprintf(stderr, "Error: nthreads must be positive, got %s\n", argv[1]);
        return 1;
    }
    
    if (tend <= 0.0) {
        fprintf(stderr, "Error: tend must be positive, got %s\n", argv[2]);
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
    
    printf("=== OpenMP N-Body Simulation Benchmark ===\n");
    printf("CPU: %s\n", cpu_info);
    printf("Threads: %d\n", nthreads);
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
    metrics.nthreads = nthreads;
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
    snprintf(output_file, sizeof(output_file), "%s/result.csv", csv_dir);
    
    /* Выполняем несколько запусков для усреднения */
    for (int run = 0; run < num_runs; run++) {
        printf("Run %d/%d: ", run + 1, num_runs);
        fflush(stdout);
        
        /* Копируем начальное состояние */
        memcpy(bodies, bodies_original, n * sizeof(Body));
        
        /* Запускаем симуляцию (записываем результаты только в последнем запуске) */
        int should_write = (run == num_runs - 1);
        double elapsed = simulate_nbody(bodies, n, tend, DT, output_file, should_write);
        
        if (elapsed < 0.0) {
            fprintf(stderr, "Simulation failed\n");
            free(bodies);
            free(bodies_original);
            return 1;
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
    write_performance_metrics(csv_dir, prefix, &metrics, cpu_info);
    
    /* Очистка */
    free(bodies);
    free(bodies_original);
    
    return 0;
}
