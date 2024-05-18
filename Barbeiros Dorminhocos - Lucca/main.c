#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define NUM_CADEIRAS 7
#define NUM_BARBEIROS 2

pthread_mutex_t mutex;
sem_t sem_cadeiras;
sem_t sem_barbeiros[NUM_BARBEIROS];

int fila_clientes[NUM_CADEIRAS]; //aqui seria as "cadeiras da fila de espera"
int frente_fila = 0; //indice da frente da fila
int fundo_fila = 0; //indice do final da fila
int proximo_cliente_id = 0; //id do proximo cliente

pthread_cond_t cond_barbeiros = PTHREAD_COND_INITIALIZER;

void enfileirar_cliente(int cliente_id)
{
    fila_clientes[fundo_fila] = cliente_id;
    fundo_fila = (fundo_fila + 1) % NUM_CADEIRAS; //circular
}


int desenfileirar_cliente()
{
    int cliente_id = fila_clientes[frente_fila];
    frente_fila = (frente_fila + 1) % NUM_CADEIRAS; //circular
    return cliente_id;
}


int tamanho_fila()
{
    return (fundo_fila - frente_fila + NUM_CADEIRAS) % NUM_CADEIRAS;
}


void *cliente(void *id)
{
    int cliente_id = *(int *)id;

    pthread_mutex_lock(&mutex);
    if (tamanho_fila() == NUM_CADEIRAS - 1)
    {
        //se as cadeiras da fila de espera estiverem ocupadas o cliente vai embora
        printf("O cliente %d não conseguiu se sentar e foi embora.\n", cliente_id);

        pthread_mutex_unlock(&mutex);
        pthread_exit(NULL);
    }
    else
    {
        //existe uma vaga nas cadeiras e o cliente conseguiu se sentar
        enfileirar_cliente(cliente_id);

        printf("Cliente %d entrou na barbearia e se sentou em uma cadeira.\n", cliente_id);

        sem_post(&sem_cadeiras); // Libera uma cadeira
        pthread_cond_signal(&cond_barbeiros); // Sinaliza que há um novo cliente na fila
    }
    pthread_mutex_unlock(&mutex);

    pthread_exit(NULL);
}


void *barbeiro(void *id)
{
    int barbeiro_id = *(int *)id;

    while (1)
    {
        pthread_mutex_lock(&mutex);

        while (frente_fila == fundo_fila)
        {
            //se nao existe (mais) nenhum cliente para ser atendido o barbeiro dorme
            printf("Barbeiro %d está dormindo.\n", barbeiro_id);

            pthread_cond_wait(&cond_barbeiros, &mutex);
        }

        int cliente_id = desenfileirar_cliente();
        pthread_mutex_unlock(&mutex);

        printf("Barbeiro %d está cortando o cabelo do cliente %d.\n", barbeiro_id, cliente_id);
        sleep(3); //esse sleep 3 e uma simulaçao do tempo do barbeiro cortando o cabelo

        sem_wait(&sem_cadeiras); //libera uma cadeira apos o corte do cabelo
    }
}

int main()
{
    pthread_t threads[NUM_BARBEIROS + NUM_CADEIRAS];
    int ids[NUM_BARBEIROS + NUM_CADEIRAS];

    pthread_mutex_init(&mutex, NULL);
    sem_init(&sem_cadeiras, 0, NUM_CADEIRAS);

    for (int i = 0; i < NUM_BARBEIROS; i++)
    {
        ids[i] = i;
        sem_init(&sem_barbeiros[i], 0, 1);
        pthread_create(&threads[i], NULL, barbeiro, &ids[i]);
    }

    //loop dos clientes
    while (1)
    {
        pthread_mutex_lock(&mutex);

        int current_client_id = proximo_cliente_id++;

        pthread_mutex_unlock(&mutex);
        pthread_create(&threads[NUM_BARBEIROS + current_client_id], NULL, cliente, &current_client_id);

        usleep(500000); //espera meio segundo para criar um novo cliente
    }

}