#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define TAMANHO_TLB 16
#define PAGINAS 1024
#define FRAMES 256
#define MASCARA_PAGINA 1023
#define TAMANHO_PAGINA 1024
#define BITS_DESLOCAMENTO 10
#define MASCARA_DESLOCAMENTO 1023
#define TAMANHO_MEMORIA_LOGICA  PAGINAS * TAMANHO_PAGINA
#define TAMANHO_MEMORIA_FISICA FRAMES * TAMANHO_PAGINA
#define TAMANHO_BUFFER 10

//==================== Funções, Variáveis Globais, Structs ====================

int proximoQuadroFIFO = 0;

int max(int a, int b)
{
    if (a > b) return a;  return b;
}

// Estrutura de dados FILA
struct no
{
    int dado;
    int idade;
    struct no *proximo;
};

typedef struct no no_t;

no_t *cabecaFila;
int tamanhoFila = 0;

void filaInicializa()
{
    // Inicia a fila
    cabecaFila = malloc(sizeof(no_t));
    cabecaFila->idade = 0;
    cabecaFila->proximo = NULL;
}

int filaRemove()
{
    // Remove a fila
    no_t *atual = cabecaFila;
    no_t *minPrev = cabecaFila;

    int idadeMin = -1;

    while(atual->proximo != NULL)
    {
        if(idadeMin < atual->proximo->idade)
        {
            idadeMin = atual->proximo->idade;
            minPrev = atual;
        }
        atual = atual->proximo;
    }
    no_t *temp = minPrev->proximo->proximo;
    int valor = minPrev->proximo->dado;
    free(minPrev->proximo);
    minPrev->proximo = temp;
    tamanhoFila--;

    return valor;
}

void filaIncrementaIdade()
{
    no_t *atual = cabecaFila;

    while(atual->proximo != NULL)
    {
        atual->proximo->idade++;
        atual = atual->proximo;
    }
}

int filaAdiciona(int i)
{
    // adiciona elemento a fila
    filaIncrementaIdade();
    no_t *atual = cabecaFila;

    while(atual->proximo != NULL)
    {
        atual = atual->proximo;

        if(atual->dado == i)
        {
            atual->idade = 0;

            return 0;
        }
    }
    no_t *novoNo = malloc(sizeof(no_t));
    novoNo->idade = 0;
    novoNo->dado = i;
    novoNo->proximo = NULL;
    atual->proximo = novoNo;
    tamanhoFila++;

    if(tamanhoFila > FRAMES)
    {
        return filaRemove();
    }

    return 0;
}

struct entradaTLB
{
    int logica;
    int fisica;
};

int modoPrograma = 0;
struct entradaTLB tlb[TAMANHO_TLB];
int indiceTLB = 0;
int tabelaPaginas[PAGINAS];
unsigned char memoriaPrincipal[TAMANHO_MEMORIA_FISICA];
unsigned char *suporte;


int buscaTLB(int paginaLogica)
{
    for(int i = 0; i < TAMANHO_TLB; i++)
    {
        if(tlb[i].logica == paginaLogica)
        {
            return tlb[i].fisica;
        }
    }

    return -1;
}


void adicionaTLB(int pagina, int quadro)
{
    for(int i = 0; i < TAMANHO_TLB; i++)
    {
        if(tlb[i].fisica == quadro)
        {
            tlb[i].logica = pagina;
            return;
        }
    }

    tlb[indiceTLB % TAMANHO_TLB].logica = pagina;
    tlb[indiceTLB % TAMANHO_TLB].fisica = quadro;
    indiceTLB++;
}

int substituicaoFIFO()
{
    int valorRetorno = -1;

    for(int i =0;i<PAGINAS; i++)
    {
        if(tabelaPaginas[i] == proximoQuadroFIFO)
        {
            valorRetorno = i;

            break;
        }
    }

    return valorRetorno;
}

int substituicaoLRU()
{
    return filaRemove();
}

int substituicao(int pagina)
{
    int paginaAntiga = 0;

    if(modoPrograma)
    {
        paginaAntiga = substituicaoLRU();
    }
    else
    {
        paginaAntiga = substituicaoFIFO();
    }

    int quadro =  tabelaPaginas[paginaAntiga];
    tabelaPaginas[paginaAntiga] = -1;

    return quadro;
}

void apresentacao()
{
    printf("\t\t========== Virtual Manager ==========\n\n");
}

// ==================== MAIN ====================
int main(int argc, const char *argv[])
{
    apresentacao();

    if (argc !=3)
    {
        fprintf(stderr, "Uso ./virtmem entrada backingstore 0/1\n");
        exit(1);
    }

    printf("Deseja executar o programa como:\n [0] LRU\n [1] FIFO\n" );
    scanf("%d", &modoPrograma);
    filaInicializa();

    const char *nomeArquivoBacking = argv[2];
    int descritorBacking = open(nomeArquivoBacking, O_RDONLY);
    suporte = mmap(0, TAMANHO_MEMORIA_LOGICA, PROT_READ, MAP_PRIVATE, descritorBacking, 0);

    const char *nomeArquivoEntrada = argv[1];
    FILE *arquivoEntrada = fopen(nomeArquivoEntrada, "r");

    int i;
    for (i = 0; i < PAGINAS; i++)
    {
        tabelaPaginas[i] = -1;
    }

    char buffer[TAMANHO_BUFFER];

    int totalEnderecos = 0;
    int acertosTLB = 0;
    int faltasPagina = 0;

    int numQuadrosLivres = FRAMES;

    signed char *localTransferenciaMemoriaPrincipal = 0;
    signed char *localDadosBackingStore = 0;

    while (fgets(buffer, TAMANHO_BUFFER, arquivoEntrada) != NULL)
    {
        totalEnderecos++;
        int enderecoLogico = atoi(buffer);

        int deslocamento = enderecoLogico & MASCARA_DESLOCAMENTO;
        int pagina = (enderecoLogico >> BITS_DESLOCAMENTO) & MASCARA_PAGINA;
        int quadro = buscaTLB(pagina);

        if (quadro != -1)
        {
            acertosTLB++;
        }
        else
        {
            quadro = tabelaPaginas[pagina];
            if (quadro == -1)
            {
                faltasPagina++;

                if(numQuadrosLivres > 0)
                {
                    quadro = FRAMES - numQuadrosLivres;
                    numQuadrosLivres--;
                }
                else
                {
                    quadro = substituicao(pagina);
                }
                proximoQuadroFIFO = (proximoQuadroFIFO+1)%FRAMES;
                localTransferenciaMemoriaPrincipal = memoriaPrincipal + (quadro * TAMANHO_PAGINA);
                localDadosBackingStore = suporte + (pagina * TAMANHO_PAGINA);
                memcpy(localTransferenciaMemoriaPrincipal, localDadosBackingStore, TAMANHO_PAGINA);
                tabelaPaginas[pagina] = quadro;
            }
            adicionaTLB(pagina, quadro);
        }

        if(modoPrograma)
        {
            filaAdiciona(pagina);
        }

        int enderecoFisico = (quadro << BITS_DESLOCAMENTO) | deslocamento;
        unsigned char valor = memoriaPrincipal[quadro * TAMANHO_PAGINA + deslocamento];

        printf("Memoria Virtual: %d Memoria Fisica: %d Valor: %d\n", enderecoLogico, enderecoFisico, valor);
    }
    printf("=====================================\n");
    printf("Número de Endereços Traduzidos = %d\n", totalEnderecos);
    printf("Faltas de Página = %d\n", faltasPagina);
    printf("Taxa de Faltas de Página = %.3f\n", faltasPagina / (1. * totalEnderecos));
    printf("Acertos TLB = %d\n", acertosTLB);
    printf("Taxa de Acertos TLB = %.3f\n", acertosTLB / (1. * totalEnderecos));

    return 0;
}
