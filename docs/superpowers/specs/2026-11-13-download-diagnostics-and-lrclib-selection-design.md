# Diagnóstico de áudio, recuperação de downloads e escolha no LRCLIB

**Estado:** conversão e recuperação aprovadas para execução isolada; LRCLIB em etapa separada · **Base:** `677de0d` · **Data:** 2026-11-13

## Pedido e limites

Investigar a causa real dos downloads que exibem ausência de áudio, oferecer tentativa explícita e cópia das informações do pedido, e tornar LRCLIB a fonte primária com termos visíveis e escolha da gravação/letra. Não implementar o restante do roadmap de letras nesta entrega. Preservar músicas, fila, dados pessoais, downloads em andamento e controles de DNS/TLS, pins, ACK, NoChild, isolamento e publicação sem overwrite. Não instalar, registrar componentes nem reiniciar o aplicativo pessoal por este trabalho.

## Evidência inicial — limites do histórico

O executável instalado corresponde por SHA-256 ao runtime de `4e879e1`. A projeção do histórico observou falhas `no-audio`, mas os contadores são por fase: entram em zero na conversão. O journal conserva `WorkspaceReady` e só registra os tamanhos na preparação da publicação. Portanto esses campos **não demonstram** falha anterior à transferência nem arquivo vazio. O resolvedor aceita somente M4A/AAC por HTTPS com `vcodec=none`; ausência de áudio e falha de decodificação não podem ser inferidas da reprodução da página.

O Media Foundation pode emitir `NoAudio` por ausência de tipo nativo e também por zero amostras decodificadas. Caracterizar essas fronteiras com mídia sintética isolada e o worker real antes de corrigir o codec. Corrigir apenas uma causa reproduzida; não alargar formatos, aceitar vídeo sem áudio ou remover validações por suposição. Se a causa dos pedidos pessoais continuar sem prova, registrar a limitação e acrescentar somente diagnóstico enumerado, sem mensagens remotas, URLs assinadas, cookies ou conteúdo pessoal nos logs.

**Caracterização inicial executada:** um harness de fixture confiável, compilado em diretório scratch e ligado ao codec atual sem alterar seus fontes, recebeu três senóides AAC de dois segundos: MP4 convencional, MP4 fragmentado e variante DASH. FFprobe identificou áudio AAC e 88 pacotes em cada entrada. O codec converteu cada uma em 31.692 bytes de MP3 e sua validação nativa retornou sucesso em todos os casos. Esse primeiro ensaio não reproduziu o erro nem qualificou o supervisor, a aquisição remota ou o pacote instalado.

### Reprodução posterior das fontes públicas

O mantenedor forneceu três páginas YouTube e uma página Letras. Um harness descartável usou os adaptadores atuais com o perfil de pacote e os componentes da instalação, em diretórios isolados. Os 47 arquivos previstos pelos pins do perfil foram conferidos por SHA-256 contra a instalação, sem divergências. O ensaio não usou Native Messaging, a interface instalada, publicação nem importação na biblioteca.

| Vídeo fornecido | Bytes recebidos, iguais ao tamanho esperado/HTTP | Conversão original | MP3 validado no controle diagnóstico |
| --- | ---: | --- | ---: |
| `nusYeKN4ByE` — Change / Miwa | 4.043.918 | `no-audio`, zero progresso decodificado | 5.985.186 bytes |
| `GXHJDBl3vrQ` | 3.463.260 | `no-audio`, zero progresso decodificado | 5.124.436 bytes |
| `0whqsmk5flU` | 4.539.065 | `no-audio`, zero progresso decodificado | 6.718.858 bytes |

Nos três casos, a seleção de produção aceitou áudio M4A/AAC por HTTPS; resolução, transferência completa e encerramento reconhecido não apresentaram erro. O worker Media Foundation da instalação recusou as entradas na conversão. **Não se trata, nesses ensaios, de escolher uma entrada somente de vídeo nem de uma transferência vazia.**

**Controle causal:** uma cópia de cada entrada teve somente os quatro bytes do tipo da caixa temporal `edts` trocados por `free`. Comprimentos, offsets e todos os bytes dos dados de áudio permaneceram idênticos. O mesmo worker então converteu e validou MP3 nos três controles. Em Change, zerar isoladamente o início ou a duração da entrada `elst` também permitiu a conversão. Isso localiza a incompatibilidade na interpretação desses metadados temporais pelo backend atual; não determina sozinho qual ramo interno emitiu `NoAudio`. Senóides fragmentadas geradas com e sem edit list passaram no worker instalado: **não generalizar a falha para todo MP4 fragmentado, DASH ou toda edit list**.

O controle é exclusivamente diagnóstico, **não uma correção de produto**: descartar `edts` ou zerar tempos indiscriminadamente pode remover cortes, atrasos ou compensação de priming intencionais. A solução de produção ainda precisa preservar a apresentação temporal e ganhar uma regressão sintética/licenciada que reproduza o perfil problemático, sem incorporar as músicas fornecidas como fixtures.

O acesso atual a `https://www.letras.mus.br/bleach/1291859/` retornou HTTP 403, inclusive acesso negado no navegador dedicado. A identidade do vídeo dessa página não foi confirmada; não associá-la por suposição a um dos três IDs acima.

Evidência privada, sem áudio no arquivo de auditoria: `out/dg/noaudio-20261113/diagnosis-sanitized.json`, `component-pin-audit.json`, `evidence-manifest.json` e os relatórios por caso no mesmo diretório. O manifesto registra hashes das cópias de evidência e dos executáveis diagnósticos. Os áudios reais permaneceram somente em scratch. O hook CMake temporário foi removido após os ensaios; fontes de produto, instalação e dados pessoais não foram alterados.

**Autorização de execução:** após receber o diagnóstico e a sequência de resolução, o mantenedor solicitou “Prossiga com a resolução proposta”. Executar agora a regressão/correção temporal, retry/cópia e qualificação de candidato isolado. LRCLIB permanece em entrega separada. Não substituir nem reiniciar a instalação pessoal. Os ensaios anteriores não fecham publicação/importação nem qualificam instalador, release ou suíte global.

### Implementação e alcance qualificado

- A regressão sem rede, gerada a partir do corpus AAC sintético/licenciado existente, reproduziu `NoAudio` com um edit de corte e mostrou perda de duração com silêncio inicial. O Red foi observado antes da implementação; os dois casos passaram depois da correção, com contagem exata de PCM apresentado ao encoder e validação MP3. Isso não significa exatidão amostral do MP3, que tem enquadramento e atraso de codec. Uma regressão adicional de entrada fragmentada **sem** edits também passou.
- O worker faz um remux virtual limitado: interpreta tabelas de fragmentos, conserva os pacotes AAC por ranges somente-leitura e apresenta ao leitor nativo tabelas MP4 convencionais. Nenhum AAC é recodificado nessa preparação; nenhum arquivo de entrada é modificado ou reaberto por nome.
- Os edits aceitos são interpretados e materializados **uma única vez no PCM**: corte/priming e silêncio inicial opcional. A visão intermediária tem tempo bruto; não se trata de apagar metadados e ignorar sua intenção. Cortes fracionários arredondam no máximo uma amostra. O perfil qualificado é AAC-LC/AOT2, uma faixa, um edit de mídia com taxa 1, opcionalmente precedido de um edit vazio. Perfis declarados fora desse recorte são recusados; a discriminação completa de extensões ASC SBR/PS posteriores ao prefixo AOT2 permanece uma limitação conhecida, não suporte qualificado a HE-AAC.
- O oracle posterior lê o PCM recuperado do **MP3 final**, usando tons sintéticos não estacionários, trecho e marcador de cauda distinguíveis, lead silencioso, clocks distintos e cortes fracionários. Controles igualmente longos e decodificáveis com corte errado ou lead ausente são recusados pelo oracle. A duração recuperada é comparada a uma referência WAV/MP3 independente e à granularidade de um quadro MPEG; o contador interno não substitui essa prova.
- Esse oracle revelou perda de cauda no wrapper MP3 ACM para blocos pequenos. Um guard de codificação limitado libera o lookahead; a saída é limitada ao número de quadros MPEG completos necessário para a apresentação PCM, sem exportar o guard como silêncio adicional ou progresso. Quotas e validação permanecem ativas. A matriz sintética cobre 32/44,1/48 kHz, mono/estéreo e finais inteiros/fracionários; não é uma qualificação em máquina limpa ou de outros drivers/backends.
- As três entradas reais originais, com hashes conferidos e sem a alteração diagnóstica de `edts`, passaram no novo worker protegido e na segunda validação nativa. O progresso de apresentação foi respectivamente 249.746.666, 213.826.666 e 280.346.666 microssegundos. O ensaio não publicou nem importou esses arquivos na biblioteca pessoal.
- Retry/cópia estão implementados em serviço/controlador/model/binding/QML, com testes do Main QML real e portas determinísticas. Cada linha falha pode criar um sucessor; tentativas posteriores partem desse sucessor. Isso evita redownload pelo ancestral após sucesso do sucessor e mantém o histórico.
- Após a revisão independente, quatro Important ganharam regressões e correções: passthrough de MP4 clássico com extensões/metadados, oracle temporal do MP3 final, ambiguidade terminal mascarada por Timeout e credenciais com chaves compostas no título copiado. O fechamento afetado passou em 198 registros CTest de UI/core (196 core, model e binding), model 66, binding 133 e Quick 51, sem falhas/skips. Há um aviso Qt APPLESS conhecido, reproduzido fora do produto e não suprimido.
- O fechamento nativo posterior passou em 9/9 CTest: cinco consumidores MF/parser (Qt: protocolo 26, parser 113, codec 47, conversão 23 e launcher 33) e quatro fronteiras de owner/importação/composição/build. Publicação, importação e os dois Saved reais foram exercitados em diretórios isolados; aquisição/DNS usam fixtures nessa composição. As três capturas originais foram reexecutadas pelo worker protegido final, com MP3 validado e duração bruta dentro de um quadro MPEG da apresentação. Isso não é jornada pelo Brave/app instalado nem suíte global.
- Preparação/autenticação do candidato permanece separada dos testes de componentes; instalação pessoal, Registro, aceitação auditiva/manual e qualificação de release/máquina limpa não foram executados. Permanecem três Minor documentados: extensões ASC SBR/PS, fase de cancelamento antes do início e orientação de falha de publicação restaurada sem fonte. Evidências: `.superpowers/sdd/2026-11-13-download-conversion-recovery/` e `out/dg/noaudio-remux-parser/`.

## Abordagem

Reutilizar os serviços e a persistência existentes. Não criar outro downloader, catálogo de letras ou histórico persistente de URLs. As três entregas são independentes e têm testes focados próprios.

### 1. Diagnóstico e correção da classificação de áudio

- Reproduzir com fixtures sintéticas AAC/MP4, incluindo o perfil temporal de `edts`/`elst` identificado nas entradas reais; distinguir trilha ausente, trilha presente sem amostras, contêiner/codec não reconhecido e conversão/validação falhas. Uma senóide fragmentada genérica que passa não substitui a regressão do perfil problemático.
- Qualificar um tratamento compatível que preserve a semântica temporal; a supressão de metadados usada no diagnóstico não está aprovada como solução permanente.
- Preservar validação completa do MP3 e o reconhecimento do encerramento do worker. Não publicar entrada parcial nem renomear MP4 para MP3.
- Acrescentar apenas informação fechada necessária para distinguir a fase e a falha, sem stderr livre ou dados de rede.
- Aceite: Red comportamental da causa identificada e Green com o worker real; os casos realmente sem áudio continuam recusados. Ensaio sintético não prova a causa de um vídeo pessoal cujo endereço não esteja disponível.

### 2. Tentar novamente e copiar informações

- `Tentar novamente` usa o item original confiável ainda retido na sessão e admite **um novo pedido/ID** pelo controlador durável; a execução só começa após `Saved`. YouTube resolve outra URL de mídia, sem reutilizar uma URL assinada anterior.
- Não reutilizar um workspace terminal nem apagar o histórico. Impedir duplo clique/reentrada de criar tentativas concorrentes do mesmo pedido. Respeitar quotas, saída e recuperação em andamento.
- Não relançar áudio de pedidos concluídos ou de falha de importação; esta mantém `Tentar importar novamente`. Publicação ambígua exige recuperação, não download cego.
- O checkpoint atual não guarda as origens. Para pedidos restaurados sem origem em memória, explicar a limitação e orientar reenvio pelo navegador. Se a falha exigir conferir os arquivos, mostrar primeiro essa orientação, inclusive após reiniciar.
- `Copiar informações` funciona também para falhas e histórico. O diagnóstico inclui ID, estado, código tipado, contadores exatos e disponibilidade de recuperação. O título segue visível na linha do download e fica fora do diagnóstico, junto com endereços de mídia, credenciais, corpo remoto, caminhos locais e bytes de música. O texto simples pode ser selecionado e copiado com mouse ou teclado.
- Aceite: falha → clique → nova admissão `Saved` → uma execução; save falho não executa; clique repetido não duplica; alvo antigo/removido e saída recusam. Cópia confere os campos e omite segredos sintéticos. Histórico sem fonte explica a indisponibilidade.

### 3. LRCLIB primário, pesquisa visível e escolha

- LRCLIB passa a primeira fonte; configuração LyricFind não se torna prioridade implícita. Letras.mus.br continua link/importação explícita, sem scraping novo.
- Na busca manual, mostrar e permitir corrigir título/artista e apresentar os demais metadados realmente enviados. Abrir/digitar não consulta a rede.
- A consulta explícita de candidatos usa `GET https://lrclib.net/api/search` v1, no host fixo, com até 20 registros, prazo, limites de bytes, TLS e `Retry-After` existentes. Não introduzir v2 beta, paginação inventada, cache persistente ou publicação de letras.
- Listar gravações por ID/título/artista/álbum/duração quando presentes. Distinguir a gravação dos formatos LRC/texto. Mostrar a prévia e exigir escolha para usar a letra; uma busca não desmarca a versão salva nem troca automaticamente a letra ativa.
- Cancelar, falhar ou receber resposta tardia conserva a letra anterior. A identidade da faixa/consulta revoga ações antigas. Reutilizar o corpo da busca, sem novo HTTP para prévias já recebidas.
- Preservar o caminho de consulta pós-download sem repetir aquisição de áudio, as restrições do LyricFind e o salvamento explícito existente. Não unificar transportes de consumidores independentes nem acrescentar cache entre sessões licenciadas nesta entrega.
- Aceite: query exata visível, lista de gravações com o mesmo título/artistas diferentes, prévia/formato, escolha explícita, cancelamento/erro/stale sem troca de letra, limite/JSON/429, texto simples e zero consultas ao abrir. A busca de letras continua independente do resultado do MP3.

## Validação proporcional e entrega

Usar fixtures e diretórios privados de teste, sem músicas pessoais ou dependência da internet nos automatizados. Identificar os alvos reais por CMake/CTest antes dos comandos, registrar Red → Green e executar consumidores atingidos por cada contrato. Agrupar a regressão dos recortes após as últimas alterações. Formato dos arquivos tocados, lint QML afetado e build Release entram no fechamento integrado; não rodar suíte global ou benchmarks por hábito. Relatar checks executados, falhas conhecidas e validações não executadas separadamente. A instalação e a aceitação manual do pacote permanecem ações distintas.
