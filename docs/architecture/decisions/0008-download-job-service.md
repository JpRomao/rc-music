# ADR-0008: coordenação comum de jobs de download

**Estado:** Aceito

**Data:** 2026-09-08

**Escopo:** serviço e apresentação de jobs de R1.8; não habilita rede, conversor, host instalado ou navegação interna.

## Contexto

O [ADR-0007](0007-main-extension-boundary.md) implementou a fronteira dos pedidos, mas uma resposta válida ainda dependia de admissão falsa. FR-051/053 exigem que a seleção pertença ao app após fechar o popup, com progresso/cancelamento e importação apenas de arquivo finalizado. A mesma coordenação deve servir à Lite e à Completa, sem contaminar playback.

## Decisão

Adotar `DownloadService` C++ puro, implementando `DownloadSubmission`; proposta registrada antes da implementação e aceita por adoção no código deste incremento. A composição confiável fornece um prefixo de IDs novo por execução e uma instância compartilhada por todos os canais autorizados. A policy de URLs/Unicode continua na fronteira dos adapters; chamadas internas não podem saltá-la. O serviço também valida os limites estruturais do DTO, copia o pedido e não inicia I/O durante a admissão.

- Mesmo UUID (sem distinguir caixa) e mesmos itens/ordem devolvem o recibo original. Alterar o lote sob o mesmo ID é recusado, inclusive após conclusão/cancelamento. Receber um ACK não significa que o download terminou.
- Manter recibos sem expulsão durante a vida do serviço, com limites configuráveis inicialmente de 128 jobs, 64 pedidos e 1 MiB de bytes de strings dos pedidos retidos (ID, URLs e títulos; não inclui overhead de containers/snapshots). Recusar lotes inteiros quando não couberem, sem apagar idempotência. Esses tetos não são budgets de performance nem cotas de transferência.
- Uma execução ativa, ordem FIFO, avanço por `tick`/relógio monotônico fornecido. `DownloadExecution` é uma porta rápida de início/poll/cancelamento: o adapter concreto fará I/O/decodificação em worker e possuirá limites, SSRF/TLS, temporários e publicação.
- Progresso tem fase explícita (`Downloading`, `Converting`, `Publishing`), unidades concluídas e total opcional. Não inventar percentual com total desconhecido. Identidade obsoleta não altera outro job; regressão/dado contraditório não vira conclusão.
- Cancelar fila impede início; cancelar execução aguarda confirmação terminal sem admitir outra em paralelo. Timeout total inicial de 30 minutos pede cancelamento; não declara I/O encerrado só porque o relógio venceu. Shutdown recusa novos lotes e drena a confirmação de parada.
- Só resultado `Published`, que o adapter garante ser MP3 validado e finalizado sem sobrescrita, pode chegar a `DownloadImport`. O core também exige path absoluto normalizado `.mp3`, mas isso não prova conteúdo nem segurança do filesystem. Falha de importação retém o resultado e oferece retry explícito pelo mesmo ID/path, sem baixar novamente; a porta de importação deve ser idempotente e não limpar a fila nem dar autoplay.
- `DownloadJobsModel` projeta snapshots para Qt/QML, sem expor URLs/query, com ações por ID e linhas estáveis durante progresso. A composição visual efetiva será feita depois; existir um model não equivale a tela instalada.

O alvo `rc_music_downloads` pode ser compilado sem Qt. A biblioteca de integração opcional o consome; ambos ficam fora da composição default do player. Não existem sockets, processos ou downloads reais nessa coordenação.

## Limites de duração e recuperação

O serviço é compartilhado entre conexões na mesma execução da aplicação, não entre processos independentes. Ainda será necessário transporte do host para o dono no app e armazenamento limitado de jobs/recibos para recuperação após restart. O checkpoint anterior sobre jobs duráveis permanece pendente; esta implementação não o substitui por uma garantia em memória. Ao atingir os limites, a UI final precisa oferecer tratamento acionável, não expulsão silenciosa de recibos. Não adicionar banco ou persistência de histórico de navegação por conveniência.

## Alternativas e consequências

- Job pertencente ao popup/processo efêmero: não sobrevive ao fechamento do canal e favorece duplicação.
- Rede no playback: falhas remotas passam a afetar arquivos locais; rejeitado pelos requisitos.
- Download/conversão síncronos em `submit`: bloqueariam interface/ACK; rejeitado.
- Paralelismo genérico e cache com expulsão automática: desnecessários inicialmente, ampliam estado e quebram idempotência sem contrato de retenção.

A fila em memória é um incremento da coordenação, não uma entrega ponta a ponta. Ferramentas reais, persistência, visual QML, composição no app, host e ambas as edições precisam de seus próprios checks.

## Critérios de aceitação e revisão

TDD com Catch2 sem Qt: cópia, atomicidade/capacidade, repetição/conflito de IDs, FIFO, progresso, cancelamento/timeout, resultados tardios/malformados, falhas de adapters/importação, retry sem re-download, reentrância e shutdown. Qt Test deve ligar `NativeDownloadSession` ao serviço real com execução/importação falsas e verificar projeção/ações estáveis do model. Sem rede, dispositivos ou mídia pessoal.

Revisar ao implementar persistência/limpeza do histórico, múltiplos workers, outro cliente autorizado ou política de retry; não alegar atendimento global de FR-051–053 apenas por estes testes.
