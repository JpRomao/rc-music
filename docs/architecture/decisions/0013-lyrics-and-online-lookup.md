# ADR-0013: letras locais e consulta online explícita

**Estado:** Aceito

**Data:** 2026-11-04

**Decisão de escopo:** pedido do mantenedor para letras originais acima da barra de progresso, com suporte local e busca online opcional. O uso de LRCLIB e dos componentes Qt abaixo foi adotado na implementação; isso não aprova cobertura universal, licenças das letras ou distribuição pública.

## Evolução da consulta

O mantenedor aprovou o LyricFind como fonte opcional ao lado do LRCLIB. A integração usa o contrato Web Service e chaves de parceiro, em HTTPS para o endpoint fixo. A ordem das fontes é configurável. Um resultado LyricFind é somente para exibição, com créditos, atribuição e proteção de cópia. O catálogo local aceita persistência e geração de romaji para fontes que permitem esses usos. [Configuração e limites](../../lyrics-variants.md#fonte-opcional-lyricfind)

## Contexto

O mantenedor pediu letras da música, sem tradução, posicionadas como legendas acima da barra de progresso. Ao escolher entre uma primeira versão apenas local e a inclusão da busca online, confirmou a segunda opção. O pedido acrescenta `FR-054` aos [requisitos](../../product/requirements.md#27-letras-das-músicas) e um refinamento de volume a `FR-011`.

Esse é um novo escopo de consulta de texto autorizado pelo mantenedor. O mecanismo de fontes remotas do [ADR-0003](0003-local-first-and-remote-sources.md) permanece Proposto; os gates de extensão, navegador, conversão e downloads do [ADR-0006](0006-browser-editions-and-extensions.md) permanecem independentes. A consulta de letras não recebe URLs arbitrárias nem habilita esses serviços.

## Decisão

### Fonte local e sincronização

1. Usar um `.lrc` com o mesmo nome base do arquivo de áudio, na mesma pasta. A primeira matriz aceita arquivos regulares locais, sem UNC ou symlink, em UTF-8 com BOM opcional. A leitura ocorre fora da thread da UI, com entrada limitada a **256 KiB**.
2. Manter parsing e seleção de verso no domínio C++ puro, sem Qt, rede, relógio ou reprodução. O parser limita expansão a **1 MiB**, **4096 cues** e timestamps de até **24 h**; conserva linhas simultâneas na ordem de origem e permite cues vazios para limpar o verso anterior. Offset diferente de zero é recusado explicitamente.
3. Usar a posição confirmada da reprodução para selecionar o verso. Pausa e seek não criam um segundo relógio de música; uma posição apenas proposta pelo arraste não substitui a confirmação do engine. Letras não alteram fila, posição, volume ou autoplay.
4. Exibir o verso original em `Text.PlainText`, acima do progresso, com altura compacta e limite visual de linhas. Quando houver somente texto, mostrar uma área rolável somente de leitura no mesmo espaço. Falha ou ausência de letra não impede a reprodução local.

### Consulta online e privacidade

5. Usar `GET https://lrclib.net/api/search` para escolher uma gravação e `GET https://lrclib.net/api/get` para a consulta por metadados exatos. A [API v1 do LRCLIB](https://lrclib.net/docs.txt) oferece texto comum e LRC sincronizado em consultas anônimas. A busca por gravações aceita até 20 registros, agrupa pela identidade do provedor e usa o texto já recebido para a prévia.
6. A ação **Buscar letra** permite conferir título, artista, álbum e duração. Somente a confirmação explícita consulta o provedor. Na busca por gravações, o app envia título e os campos opcionais de artista e álbum. A duração serve para comparação local. A consulta exata pode enviar duração. O app preserva a letra ativa até que o usuário escolha um resultado da pesquisa. Arquivos, caminhos, cookies e credenciais ficam fora da consulta LRCLIB. O salvamento explícito usa o catálogo do [ADR-0016](0016-lyrics-variants.md).
7. Fixar método, host e endpoint no adapter; o usuário corrige metadados, não fornece URL ou comando. Não seguir redirecionamentos automaticamente, ignorar erros TLS, reutilizar autenticação ou carregar/salvar cookies por esse pedido. A apresentação da letra continua como texto simples, sem interpretar HTML ou scripts da resposta.
8. Limitar a resposta HTTP a **1 MiB**, cada campo de letra ao limite local e o prazo normal da consulta a **10 s**. Admitir uma consulta por vez, com intervalo mínimo de **500 ms**; respeitar `Retry-After` de HTTP 429 sem retry automático. Um prazo do servidor que não possa ser representado não é encurtado para permitir outra consulta na mesma sessão. Isso não promete evitar toda limitação do provedor.
9. Capturar os metadados e vincular resultados à revisão da consulta. O diálogo da faixa em reprodução fecha quando sua ocorrência muda. A consulta iniciada em **Gerenciar letras** permanece vinculada à ocorrência da fila e ao arquivo escolhidos, inclusive durante transições de reprodução. Remoção, indisponibilidade ou troca do arquivo revogam ações ainda abertas. Player e fila compartilham o prazo e o intervalo de consultas. Na saída, fechar admissão, cancelar a consulta e aguardar a drenagem do trabalho.

### Componentes e dependências

10. Compor `QtLyricsService` atrás do binding de apresentação. Qt Core, Network e Concurrent pertencem ao SDK Qt aceito; a leitura local usa Qt Concurrent e a consulta usa Qt Network. A UI recebe somente texto e estado, sem acrescentar CLI, engine de navegador, serviço próprio ou conta.
11. A identificação enviada em `User-Agent` pertence à composição da aplicação e identifica RC Music/versão com referência do projeto conforme o contrato do provedor. O valor adotado é `RCMusic/0.1 (+https://github.com/JpRomao/rc-music)`, com URL obtida do `origin` do repositório e sem credenciais. Essa é a URL configurada do projeto, não uma comprovação de visibilidade pública, publicação ou registro no provedor. Empacotamento, TLS disponível, notices e licenças continuam sujeitos aos checks da distribuição.

O indicador de volume solicitado no mesmo incremento usa o slider existente: percentual inteiro de **0 a 100%**, visível no hover ou durante o arraste e atualizado a partir do volume vigente. Não adiciona outra fonte de verdade para volume ou mudo.

## Alternativas e consequências

- Somente `.lrc` local reduz a superfície de rede, mas não atende à opção de busca online escolhida pelo mantenedor. Ele permanece como caminho offline prioritário.
- Gerar letras a partir do áudio exigiria transcrição, outros modelos/ferramentas e uma nova matriz de precisão; não faz parte deste pedido. Tradução também não é adicionada.
- Usar páginas HTML, scraping, navegador interno ou uma CLI amplia dependências e permissões sem necessidade para a API de texto selecionada.
- Buscar automaticamente ao trocar de faixa enviaria metadados sem a confirmação adotada. Não implementar essa política como consequência implícita do suporte online.
- Persistir letras online, embuti-las em tags ou redistribuir um catálogo exigiria outra decisão. Este incremento mantém os resultados somente na sessão.

## Validação e limites

O domínio deve ter testes determinísticos de UTF-8, timestamps, ordenação/duplicatas, limites, offset recusado e seleção de verso antes/depois de seek. O adapter deve usar arquivos próprios e transporte falso para ausência/corrupção local, query exata, cancelamento, timeout, HTTP 429, resposta inválida/excessiva e revogação de resposta antiga. A apresentação deve verificar posição confirmada, fallback, confirmação de envio, alteração de ocorrência, texto simples, geometria acima do progresso e tooltip do volume. As evidências e contagens finais pertencem ao fechamento do incremento, sem substituir a prova de cada recorte por um check genérico.

A presença de testes com transporte falso não qualifica o catálogo remoto, disponibilidade do serviço, backend TLS instalado, precisão de tempos em músicas reais ou o pacote distribuído. A existência de uma API pública sem chave não concede licença para redistribuir letras. Licenças, notices/SBOM, requisitos da distribuição e demais gates do projeto não são marcados como concluídos por este ADR.

O recorte local continua limitado a UTF-8 e offset zero; a consulta pode não encontrar a versão correta, retornar somente texto ou reconhecer uma faixa instrumental. Não há tradução, transcrição, busca universal de letras nem suporte adicional a provedores. Revisar esta decisão antes de adicionar outro host, consulta automática, persistência de letras online, redistribuição de conteúdo ou nova dependência de runtime.

**Evolução posterior:** o [ADR-0016](0016-lyrics-variants.md) registra o pedido de guardar várias formas e adicioná-las posteriormente. Acrescenta salvamento local mediante ação explícita, inclusive da letra carregada, mantendo sem alteração a proibição de cache automático, envio de áudio/paths e consulta a novos hosts. Este registro conserva o contrato e as evidências da entrega inicial.
