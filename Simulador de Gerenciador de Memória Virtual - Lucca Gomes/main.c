/*
  Projeto: Gerenciador de Memória Virtual
  256 páginas no espaço de endereço virtual.
  256 frames no espaço de memória física.
  BACKING_STORE.bin simula um disco rígido.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

// ==================== Definições Globais ====================

#define TAMANHO_PAGINA 256           // Tamanho da página, em bytes.
#define ENTRADAS_PAGINA 256         // Máximo de entradas na tabela de páginas.
#define BITS_NUM_PAGINA 8         // Tamanho do número da página, em bits.
#define TAMANHO_FRAME 256          // Tamanho do frame, em bytes.
#define ENTRADAS_FRAME 256       // Número de frames na memória física.
#define TAMANHO_MEMORIA (TAMANHO_FRAME * ENTRADAS_FRAME) // Tamanho da memória, em bytes.
#define TLB_ENTRADAS 16          // Máximo de entradas na TLB.

// ==================== Variáveis Globais ====================

int endereco_virtual;        // Endereço virtual.
int numero_pagina;    // Número da página.
int deslocamento;         // Deslocamento.
int endereco_fisico;       // Endereço físico.
int numero_frame;   // Número do frame.
int valor;
int tabela_paginas[ENTRADAS_PAGINA]; // Tabela de páginas.
int tlb[TLB_ENTRADAS][2]; // Buffer de busca antecipada de tradução.
int tlb_inicio = -1; // Índice inicial da TLB, estrutura de dados de fila.
int tlb_fim = -1;  // Índice final da TLB, estrutura de dados de fila.
char memoria[TAMANHO_MEMORIA]; // Memória física. Cada char é 1 byte.
int indice_memoria = 0;  // Aponta para o início do primeiro frame vazio.

// ==================== Variáveis de Estatísticas ====================

int contador_page_fault = 0;   // Conta as faltas de página.
int contador_tlb = 0;     // Contador de acertos da TLB.
int contador_endereco = 0; // Conta endereços lidos do arquivo.
float taxa_page_fault;        // Taxa de falta de página.
float taxa_tlb;          // Taxa de acerto da TLB.

// ==================== Declarações de Funções ====================

int obter_deslocamento(int endereco_virtual);
int obter_numero_pagina(int endereco_virtual);
void inicializar_tabela_paginas(int n);
void inicializar_tlb(int n);
int consultar_tabela_paginas(int numero_pagina);
int consultar_tlb(int numero_pagina);
void atualizar_tlb(int numero_pagina, int numero_frame);


//==================== Funções ====================

/* Calcula e retorna o número da página. */
int obter_numero_pagina(int endereco_virtual)
{
    // Desloca o endereço virtual para a direita por n bits.
    return (endereco_virtual >> BITS_NUM_PAGINA);
}


/* Calcula e retorna o valor do deslocamento. */
int obter_deslocamento(int endereco_virtual)
{
    /*
    A máscara é a representação decimal de 8 dígitos binários 1.
    É na verdade 2^8-1.
    */
    int mascara = 255;

    return endereco_virtual & mascara;
}


/* Define todos os elementos da tabela_paginas como o número inteiro n. */
void inicializar_tabela_paginas(int n)
{
    for (int i = 0; i < ENTRADAS_PAGINA; i++)
    {
        tabela_paginas[i] = n;
    }
}


/* Define todos os elementos da TLB como o número inteiro n. */
void inicializar_tlb(int n)
{
    for (int i = 0; i < TLB_ENTRADAS; i++)
    {
        tlb[i][0] = -1;
        tlb[i][1] = -1;
    }
}


/* Leva um número_pagina e verifica se há um número_frame correspondente. */
int consultar_tabela_paginas(int numero_pagina)
{
    if (tabela_paginas[numero_pagina] == -1)
    {
        contador_page_fault++;
    }

    return tabela_paginas[numero_pagina];
}


/* Leva um número_pagina e verifica se há um número_frame correspondente. */
int consultar_tlb(int numero_pagina)
{
    // Se o número_pagina for encontrado, retorne o número_frame correspondente.
    for (int i = 0; i < TLB_ENTRADAS; i++)
    {
        if (tlb[i][0] == numero_pagina)
        {
            // Acerto na TLB!
            contador_tlb++;

            return tlb[i][1];
        }
    }

    /* Se o número_pagina não existir na TLB, retorne -1. */
    /* TLB miss! */
    return -1;
}


void atualizar_tlb(int numero_pagina, int numero_frame)
{
    // Usa a política FIFO.
    if (tlb_inicio == -1)
    {
        // Define os índices iniciais e finais.
        tlb_inicio = 0;
        tlb_fim = 0;

        // Atualiza a TLB.
        tlb[tlb_fim][0] = numero_pagina;
        tlb[tlb_fim][1] = numero_frame;
    }
    else
    {
        // Usa uma matriz circular para implementar uma fila.
        tlb_inicio = (tlb_inicio + 1) % TLB_ENTRADAS;
        tlb_fim = (tlb_fim + 1) % TLB_ENTRADAS;

        // Insere nova entrada da TLB no final.
        tlb[tlb_fim][0] = numero_pagina;
        tlb[tlb_fim][1] = numero_frame;
    }

    return;
}

// ==================== MAIN ====================

int main(int argc, char *argv[])
{

    /* Variáveis de E/S de arquivo. */
    char* arquivo_entrada;      // Nome do arquivo de endereços.
    char* arquivo_armazenamento;   // Nome do arquivo de armazenamento.
    char* dados_armazenamento;   // Dados do arquivo de armazenamento.
    int fd_armazenamento;       // Descritor de arquivo de armazenamento.
    char linha[8];       // String temporária para armazenar cada linha no arquivo de entrada.
    FILE* ponteiro_entrada;       // Ponteiro do arquivo de endereços.
    FILE* ponteiro_saida;      // Ponteiro do arquivo de saída.

    // Inicializa tabela_paginas, define todos os elementos como -1.
    inicializar_tabela_paginas(-1);
    inicializar_tlb(-1);

    // Obtém argumentos da linha de comando.
    if (argc != 3)
    {
        printf("Informe os nomes dos arquivos de entrada, saída e armazenamento!");

        exit(EXIT_FAILURE);
    }
    else // Caso contrário, prossiga com a execução.
    {
        // Obtém os nomes dos arquivos de argv[].
        arquivo_entrada = argv[1];
        arquivo_armazenamento = argv[2];

        // Abre o arquivo de endereços.
        if ((ponteiro_entrada = fopen(arquivo_entrada, "r")) == NULL)
        {
            // Se fopen falhar, imprime erro e sai.
            printf("Não foi possível abrir o arquivo de entrada.\n");

            exit(EXIT_FAILURE);
        }


        /*
         Abre o arquivo de armazenamento.
         Mapeia o arquivo de armazenamento para a memória.
         Inicializa o descritor de arquivo.
        */
        fd_armazenamento = open(arquivo_armazenamento, O_RDONLY);
        dados_armazenamento = mmap(0, TAMANHO_MEMORIA, PROT_READ, MAP_SHARED, fd_armazenamento, 0);

        // Verifica se a chamada mmap teve sucesso.
        if (dados_armazenamento == MAP_FAILED)
        {
            close(fd_armazenamento);
            printf("Erro ao mapear o arquivo de armazenamento de fundo!");
            exit(EXIT_FAILURE);
        }

        // Loop através do arquivo de entrada uma linha de cada vez.
        while (fgets(linha, sizeof(linha), ponteiro_entrada))
        {
            // Lê um único endereço do arquivo, atribui a virtual.
            endereco_virtual = atoi(linha);
            // Incrementa o contador de endereços.
            contador_endereco++;

            // Obtém o número_pagina do endereço virtual.
            numero_pagina = obter_numero_pagina(endereco_virtual);
            // Obtém o deslocamento do endereço virtual.
            deslocamento = obter_deslocamento(endereco_virtual);

            // Usa o número_pagina para encontrar o número_frame na TLB, se existir.
            numero_frame = consultar_tlb(numero_pagina);

            // Verifica o número_frame retornado pela função consultar_tlb.
            if (numero_frame != -1)
            {
                /*
                Busca da TLB bem-sucedida.
                Nenhuma atualização na TLB necessária.
                */
                endereco_fisico = numero_frame + deslocamento;

                /*
                Nenhum acesso ao arquivo de armazenamento é necessário
                Obtenha o valor diretamente da memória.
                */
                valor = memoria[endereco_fisico];
            }
            else
            {
                /*
                Busca da TLB falhou.
                Procure o número_frame na tabela de páginas em vez disso.
                */
                numero_frame = consultar_tabela_paginas(numero_pagina);

                // Verifica o número_frame retornado pela consultar_tabela_paginas.
                if (numero_frame != -1)
                {
                    // Sem falta de página.
                    endereco_fisico = numero_frame + deslocamento;

                    // Atualiza a TLB.
                    atualizar_tlb(numero_pagina, numero_frame);

                    /*
                    Nenhum acesso ao arquivo de armazenamento é necessário
                    Obtenha o valor diretamente da memória.
                     */
                    valor = memoria[endereco_fisico];
                }
                else
                {
                    /*
                    Falha de página!
                    Quando ocorre uma falta de página, você lerá uma página de 256 bytes
                    do arquivo BACKING_STORE.bin e a armazenará em um frame disponível na memória física.
                    */

                    // Procure o início da página no arquivo ptr_store.
                    int endereco_pagina = numero_pagina * TAMANHO_PAGINA;

                    // Verifique se um frame livre existe.
                    if (indice_memoria != -1)
                    {
                        /*
                        Sucesso, existe um frame livre.
                        Armazene a página do arquivo de armazenamento na memória no frame.
                        */
                        memcpy(memoria + indice_memoria, dados_armazenamento + endereco_pagina, TAMANHO_PAGINA);

                        // Calcule o endereço físico de um byte específico.
                        numero_frame = indice_memoria;
                        endereco_fisico = numero_frame + deslocamento;
                        // Obtenha o valor.
                        valor = memoria[endereco_fisico];

                        // Atualize a tabela_paginas com o número_frame correto.
                        tabela_paginas[numero_pagina] = indice_memoria;
                        // Atualize a TLB.
                        atualizar_tlb(numero_pagina, numero_frame);

                        // Incremente indice_memoria.
                        if (indice_memoria < TAMANHO_MEMORIA - TAMANHO_FRAME)
                        {
                            indice_memoria += TAMANHO_FRAME;
                        }
                        else
                        {
                            // Defina indice_memoria como -1, indicando que a memória está cheia.
                            indice_memoria = -1;
                        }
                    }
                    else
                    {
                        /*
                        Falhou, nenhum frame livre na memória existe.
                        Troque!
                        */
                    }
                }
            }

            // Anexe os resultados ao arquivo de saída.
            printf("Endereço virtual: %d ", endereco_virtual);
            printf("Endereço físico: %d ", endereco_fisico);
            printf("Valor: %d\n", valor);
        }

        // Calcule as taxas.
        taxa_page_fault = (float) contador_page_fault / (float) contador_endereco;
        taxa_tlb = (float) contador_tlb / (float) contador_endereco;

        // Imprima as estatísticas no final do arquivo de saída.
        printf("Número de Endereços Traduzidos = %d\n", contador_endereco);
        printf("Page Faults = %d\n", contador_page_fault);
        printf("Taxa de Page Fault = %.3f\n", taxa_page_fault);
        printf("TLB Hits = %d\n", contador_tlb);
        printf("Taxa de TLB Hit = %.3f\n", taxa_tlb);

        // Feche os três arquivos.
        fclose(ponteiro_entrada);
        fclose(ponteiro_saida);
        close(fd_armazenamento);
    }

    return EXIT_SUCCESS;
}
