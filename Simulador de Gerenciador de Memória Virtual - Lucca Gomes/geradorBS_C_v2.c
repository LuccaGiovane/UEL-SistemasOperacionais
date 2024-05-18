#include <stdio.h>
#include <stdlib.h>

// ==================== Declarações de Funções ====================

void CriarArquivo();
void ConsultarArquivo();

//==================== Funções ====================

void CriarArquivo()
{
    FILE * arquivo;

    if ((arquivo = fopen("teste.dat","wb")) == NULL)
    {
        printf("Erro! Falha ao abrir o arquivo");
        exit(1);
    }

    int i;
    int num_inteiro;
    unsigned char numero;

    for (i=0; i<65536; i++)
    {
        num_inteiro = rand();
        numero = (char)(num_inteiro % 256);
        fwrite(&numero, sizeof(numero), 1, arquivo);
    }
    fclose(arquivo);
}

void ConsultarArquivo()
{
    FILE * arquivo;
    int i, j;
    unsigned char numero;
    int num_inteiro;
    long deslocamento = 0;

    if ((arquivo = fopen("teste.dat","rb")) == NULL)
    {
        printf("Erro! Falha ao abrir o arquivo");
        exit(1);
    }

    for (i=0; i<65536; i++)
    {
        fread(&numero, sizeof(numero), 1, arquivo);
        printf(">>>Endereço: %d >>> Conteúdo: %d\n", i, numero);
    }

    fclose(arquivo);
}

// ==================== MAIN ====================
void main()
{
//   CriarArquivo();  // para manter o mesmo arquivo, executar uma única vez a geração do arquivo

    ConsultarArquivo();
}
