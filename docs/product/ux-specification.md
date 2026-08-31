# Especificação de UX e interface

**Estado:** proposta para protótipo e teste  
**Relaciona-se a:** [requisitos](requirements.md)
**Última revisão:** 2026-08-31

## 1. Direção

A interface deve parecer uma pequena ferramenta desktop de alta qualidade, não uma página web nem uma central multimídia. A hierarquia é sempre:

1. faixa atual;
2. posição e reprodução;
3. fila;
4. detalhes e preferências.

Minimalismo aqui significa retirar competição visual, não esconder funções essenciais. Controles principais devem permanecer visíveis e legíveis.

## 2. Estrutura da janela

Tamanho inicial proposto: `960 × 640`. Tamanho mínimo: `720 × 520`. A janela deve funcionar entre 100% e 200% de escala. Em largura reduzida, capa e texto se empilham; controles nunca são cortados.

```text
┌──────────────────────────────────────────────────────────────────┐
│ Marca/nome                 Abrir   Fila   Preferências   Janela  │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│          ┌────────────────┐   Título da faixa                    │
│          │                │   Artista · Álbum                    │
│          │      capa      │   [ detalhes do arquivo ]            │
│          │                │                                      │
│          └────────────────┘                                      │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│  01:24  ───────────────●──────────────────────────────  04:08   │
│  volume       anterior   −5      ▶      +5    próxima            │
└──────────────────────────────────────────────────────────────────┘
                                            ┌─────────────────────┐
                                fila →      │ itens / reordenar   │
                                            └─────────────────────┘
```

A fila abre como painel lateral de aproximadamente 320–380 px, redimensionando o conteúdo quando houver espaço e sobrepondo-o em janela estreita. Detalhes técnicos abrem como folha/modal não bloqueante. Preferências usam janela ou painel próprio com navegação curta.

## 3. Estados da tela principal

### 3.1 Vazio

- placeholder de capa discreto;
- título “Abra um áudio para começar”;
- botão primário “Abrir áudio”;
- texto secundário “ou arraste arquivos para esta janela”;
- controles de reprodução desabilitados, sem parecer quebrados.

### 3.2 Carregando

- geometria da capa e dos textos permanece estável;
- skeleton sutil ou spinner com rótulo acessível;
- a interface continua aceitando uma nova abertura;
- tarefa anterior é cancelada ou descartada por geração.

### 3.3 Pronto, tocando e pausado

- play e pause são a única mudança estrutural do botão central;
- estado é exposto ao leitor de tela e à integração do sistema;
- a barra usa posição interpolada visualmente, mas o engine é autoridade;
- título longo usa elipse e tooltip/nome acessível completo.

### 3.4 Erro recuperável

- preservar fila e faixa anterior quando possível;
- mensagem curta na área relevante ou toast;
- ação primária contextual: “Pular”, “Tentar novamente” ou “Localizar arquivo”;
- detalhes técnicos ficam recolhidos e copiáveis, sem stack trace na UI comum.

### 3.5 Sem duração ou sem seek

- progresso indeterminado ou somente informativo;
- slider e saltos ficam desabilitados quando a fonte não permite buscar;
- tooltip e nome acessível explicam “Esta fonte não permite avançar ou voltar”.

## 4. Comportamento dos controles

### 4.1 Progresso e seek

- clicar na trilha busca diretamente a posição apontada;
- ao iniciar drag, a UI entra em `seek preview` e deixa de aplicar ticks do engine ao thumb;
- durante o drag, tempo previsto e tooltip acompanham o ponteiro;
- ao soltar, um único `SeekTo` é enviado;
- após confirmação, o progresso volta a seguir o engine;
- erro restaura a posição confirmada e anuncia a falha;
- seek em pausa não começa a tocar;
- teclado no slider usa passos acessíveis e `Home`/`End` quando suportado.

Atualização do texto e da barra deve ficar entre 5 e 10 Hz; animação local pode ser mais fluida sem consultar o backend a cada frame.

### 4.2 Saltos

- botões exibem dinamicamente `−Ns` e `+Ns`;
- defaults: 5 s para ambos;
- preferências independentes: 1–300 s;
- resultado é limitado ao início/fim;
- setas esquerda/direita usam os mesmos valores e casos de uso.

### 4.3 Anterior e próxima

- anterior após mais de 3 s reinicia a faixa; até 3 s vai ao item anterior;
- próxima ignora item indisponível, registra o problema e continua;
- pressionar repetidamente não permite que respostas assíncronas antigas mudem a faixa atual.

### 4.4 Volume

- slider com 0–100% e botão de mudo;
- desmutar restaura o volume anterior diferente de zero;
- scroll só altera volume quando o controle está sob foco/ponteiro, evitando mudanças acidentais;
- volume persistido não é amplificação acima de 100% no MVP.

## 5. Fila

Cada ocorrência possui ID próprio, mesmo que o path se repita. Uma linha contém:

- indicador da faixa atual;
- título ou nome do arquivo;
- artista quando conhecido;
- duração;
- estado indisponível/erro;
- menu contextual para tocar agora, mover para cima/baixo e remover; localizar e ver detalhes permanecem em recorte posterior.

Regras:

- abrir um único arquivo no estado vazio toca imediatamente;
- abrir arquivos adicionais oferece comportamento previsível: a primeira seleção toca e as demais entram em ordem, sem apagar silenciosamente a fila;
- drag-and-drop na fila mostra posição de inserção;
- limpar exige confirmação para toda fila não vazia, inclusive com um único item; cancelar ou fechar o diálogo preserva fila, seleção e reprodução;
- remover a faixa atual tenta somente a próxima ocorrência à frente, sem circular nem voltar; se não houver sucessor, descarrega a sessão e deixa a reprodução vazia, preservando as ocorrências anteriores;
- o foco acompanha reordenação e remoção; mover anuncia título e nova posição por uma região acessível. A cobertura automatizada não substitui NVDA/manual.

## 6. Detalhes da faixa

Uma folha mostra informações em grupos:

- **Faixa:** título, artista, álbum, número, ano;
- **Arquivo:** nome, path copiável, tamanho e modificação;
- **Áudio:** duração, contêiner, codec, bitrate, sample rate, canais e bit depth;
- **Diagnóstico:** campos ausentes ou limitações relevantes, sem ruído quando tudo está normal.

O path completo não ocupa a tela principal. Valores desconhecidos usam “Não informado”, não zero ou string vazia.

## 7. Bandeja e ciclo de vida

Ao fechar pela primeira vez com comportamento “bandeja”, mostrar aviso discreto: “O player continuará na bandeja. Use Sair para encerrar.” O aviso pode ser desativado.

Menu mínimo:

1. faixa atual, desabilitada ou usada para mostrar a janela;
2. play/pause;
3. anterior e próxima;
4. mostrar/ocultar;
5. sair.

Duplo clique no ícone restaura e ativa a janela. Restaurar preserva posição, fila e foco sensato. No Linux sem suporte a tray, fechar segue a preferência de encerramento/minimização normal e nunca cria um processo invisível.

## 8. Preferências

Categorias iniciais:

### Geral

- ao fechar: minimizar para bandeja / encerrar;
- restaurar última sessão;
- iniciar minimizado, somente se tray estiver funcional;
- botão para abrir configurações de Aplicativos Padrão no Windows.

### Reprodução

- segundos para retroceder;
- segundos para avançar;
- em 0.2, dispositivo de saída quando suportado;
- comportamento ao terminar a fila.

### Aparência

- tema: sistema / claro / escuro;
- movimento: sistema / reduzido;
- editor de tema aparece somente em fase futura.

Alterações seguras possuem preview imediato. Valores inválidos mostram mensagem junto ao campo e não são persistidos.

## 9. Associação de arquivos

O produto não promete alterar silenciosamente o padrão do Windows. A preferência explica:

> O Windows permite que você escolha o aplicativo padrão nas Configurações. Abriremos a página correta para você.

O instalador registra apenas formatos efetivamente validados. A versão portátil informa que associações permanentes podem não estar disponíveis. No Linux, o pacote declara os MIME types e respeita a escolha do ambiente desktop.

## 10. Atalhos e foco

Os atalhos padrão são definidos nos [requisitos](requirements.md). Regras adicionais:

- `Tab` segue topo → conteúdo → transporte → painéis;
- abrir um painel move o foco para seu título/primeira ação;
- fechar devolve foco ao controle que o abriu;
- `Esc` fecha painel, modal ou menu, mas não encerra o app;
- `Space` não dispara play/pause enquanto edita um campo ou manipula um controle que usa espaço;
- controles de ícone têm rótulo, estado e dica de atalho.

## 11. Visual, tema e movimento

Direção proposta:

- superfícies planas com elevação discreta apenas em painéis;
- uma cor de destaque derivada do tema, não necessariamente da capa no MVP;
- tipografia do sistema para integração e inicialização rápida;
- raios moderados e consistentes;
- área clicável mínima de 40 × 40 px, ideal de 44 × 44 px;
- transições de 120–200 ms para mudanças de painel/estado;
- nenhuma animação decorativa contínua;
- movimento reduzido elimina deslocamentos e usa fades curtos ou troca direta.

Tokens obrigatórios: canvas, surface, surface-raised, text-primary, text-secondary, accent, on-accent, border, focus, error, warning e success. Todo par publicado deve satisfazer contraste aplicável.

## 12. Acessibilidade e validação manual

Antes do MVP:

- navegar todas as jornadas sem mouse;
- testar nomes, papéis, estados e anúncios com NVDA no Windows;
- validar 100%, 150% e 200% de escala;
- validar alto contraste e temas claro/escuro;
- validar movimento reduzido;
- no Linux de referência, testar Orca/AT-SPI quando suportado pelo toolkit;
- evitar anúncio do cronômetro a cada tick.

## 13. Jornadas de teste de usabilidade

1. **Primeiro uso:** abrir um MP3 e pausar sem instrução.
2. **Revisão de áudio:** voltar 7 s, mudar o valor para 12 s e repetir.
3. **Arquivo longo:** arrastar para 50%, pausar, buscar novamente e retomar.
4. **Fila:** soltar cinco arquivos, reordenar, remover um e avançar.
5. **Segundo plano:** fechar, controlar pela bandeja e sair completamente.
6. **Arquivo associado:** abrir por duplo clique com o app já em execução.
7. **Recuperação:** reiniciar após término forçado e encontrar a sessão pausada.
8. **Falha:** abrir arquivo corrompido entre duas faixas válidas e continuar.
9. **Acessibilidade:** executar as jornadas 1–5 apenas com teclado e leitor de tela.

## 14. Entregáveis antes do código de UI definitivo

- protótipo navegável dos estados vazio, tocando, fila, detalhes, preferências e erro;
- matriz de componentes e estados interativos;
- revisão de contraste e escala;
- spike do toolkit com slider acessível, drag-and-drop, painel e tray;
- teste breve com pelo menos as jornadas 1, 2, 5 e 9;
- registro das decisões no ADR da stack.
