/* my_rwlock.h
 * Заголовочный файл для реализации read-write блокировок
 */

#ifndef MY_RWLOCK_H
#define MY_RWLOCK_H

#include <pthread.h>

/* Структура для read-write блокировки */
typedef struct {
    pthread_mutex_t mutex;              /* Защищает структуру rwlock */
    pthread_cond_t read_cond;           /* Условная переменная для читателей */
    pthread_cond_t write_cond;          /* Условная переменная для писателей */
    int active_readers;                 /* Количество активных читателей */
    int waiting_readers;                /* Количество ожидающих читателей */
    int waiting_writers;                /* Количество ожидающих писателей */
    int writer_active;                  /* Флаг: есть ли активный писатель (0 или 1) */
} my_rwlock_t;

/* Функции для работы с блокировками */
int my_rwlock_init(my_rwlock_t *rwlock);
int my_rwlock_destroy(my_rwlock_t *rwlock);
int my_rwlock_rdlock(my_rwlock_t *rwlock);
int my_rwlock_wrlock(my_rwlock_t *rwlock);
int my_rwlock_unlock(my_rwlock_t *rwlock);

#endif /* MY_RWLOCK_H */
