# ADR-0021: Até três downloads ao mesmo tempo

**Estado:** Aceito

**Data:** 2026-11-16

**Relaciona-se a:** [ADR-0008](0008-download-job-service.md) (serviço de jobs de download) e ao [plano da biblioteca e da busca](../../superpowers/plans/2026-11-16-library-and-search.md) (fatia L8).

## Contexto

O serviço de downloads rodava um job por vez. Ele tinha uma única vaga de execução nativa, e o executor do Windows tinha uma única thread de trabalho. Com a busca interna e os links colados (L5 e L7), o usuário passou a pedir vários áudios de uma vez, e cada um esperava o anterior terminar. Em 2026-11-16 o mantenedor pediu:

> Também deve ser possível fazer download de até 3 obras separadas.

## Decisão

1. O serviço guarda as vagas ocupadas numa lista, com o limite `DownloadServiceLimits::max_active_jobs`, de 1 a 3 e com padrão 1. Cada tick inicia no máximo um job da fila enquanto houver vaga, e um tick que inicia um job não consulta os outros, como antes. Os demais ticks consultam todos os jobs em execução, cada um com o seu prazo. Encerrar o serviço cancela ou drena todos eles.
2. O `WindowsDownloadExecution` passa a ter de 1 a 3 pistas. Cada pista é o executor anterior: uma thread, um job por vez e o mesmo protocolo de terminal observado. Um job vai para uma pista livre, e `poll`, `cancel` e o caminho retido encontram a pista pelo `job_id`. Uma pista fica livre quando o dono observa o terminal do seu job.
3. O número de vagas do serviço vem do executor (`lanes()`), então os dois nunca discordam. O perfil de execução tem `parallel_jobs`, que vale 1 por padrão. O app usa 3.
4. O lock de arquivo dos documentos privados (checkpoint e diário de propriedade) recusa um segundo handle em vez de esperar. As gravações do processo passam por um `std::recursive_mutex` antes desse lock. Assim, pistas diferentes esperam a vez. Uma gravação aninhada na mesma thread continua esbarrando no lock de arquivo e falhando, como antes.
5. Cada job continua com o seu workspace, os seus processos de rede, mídia e YouTube e a sua cópia do runtime. A recuperação e o diário de propriedade continuam por job.

## Alternativas

- **Um pool de executores separados no serviço:** duplicaria o desligamento, o `stopped` e o caminho retido, e a composição teria de somar executores. Rejeitada em favor das pistas dentro do executor.
- **Esperar o lock de arquivo com novas tentativas:** troca uma falha por uma espera ativa e não ordena as gravações. Rejeitada.
- **Mais de três downloads:** multiplica processos, disco temporário e conexões sem pedido do mantenedor. Fica fora do escopo.

## Consequências

- Até três jobs consomem rede, CPU de conversão e espaço de workspace ao mesmo tempo, cada um dentro do limite que já tinha.
- A ordem de término deixa de seguir a ordem da fila. A fila de reprodução recebe cada MP3 quando a importação dele termina.
- O teste real (2026-11-16) mostrou três problemas, todos corrigidos com testes:
  - as pistas disputavam o mesmo registro antigo do diário cheio; a verificação de capacidade passou a rodar uma pista por vez, deixando uma vaga por pista;
  - o workspace e os pins de artefatos prendiam as pastas ancestrais, entre elas a de configuração, sem compartilhar escrita. O Windows recusava então a troca atômica do `session.json.bak` enquanto outro download rodava, e a importação ficava pendente. As ancestrais passaram a compartilhar escrita e continuam negando exclusão;
  - depois de um HTTP 403, a nova resolução dentro do mesmo job não conseguia gravar o registro novo, porque o da primeira tentativa continuava no diário. A tarefa agora aposenta a tentativa anterior que não deixou arquivos.
- Ficou uma falha intermitente na criação do workspace com três jobs criando ao mesmo tempo. Ela apareceu uma vez e não voltou em quatro rodadas seguidas.

## Critérios de revisão

Rever se três jobs simultâneos causarem recusas do YouTube (HTTP 403/429), se o espaço do workspace não comportar três jobs ou se o mantenedor pedir outro limite.
