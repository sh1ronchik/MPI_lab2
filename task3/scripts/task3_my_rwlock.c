/* task3_my_rwlock.c
 * Тестирование своей реализации read-write блокировок
 * на примере односвязного списка
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include "my_rwlock.h"

/* Константы */
const int MAX_KEY = 100000000;

/* Структура узла списка */
struct list_node_s {
    int data;
    struct list_node_s* next;
};

/* Глобальные переменные */
struct list_node_s* head = NULL;
int thread_count;
int total_ops;
double insert_percent;
double search_percent;
double delete_percent;
my_rwlock_t rwlock;
pthread_mutex_t count_mutex;
int member_count = 0, insert_count = 0, delete_count = 0;

/* Функции работы со списком */
int Insert(int value);
int Member(int value);
int Delete(int value);
void Free_list(void);
int Is_empty(void);

/* Генератор случайных чисел */
unsigned my_rand(unsigned* seed) {
    *seed = *seed * 1103515245 + 12345;
    return (*seed / 65536) % 32768;
}

double my_drand(unsigned* seed) {
    return ((double)my_rand(seed)) / 32768.0;
}

/* Вставка значения в список */
int Insert(int value) {
    struct list_node_s* curr = head;
    struct list_node_s* pred = NULL;
    struct list_node_s* temp;
    int rv = 1;
    
    while (curr != NULL && curr->data < value) {
        pred = curr;
        curr = curr->next;
    }
    
    if (curr == NULL || curr->data > value) {
        temp = malloc(sizeof(struct list_node_s));
        temp->data = value;
        temp->next = curr;
        if (pred == NULL)
            head = temp;
        else
            pred->next = temp;
    } else {
        rv = 0;
    }
    
    return rv;
}

/* Поиск значения в списке */
int Member(int value) {
    struct list_node_s* temp = head;
    
    while (temp != NULL && temp->data < value)
        temp = temp->next;
    
    if (temp == NULL || temp->data > value) {
        return 0;
    } else {
        return 1;
    }
}

/* Удаление значения из списка */
int Delete(int value) {
    struct list_node_s* curr = head;
    struct list_node_s* pred = NULL;
    int rv = 1;
    
    while (curr != NULL && curr->data < value) {
        pred = curr;
        curr = curr->next;
    }
    
    if (curr != NULL && curr->data == value) {
        if (pred == NULL) {
            head = curr->next;
            free(curr);
        } else {
            pred->next = curr->next;
            free(curr);
        }
    } else {
        rv = 0;
    }
    
    return rv;
}

/* Освобождение памяти списка */
void Free_list(void) {
    struct list_node_s* current;
    struct list_node_s* following;
    
    if (Is_empty()) return;
    current = head;
    following = current->next;
    while (following != NULL) {
        free(current);
        current = following;
        following = current->next;
    }
    free(current);
}

/* Проверка на пустоту */
int Is_empty(void) {
    return (head == NULL);
}

/* Функция потока */
void* Thread_work(void* rank) {
    long my_rank = (long) rank;
    int i, val;
    double which_op;
    unsigned seed = my_rank + time(NULL);
    int my_member_count = 0, my_insert_count = 0, my_delete_count = 0;
    int ops_per_thread = total_ops / thread_count;
    
    for (i = 0; i < ops_per_thread; i++) {
        which_op = my_drand(&seed);
        val = my_rand(&seed) % MAX_KEY;
        
        if (which_op < search_percent) {
            /* Операция поиска - блокировка на чтение */
            my_rwlock_rdlock(&rwlock);
            Member(val);
            my_rwlock_unlock(&rwlock);
            my_member_count++;
        } else if (which_op < search_percent + insert_percent) {
            /* Операция вставки - блокировка на запись */
            my_rwlock_wrlock(&rwlock);
            Insert(val);
            my_rwlock_unlock(&rwlock);
            my_insert_count++;
        } else {
            /* Операция удаления - блокировка на запись */
            my_rwlock_wrlock(&rwlock);
            Delete(val);
            my_rwlock_unlock(&rwlock);
            my_delete_count++;
        }
    }
    
    pthread_mutex_lock(&count_mutex);
    member_count += my_member_count;
    insert_count += my_insert_count;
    delete_count += my_delete_count;
    pthread_mutex_unlock(&count_mutex);
    
    return NULL;
}

/* Вывод справки */
void Usage(char* prog_name) {
    fprintf(stderr, "Usage: %s <thread_count>\n", prog_name);
    exit(0);
}

/* Получение входных данных */
void Get_input(int* inserts_in_main_p) {
    printf("How many keys should be inserted in the main thread?\n");
    scanf("%d", inserts_in_main_p);
    printf("How many ops total should be executed?\n");
    scanf("%d", &total_ops);
    printf("Percent of ops that should be searches? (between 0 and 1)\n");
    scanf("%lf", &search_percent);
    printf("Percent of ops that should be inserts? (between 0 and 1)\n");
    scanf("%lf", &insert_percent);
    delete_percent = 1.0 - (search_percent + insert_percent);
}

/* Главная функция */
int main(int argc, char* argv[]) {
    long i;
    int key, success, attempts;
    pthread_t* thread_handles;
    int inserts_in_main;
    unsigned seed = 1;
    struct timespec start, finish;
    double elapsed;
    
    if (argc != 2) Usage(argv[0]);
    thread_count = strtol(argv[1], NULL, 10);
    
    Get_input(&inserts_in_main);
    
    /* Вставляем начальные ключи */
    i = attempts = 0;
    while (i < inserts_in_main && attempts < 2*inserts_in_main) {
        key = my_rand(&seed) % MAX_KEY;
        success = Insert(key);
        attempts++;
        if (success) i++;
    }
    printf("Inserted %ld keys in empty list\n", i);
    
    thread_handles = malloc(thread_count * sizeof(pthread_t));
    pthread_mutex_init(&count_mutex, NULL);
    my_rwlock_init(&rwlock);
    
    /* Замер времени */
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (i = 0; i < thread_count; i++)
        pthread_create(&thread_handles[i], NULL, Thread_work, (void*) i);
    
    for (i = 0; i < thread_count; i++)
        pthread_join(thread_handles[i], NULL);
    
    clock_gettime(CLOCK_MONOTONIC, &finish);
    elapsed = (finish.tv_sec - start.tv_sec);
    elapsed += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    
    printf("\n=== Results (My RWLock) ===\n");
    printf("Elapsed time = %.6f seconds\n", elapsed);
    printf("Total ops = %d\n", total_ops);
    printf("Member ops = %d\n", member_count);
    printf("Insert ops = %d\n", insert_count);
    printf("Delete ops = %d\n", delete_count);
    printf("===========================\n");
    
    Free_list();
    my_rwlock_destroy(&rwlock);
    pthread_mutex_destroy(&count_mutex);
    free(thread_handles);
    
    return 0;
}
