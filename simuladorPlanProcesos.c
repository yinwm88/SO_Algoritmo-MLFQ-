#include <stdio.h>
#include <stdlib.h>

#define MAX_PROCESOS 200   // por si quieren simular mas de 100

typedef struct {
    int id;               // ID del proceso
    int arrival;                // Tiempo de llegada (t0, t1... tn)
    int burst;                  // Ráfaga de CPU original (tiempo cpu)
    int priority;               // Tiempo restante (para RR, SRTN)
    int remaining;        // Para algoritmos con prioridades
    int start_time;        // Primer instante en que se ejecuta
    int finish_time;        // Momento en que termina
    int waiting_time;        // Tiempo total de espera
    int turnaround;        // finish_time - arrival
    int response_time;          // primer instante en que el proceso recibe CPU − arrival
} Process;

// PARÁMETROS MLFQ
 
#define MLFQ_NUM_COLAS      3      // Q2 (alta), Q1 (media), Q0 (baja)
#define MLFQ_QUANTUM_Q2     2
#define MLFQ_QUANTUM_Q1     4
#define MLFQ_QUANTUM_Q0     8
#define MLFQ_BOOST_INTERVAL 20     // Regla 5: cada S unidades de tiempo

#define MLFQ_CUOTA_Q2       2      // Regla 4 corregida: cuota acumulativa
#define MLFQ_CUOTA_Q1       4
#define MLFQ_CUOTA_Q0       8

// ESTRUCTURAS AUXILIARES
typedef struct {
    int items[MAX_PROCESOS];
    int head;
    int tail;
    int count;
} MLFQ_Queue;

// Arreglo paralelo con metadatos de cada proceso para MLFQ
typedef struct {
    int queue_level;         // 2 = alta, 1 = media, 0 = baja
    int cpu_used_in_level;   // Tiempo acumulado en el nivel actual
} MLFQ_Meta;

static void mlfq_queue_init(MLFQ_Queue *q) {
    q->head = q->tail = q->count = 0;
}

static int mlfq_queue_empty(const MLFQ_Queue *q) {
    return q->count == 0;
}

static void mlfq_queue_push(MLFQ_Queue *q, int idx) {
    q->items[q->tail] = idx;
    q->tail = (q->tail + 1) % MAX_PROCESOS;
    q->count++;
}

static int mlfq_queue_pop(MLFQ_Queue *q) {
    int v = q->items[q->head];
    q->head = (q->head + 1) % MAX_PROCESOS;
    q->count--;
    return v;
}

static int mlfq_quantum_for_level(int level) {
    switch (level) {
        case 2: return MLFQ_QUANTUM_Q2;
        case 1: return MLFQ_QUANTUM_Q1;
        default: return MLFQ_QUANTUM_Q0;
    }
}

static int mlfq_cuota_for_level(int level) {
    switch (level) {
        case 2: return MLFQ_CUOTA_Q2;
        case 1: return MLFQ_CUOTA_Q1;
        default: return MLFQ_CUOTA_Q0;
    }
}

/*
 *  SIMULACIÓN MLFQ
 *  Basada en Silberschatz, "Operating System Concepts" 10th ed.
 *  Reglas:
 *   1) Mayor prioridad ejecuta primero
 *   2) Igual prioridad => Round Robin
 *   3) Proceso nuevo entra a la cola más alta (Q2)
 *   4) Si agota su cuota acumulativa en el nivel, baja un nivel
 *   5) Cada S unidades de tiempo, todos vuelven a Q2
 */
void simulate_mlfq(Process *p, int n) {
    MLFQ_Queue colas[MLFQ_NUM_COLAS];
    for (int i = 0; i < MLFQ_NUM_COLAS; i++) mlfq_queue_init(&colas[i]);

    MLFQ_Meta *meta = (MLFQ_Meta *)malloc(n * sizeof(MLFQ_Meta));
    int *admitido  = (int *)calloc(n, sizeof(int));
    for (int i = 0; i < n; i++) {
        meta[i].queue_level       = 2;
        meta[i].cpu_used_in_level = 0;
    }

    int completados  = 0;
    int current_time = 0;
    int next_boost   = MLFQ_BOOST_INTERVAL;

    printf("\n===== SIMULACION MLFQ =====\n");
    printf("Colas: Q2(quantum=%d) > Q1(quantum=%d) > Q0(quantum=%d)\n",
           MLFQ_QUANTUM_Q2, MLFQ_QUANTUM_Q1, MLFQ_QUANTUM_Q0);
    printf("Boost (Regla 5) cada S=%d u.t.\n\n", MLFQ_BOOST_INTERVAL);

    while (completados < n) {

        /* --- Regla 5: boost periódico --- */
        if (current_time >= next_boost) {
            printf(">>> BOOST en t=%d: procesos listos vuelven a Q2 <<<\n",
                   current_time);
            for (int lvl = 0; lvl < MLFQ_NUM_COLAS - 1; lvl++) {
                while (!mlfq_queue_empty(&colas[lvl])) {
                    int idx = mlfq_queue_pop(&colas[lvl]);
                    meta[idx].queue_level       = 2;
                    meta[idx].cpu_used_in_level = 0;
                    mlfq_queue_push(&colas[2], idx);
                }
            }
            /* Reiniciar contabilidad de los que ya estaban en Q2 */
            for (int k = 0; k < colas[2].count; k++) {
                int idx = colas[2].items[(colas[2].head + k) % MAX_PROCESOS];
                meta[idx].cpu_used_in_level = 0;
            }
            next_boost += MLFQ_BOOST_INTERVAL;
        }

        /* --- Admitir procesos que ya llegaron --- */
        for (int i = 0; i < n; i++) {
            if (!admitido[i] && p[i].arrival <= current_time) {
                admitido[i]                 = 1;
                meta[i].queue_level         = 2;   /* Regla 3 */
                meta[i].cpu_used_in_level   = 0;
                mlfq_queue_push(&colas[2], i);
            }
        }

        /* --- Seleccionar la cola de mayor prioridad no vacía (Regla 1) --- */
        int lvl = -1;
        for (int k = MLFQ_NUM_COLAS - 1; k >= 0; k--) {
            if (!mlfq_queue_empty(&colas[k])) { lvl = k; break; }
        }

        if (lvl == -1) {
            current_time++;
            continue;
        }

        /* --- Extraer proceso y ejecutarlo (Regla 2 dentro de la cola) --- */
        int idx = mlfq_queue_pop(&colas[lvl]);
        Process *pr = &p[idx];

        int quantum = mlfq_quantum_for_level(lvl);
        int cuota   = mlfq_cuota_for_level(lvl);
        int margen  = cuota - meta[idx].cpu_used_in_level;

        int exec = pr->remaining;
        if (exec > quantum) exec = quantum;
        if (exec > margen)  exec = margen;
        if (exec <= 0)      exec = 1;

        int t_ini = current_time;
        current_time            += exec;
        pr->remaining           -= exec;
        meta[idx].cpu_used_in_level += exec;

        if (pr->start_time == -1) {
            pr->start_time    = t_ini;
            pr->response_time = t_ini - pr->arrival;
        }

        printf("[t=%3d -> %3d] P%-3d ejecuta en Q%d (%d u.t.)\n",
               t_ini, current_time, pr->id, lvl, exec);

        /* --- ¿Terminó? --- */
        if (pr->remaining == 0) {
            pr->finish_time  = current_time;
            pr->turnaround   = pr->finish_time - pr->arrival;
            pr->waiting_time = pr->turnaround - pr->burst;
            completados++;
            continue;
        }

        /* --- Regla 4 corregida: contabilidad acumulativa --- */
        if (meta[idx].cpu_used_in_level >= cuota && lvl > 0) {
            meta[idx].queue_level       = lvl - 1;
            meta[idx].cpu_used_in_level = 0;
            mlfq_queue_push(&colas[lvl - 1], idx);
        } else {
            mlfq_queue_push(&colas[lvl], idx);
        }
    }

    /* --- Impresión de métricas --- */
    double sum_w = 0, sum_t = 0, sum_r = 0;
    printf("\n===== METRICAS POR PROCESO (MLFQ) =====\n");
    printf("%-5s %-8s %-7s %-8s %-10s %-10s %-10s\n",
           "ID", "Llegada", "Burst", "Fin", "Retorno", "Espera", "Respuesta");
    for (int i = 0; i < n; i++) {
        printf("%-5d %-8d %-7d %-8d %-10d %-10d %-10d\n",
               p[i].id, p[i].arrival, p[i].burst,
               p[i].finish_time, p[i].turnaround,
               p[i].waiting_time, p[i].response_time);
        sum_w += p[i].waiting_time;
        sum_t += p[i].turnaround;
        sum_r += p[i].response_time;
    }
    printf("\n===== PROMEDIOS (MLFQ) =====\n");
    printf("Tiempo de espera promedio    : %.2f\n", sum_w / n);
    printf("Tiempo de retorno promedio   : %.2f\n", sum_t / n);
    printf("Tiempo de respuesta promedio : %.2f\n", sum_r / n);

    free(meta);
    free(admitido);
}



int main(void) {
    Process procesos[MAX_PROCESOS];
    int n = 0;

    // Lee desde stdin hasta EOF: cada línea debe tener id arrival burst priority
    while (scanf("%d %d %d %d",
                 &procesos[n].id,
                 &procesos[n].arrival,
                 &procesos[n].burst,
                 &procesos[n].priority) == 4) {

        procesos[n].remaining    = procesos[n].burst;
        procesos[n].start_time   = -1;
        procesos[n].finish_time  = 0;
        procesos[n].waiting_time = 0;
        procesos[n].turnaround   = 0;
        procesos[n].response_time= -1;
        n++;

        if (n >= MAX_PROCESOS) {
            fprintf(stderr, "Se alcanzó el límite MAX_PROCESOS=%d\n", MAX_PROCESOS);
            break;
        }
    }

    printf("Se leyeron %d procesos:\n", n);
    for (int i = 0; i < n; i++) {
        printf("P%3d: arrival=%3d burst=%2d priority=%d\n",
               procesos[i].id,
               procesos[i].arrival,
               procesos[i].burst,
               procesos[i].priority);
    }

    // Aquí ya puedes llamar a tus funciones de simulación:
    //void simulate_fcfs(Process *p, int n);
    //void simulate_rr(Process *p, int n, int quantum);
    //void simulate_sjf(Process *p, int n);
    //void simulate_srtn(Process *p, int n);
    //void simulate_priority(Process *p, int n);
    simulate_mlfq(procesos, n);
    
    return 0;
}
