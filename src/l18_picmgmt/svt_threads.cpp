// l18_picmgmt - svt_threads.c spans (the platform synchronization wrappers).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/svt_threads.c.
#include "picmgmt.h"

#include <cstdio>
#include <cstring>

#ifndef _WIN32
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif
#endif

namespace l18_picmgmt {

// svt_format_thread_name (svt_threads.c:174-:176).
void formatThreadName(char* buf, std::size_t size, const char* prefix, std::uint32_t index) {
    std::snprintf(buf, size, "%s%u", prefix, index);
}

// ---------------------------------------------------------------------------
// The semaphore quartet (svt_threads.c:312-:400). The __APPLE__ and POSIX
// branches are carried verbatim from the reference; only the _WIN32 branch
// compiles on this platform (exactly as in the reference).
// ---------------------------------------------------------------------------

EbHandle createSemaphore(std::uint32_t initialCount, std::uint32_t maxCount) {
    EbHandle semaphoreHandle;

#if defined(_WIN32)
    semaphoreHandle = (EbHandle)CreateSemaphore(NULL, // default security attributes
                                                initialCount, // initial semaphore count
                                                maxCount, // maximum semaphore count
                                                NULL); // semaphore is not named

#elif defined(__APPLE__)
    (void)maxCount;
    semaphoreHandle = (EbHandle)dispatch_semaphore_create(initialCount);
#else
    (void)maxCount;

    semaphoreHandle = (EbHandle)malloc(sizeof(sem_t));
    if (semaphoreHandle != NULL) {
        sem_init((sem_t*)semaphoreHandle, // semaphore handle
                 0, // shared semaphore (not local)
                 initialCount); // initial count
    }
#endif

    return semaphoreHandle;
}

EbErrorType postSemaphore(EbHandle semaphoreHandle) {
    EbErrorType returnError;

#ifdef _WIN32
    returnError = !ReleaseSemaphore(semaphoreHandle, // semaphore handle
                                    1, // amount to increment the semaphore
                                    NULL) // pointer to previous count (optional)
        ? EB_ErrorSemaphoreUnresponsive
        : EB_ErrorNone;
#elif defined(__APPLE__)
    dispatch_semaphore_signal((dispatch_semaphore_t)semaphoreHandle);
    returnError = EB_ErrorNone;
#else
    returnError = sem_post((sem_t*)semaphoreHandle) ? EB_ErrorSemaphoreUnresponsive : EB_ErrorNone;
#endif

    return returnError;
}

EbErrorType blockOnSemaphore(EbHandle semaphoreHandle) {
    EbErrorType returnError;

#ifdef _WIN32
    returnError = WaitForSingleObject((HANDLE)semaphoreHandle, INFINITE) ? EB_ErrorSemaphoreUnresponsive
                                                                         : EB_ErrorNone;
#elif defined(__APPLE__)
    returnError = dispatch_semaphore_wait((dispatch_semaphore_t)semaphoreHandle, DISPATCH_TIME_FOREVER)
        ? EB_ErrorSemaphoreUnresponsive
        : EB_ErrorNone;
#else
    int ret;
    do {
        ret = sem_wait((sem_t*)semaphoreHandle);
    } while (ret == -1 && errno == EINTR);
    returnError = ret ? EB_ErrorSemaphoreUnresponsive : EB_ErrorNone;
#endif

    return returnError;
}

EbErrorType destroySemaphore(EbHandle semaphoreHandle) {
    EbErrorType returnError;

#ifdef _WIN32
    returnError = !CloseHandle((HANDLE)semaphoreHandle) ? EB_ErrorDestroySemaphoreFailed : EB_ErrorNone;
#elif defined(__APPLE__)
    dispatch_release((dispatch_semaphore_t)semaphoreHandle);
    returnError = EB_ErrorNone;
#else
    returnError = sem_destroy((sem_t*)semaphoreHandle) ? EB_ErrorDestroySemaphoreFailed : EB_ErrorNone;
    free(semaphoreHandle);
#endif

    return returnError;
}

// ---------------------------------------------------------------------------
// The mutex quartet (svt_threads.c:405-:470).
// ---------------------------------------------------------------------------

EbHandle createMutex() {
    EbHandle mutexHandle;

#ifdef _WIN32
    mutexHandle = (EbHandle)CreateMutex(NULL, // default security attributes
                                        false, // false := not initially owned
                                        NULL); // mutex is not named
#else
    mutexHandle = (EbHandle)malloc(sizeof(pthread_mutex_t));

    if (mutexHandle != NULL) {
        pthread_mutex_init((pthread_mutex_t*)mutexHandle,
                           NULL); // default attributes
    }
#endif

    return mutexHandle;
}

EbErrorType releaseMutex(EbHandle mutexHandle) {
    EbErrorType returnError;

#ifdef _WIN32
    returnError = !ReleaseMutex((HANDLE)mutexHandle) ? EB_ErrorMutexUnresponsive : EB_ErrorNone;
#else
    returnError = pthread_mutex_unlock((pthread_mutex_t*)mutexHandle) ? EB_ErrorMutexUnresponsive : EB_ErrorNone;
#endif

    return returnError;
}

EbErrorType blockOnMutex(EbHandle mutexHandle) {
    EbErrorType returnError;

#ifdef _WIN32
    returnError = WaitForSingleObject((HANDLE)mutexHandle, INFINITE) ? EB_ErrorMutexUnresponsive : EB_ErrorNone;
#else
    returnError = pthread_mutex_lock((pthread_mutex_t*)mutexHandle) ? EB_ErrorMutexUnresponsive : EB_ErrorNone;
#endif

    return returnError;
}

EbErrorType destroyMutex(EbHandle mutexHandle) {
    EbErrorType returnError;

#ifdef _WIN32
    returnError = CloseHandle((HANDLE)mutexHandle) ? EB_ErrorDestroyMutexFailed : EB_ErrorNone;
#else
    returnError = pthread_mutex_destroy((pthread_mutex_t*)mutexHandle) ? EB_ErrorDestroyMutexFailed : EB_ErrorNone;
    free(mutexHandle);
#endif

    return returnError;
}

// svt_aom_atomic_set_u32 (svt_threads.c:475-:479).
void atomicSetU32(AtomicVarU32* var, std::uint32_t in) {
    blockOnMutex(var->mutex);
    var->obj = in;
    releaseMutex(var->mutex);
}

// The cond-var trio (svt_threads.c:490-:553).
EbErrorType createCondVar(CondVar* condVar) {
    EbErrorType returnError;
    condVar->val = 0;
#ifdef _WIN32
    InitializeCriticalSection(&condVar->cs);
    InitializeConditionVariable(&condVar->cv);
    returnError = EB_ErrorNone;
#else
    pthread_mutex_init(&condVar->m_mutex, NULL);
    returnError = (EbErrorType)pthread_cond_init(&condVar->m_cond, NULL);
#endif
    return returnError;
}

EbErrorType setCondVar(CondVar* condVar, std::int32_t newVal) {
    EbErrorType returnError;
#ifdef _WIN32
    EnterCriticalSection(&condVar->cs);
    condVar->val = newVal;
    WakeAllConditionVariable(&condVar->cv);
    LeaveCriticalSection(&condVar->cs);
    returnError = EB_ErrorNone;
#else
    returnError = (EbErrorType)pthread_mutex_lock(&condVar->m_mutex);
    condVar->val = newVal;
    returnError = (EbErrorType)(returnError | pthread_cond_broadcast(&condVar->m_cond));
    returnError = (EbErrorType)(returnError | pthread_mutex_unlock(&condVar->m_mutex));
#endif
    return returnError;
}

EbErrorType waitCondVar(CondVar* condVar, std::int32_t input) {
#ifdef _WIN32
    EnterCriticalSection(&condVar->cs);
    while (condVar->val == input) {
        SleepConditionVariableCS(&condVar->cv, &condVar->cs, INFINITE);
    }
    LeaveCriticalSection(&condVar->cs);
#else
    if (pthread_mutex_lock(&condVar->m_mutex)) {
        return EB_ErrorMutexUnresponsive;
    }
    while (condVar->val == input) {
        if (pthread_cond_wait(&condVar->m_cond, &condVar->m_mutex)) {
            (void)pthread_mutex_unlock(&condVar->m_mutex);
            return EB_ErrorMutexUnresponsive;
        }
    }
    if (pthread_mutex_unlock(&condVar->m_mutex)) {
        return EB_ErrorMutexUnresponsive;
    }
#endif
    return EB_ErrorNone;
}

// svt_run_once (svt_threads.c:555-:561).
void runOnce(OnceType* onceControl, OnceFn initRoutine) {
#ifdef _WIN32
    InitOnceExecuteOnce(onceControl, initRoutine, NULL, NULL);
#else
    pthread_once(onceControl, initRoutine);
#endif
}

} // namespace l18_picmgmt
