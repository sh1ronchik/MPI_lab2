/* my_rwlock.c
 * Реализация read-write блокировок
*/

#include "my_rwlock.h"
#include <stdlib.h>

/* Инициализация read-write блокировки */
int my_rwlock_init(my_rwlock_t *rwlock) {
    if (!rwlock) return -1;
    
    pthread_mutex_init(&rwlock->mutex, NULL);
    pthread_cond_init(&rwlock->read_cond, NULL);
    pthread_cond_init(&rwlock->write_cond, NULL);
    
    rwlock->active_readers = 0;
    rwlock->waiting_readers = 0;
    rwlock->waiting_writers = 0;
    rwlock->writer_active = 0;
    
    return 0;
}

/* Уничтожение read-write блокировки */
int my_rwlock_destroy(my_rwlock_t *rwlock) {
    if (!rwlock) return -1;
    
    pthread_mutex_destroy(&rwlock->mutex);
    pthread_cond_destroy(&rwlock->read_cond);
    pthread_cond_destroy(&rwlock->write_cond);
    
    return 0;
}

/* Получение блокировки на чтение */
int my_rwlock_rdlock(my_rwlock_t *rwlock) {
    if (!rwlock) return -1;
    
    pthread_mutex_lock(&rwlock->mutex);
    
    /* Ждем, пока не будет активного писателя и ожидающих писателей */
    /* (отдаем приоритет писателям) */
    rwlock->waiting_readers++;
    while (rwlock->writer_active || rwlock->waiting_writers > 0) {
        pthread_cond_wait(&rwlock->read_cond, &rwlock->mutex);
    }
    rwlock->waiting_readers--;
    
    /* Получаем блокировку на чтение */
    rwlock->active_readers++;
    
    pthread_mutex_unlock(&rwlock->mutex);
    
    return 0;
}

/* Получение блокировки на запись */
int my_rwlock_wrlock(my_rwlock_t *rwlock) {
    if (!rwlock) return -1;
    
    pthread_mutex_lock(&rwlock->mutex);
    
    /* Ждем, пока не будет активных читателей и писателя */
    rwlock->waiting_writers++;
    while (rwlock->active_readers > 0 || rwlock->writer_active) {
        pthread_cond_wait(&rwlock->write_cond, &rwlock->mutex);
    }
    rwlock->waiting_writers--;
    
    /* Получаем блокировку на запись */
    rwlock->writer_active = 1;
    
    pthread_mutex_unlock(&rwlock->mutex);
    
    return 0;
}

/* Освобождение блокировки */
int my_rwlock_unlock(my_rwlock_t *rwlock) {
    if (!rwlock) return -1;
    
    pthread_mutex_lock(&rwlock->mutex);
    
    if (rwlock->writer_active) {
        /* Писатель освобождает блокировку */
        rwlock->writer_active = 0;
        
        /* Приоритет писателям: если есть ожидающие писатели, будим их */
        if (rwlock->waiting_writers > 0) {
            pthread_cond_signal(&rwlock->write_cond);
        } else {
            /* Иначе будим всех читателей */
            pthread_cond_broadcast(&rwlock->read_cond);
        }
    } else if (rwlock->active_readers > 0) {
        /* Читатель освобождает блокировку */
        rwlock->active_readers--;
        
        /* Если это был последний читатель, будим писателя */
        if (rwlock->active_readers == 0 && rwlock->waiting_writers > 0) {
            pthread_cond_signal(&rwlock->write_cond);
        }
    }
    
    pthread_mutex_unlock(&rwlock->mutex);
    
    return 0;
}
