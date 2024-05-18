use std::{collections::VecDeque, fs::File, io::{BufReader, SeekFrom}};
use std::str;
use std::io::prelude::*;
use std::env;

const TAMANHO_PAGINA: usize = 256;
const NUM_PAGINAS: usize = 256;
const NUM_FRAMES: usize = 128; // Originalmente 256 (para obter o resultado de correct.txt)
const TAMANHO_MEMORIA: usize = TAMANHO_PAGINA * NUM_FRAMES;
const TLB_ENTRADAS: usize = 16;

const CAMINHO_BACKING_STORE: &str = "BACKING_STORE.bin";

// Estrutura que representa uma memória
struct Memoria
{
    dados: [u8; TAMANHO_MEMORIA],
    tabela_paginas: TabelaPaginas,
    tlb: VecDeque<Entrada>,
}

// Estrutura usada para mapear uma página a um frame
struct Entrada
{
    num_pagina: usize,
    num_frame: usize,
}

// Estrutura que representa uma tabela de páginas
struct TabelaPaginas
{
    num_frames: [Option<usize>; NUM_PAGINAS],
    fila_substituicao: VecDeque<Entrada>,
}

// Estrutura que representa o resultado de uma consulta na memória
struct ResultadoConsulta
{
    endereco_fisico: usize,
    page_fault: bool,
    tlb_hit: bool,
    valor: i8,
}

impl Memoria
{
    // Inicializa uma memória
    pub fn nova() -> Memoria
    {
        Memoria
        {
            dados: [0; TAMANHO_MEMORIA],
            tabela_paginas: TabelaPaginas::nova(),
            tlb: VecDeque::with_capacity(TLB_ENTRADAS),
        }
    }

    // Consulta o TLB, retornando o frame correspondente se a página estiver armazenada nele
    fn consultar_tlb(&self, num_pagina: usize) -> Option<usize>
    {
        for entrada_tlb in self.tlb.iter()
        {
            if entrada_tlb.num_pagina == num_pagina
            {
                return Some(entrada_tlb.num_frame);
            }
        }

        None
    }

    // Insere um mapeamento página-frame no TLB
    fn atualizar_tlb(&mut self, num_pagina: usize, num_frame: usize)
    {
        if self.tlb.len() == TLB_ENTRADAS
        {
            // Fila do TLB está cheia, remover página mais antiga (inserida antes)
            self.tlb.pop_front();
        }

        // Inserir nova página
        self.tlb.push_back(Entrada
        {
            num_pagina,
            num_frame,
        });
    }

    // Lê a página `num_pagina` do arquivo `bck_store` e a armazena no frame `num_frame`
    fn ler_do_arquivo(&mut self, num_pagina: usize, num_frame: usize, bck_store: &mut File)
     {
        let frame_fim = num_frame + TAMANHO_PAGINA;

        bck_store.seek(SeekFrom::Start((num_pagina * TAMANHO_PAGINA) as u64)).expect("Falha ao posicionar cursor no arquivo");
        bck_store.read(&mut self.dados[num_frame..frame_fim]).expect("Falha ao ler arquivo");
    }

    /*
     Consulta a memória usando o endereço virtual `endereco_virtual` e o arquivo
     `bck_store` como base
    */
    pub fn consulta(&mut self, endereco_virtual: u32, bck_store: &mut File) -> ResultadoConsulta
    {
        let endereco_virtual = endereco_virtual as usize;
        // Extrair os 8 primeiros bits do endereço (número da página)
        let num_pagina = endereco_virtual >> 8;
        // Extrair os 8 últimos bits do endereço (deslocamento)
        let offset = endereco_virtual & 0xFF;

        if let Some(num_frame) = self.consultar_tlb(num_pagina)
        {
            // TLB hit

            let endereco_fisico = num_frame + offset;

            ResultadoConsulta
            {
                endereco_fisico,
                page_fault: false,
                tlb_hit: true,
                valor: self.dados[endereco_fisico] as i8,
            }
        }
        else if let Some(num_frame) = self.tabela_paginas.num_frames[num_pagina]
        {
            // Page hit
            self.atualizar_tlb(num_pagina, num_frame);
            let endereco_fisico = num_frame + offset;

            ResultadoConsulta
            {
                endereco_fisico,
                page_fault: false,
                tlb_hit: false,
                valor: self.dados[endereco_fisico] as i8,
            }
        }
        else
        {
            // Page miss
            let num_frame = self.tabela_paginas.obter_proximo_num_frame(num_pagina);
            self.atualizar_tlb(num_pagina, num_frame);
            self.ler_do_arquivo(num_pagina, num_frame, bck_store);

            let endereco_fisico = num_frame + offset;

            ResultadoConsulta
            {
                endereco_fisico,
                page_fault: true,
                tlb_hit: false,
                valor: self.dados[endereco_fisico] as i8,
            }
        }
    }
}

impl TabelaPaginas
{
    // Inicializa uma tabela de páginas
    pub fn nova() -> TabelaPaginas
    {
        TabelaPaginas
        {
            num_frames: [None; NUM_PAGINAS],
            fila_substituicao: VecDeque::with_capacity(NUM_FRAMES),
        }
    }

    // Obtém o número do frame correspondente à página `num_pagina`
    pub fn obter_proximo_num_frame(&mut self, num_pagina: usize) -> usize
    {
        let num_frame = if self.fila_substituicao.len() == NUM_FRAMES
        {
            // Memória está cheia, remover página mais antiga e usar o seu frame
            let pagina_substituida = self.fila_substituicao.pop_front().unwrap();
            self.num_frames[pagina_substituida.num_pagina] = None;
            pagina_substituida.num_frame
        }
        else
        {
            self.fila_substituicao.len() * TAMANHO_PAGINA
        };

        self.fila_substituicao.push_back(Entrada { num_pagina, num_frame: num_frame });
        self.num_frames[num_pagina] = Some(num_frame);

        num_frame
    }
}

fn main() -> std::io::Result<()>
 {
    let mut bck_store = File::open(CAMINHO_BACKING_STORE).expect("Arquivo backing store não encontrado");

    let caminho = env::args().nth(1).expect("Informe um arquivo");
    let arquivo = File::open(caminho)?;
    let mut buf_reader = BufReader::new(arquivo);

    let mut memoria = Memoria::nova();

    let mut page_faults = 0;
    let mut tlb_hits = 0;
    let mut count = 0;

    loop
    {
        let mut addr = String::new();
        let bytes = buf_reader.read_line(&mut addr).expect("Falha ao ler arquivo");

        // Fim de arquivo atingido
        if bytes == 0
        {
            break;
        }

        count += 1;

        let addr: u32 = addr.trim().parse().expect("Número inválido");
        let addr_mascarado = addr & 0xFFFF;
        let resultado_consulta = memoria.consulta(addr_mascarado, &mut bck_store);

        if resultado_consulta.page_fault
        {
            page_faults += 1;
        }

        if resultado_consulta.tlb_hit
        {
            tlb_hits += 1;
        }

        print!("Endereço virtual: {} ", addr_mascarado);
        print!("Endereço físico: {} ", resultado_consulta.endereco_fisico);
        println!("Valor: {}", resultado_consulta.valor);
    }

    println!("Número de Endereços Traduzidos = {}", count);
    println!("Page Faults = {}", page_faults);
    println!("Taxa de Page Fault = {}", page_faults as f64 / count as f64);
    println!("TLB Hits = {}", tlb_hits);
    println!("Taxa de TLB Hit = {}", tlb_hits as f64 / count as f64);

    Ok(())
}
