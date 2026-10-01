# Transporte opcional de mídia direta

**Estado:** incremento de R1.2/R1.8; não conectado ao player e não é o backend completo de download/conversão.

O [ADR-0009](architecture/decisions/0009-download-execution-backends.md) continua proposto. Este componente implementa o transporte de bytes que será composto com worker, temporários privados, conversor e publicação segura. A [coordenação de jobs](download-jobs.md) permanece separada: receber bytes não significa publicar MP3 nem importar uma faixa.

## Fronteiras implementadas

- [DownloadNetworkPolicy](../src/adapters/DownloadNetworkPolicy.hpp) reutiliza a política lexical da admissão. Preserva os bytes de Host, path e query, valida todos os resultados DNS antes de escolher um IP e recusa respostas vazias, mistas ou com mais de 32 endereços. Bloqueia faixas especiais IPv4 e permite somente alocações IPv6 revisadas, sem tradução/mapeamento/escopo. A lista IANA é estática, revisada em desenvolvimento, não baixada em execução.
- Redirects voltam à política e ao DNS a cada salto, inclusive no mesmo host. Resolvem segmentos literais relativos sem recodificar assinaturas; downgrade HTTPS, YouTube, credenciais, portas fora do contrato e destinos privados não recebem conexão. Fragmentos e segmentos de ponto codificados permanecem recusados; não há compatibilidade universal prometida.
- [PinnedDownloadStream](../src/adapters/PinnedDownloadStream.hpp) usa Qt Network com conexão ao endereço numérico escolhido, sem resolver novamente o hostname lógico. O pedido GET conserva seus bytes e usa `Accept-Encoding: identity`; não há proxy, cookie, autenticação, cache ou encaminhamento automático. HTTPS exige certificado/nome lógico válidos e TLS 1.2 ou posterior, sem ignorar erros. Nenhum GET é enviado antes da verificação TLS.
- [DownloadHttpResponse](../src/adapters/DownloadHttpResponse.hpp) limita framing HTTP/1.0–1.1, cabeçalhos/trailers e corpo. Aceita Content-Length, chunked ou fechamento ordenado; recusa framing ambíguo, compressão inesperada, upgrade, excesso e truncamento. Só status 200 emite corpo de mídia. Metadados raw são internos e não podem ir a UI/log.
- [DirectDownloadSession](../src/adapters/DirectDownloadSession.hpp) compõe policy, DNS, conexão e decoder com sink fornecido pelo proprietário do job. Tem progresso, cancelamento, erros tipados, até cinco redirects, cota de corpo inicial 512 MiB, cota de tráfego 514 MiB, deadline total de 30 min, inatividade de 15 s e resolução de 10 s. São limites ajustáveis pela composição confiável, não pela página. Cancelamento reentrante não pode virar conclusão.
- [QtDownloadIo](../src/adapters/QtDownloadIo.hpp) fornece lookup assíncrono e socket concretos. Callbacks antigos de DNS são invalidados por geração. Cancelar aborta somente seu lookup e conexão; não modifica playback, perfis ou configurações do navegador.

Construir, usar e destruir os objetos na mesma thread worker com event loop. A sessão recebe relógio monotônico por `tick()` e não cria seu próprio timer/thread. Escritas no sink podem bloquear; não chamar na thread visual. O sink precisa ser novo/vazio, não sequencial, gravável e pertencente ao job. O proprietário ainda deve controlar espaço em disco, flush, temporários, remoção de parciais e publicação sem sobrescrita. A sessão não apaga arquivos do chamador e não oferece path concluído como MP3.

No corpo delimitado somente por EOF, HTTPS exige fechamento autenticado. Se o backend TLS não expuser `close_notify`, a transferência não pode anunciar sucesso nesse modo; Content-Length/chunked continuam podendo comprovar fim pelo framing. Isso evita confundir corte da conexão com arquivo completo. Validação posterior do formato/áudio continua obrigatória mesmo com framing correto.

O socket aplica high-water de 4 KiB nas filas Qt, solicita recepção nativa de 4 KiB e confere valor efetivo de até 8 KiB antes da conexão. O fechamento TLS drena dados retidos enquanto o descritor ainda é válido, com high-water de 48 KiB para reservar um registro sob o limite legível de 64 KiB. Valores legíveis acima do teto falham fechados; não são mascarados em `poll()`. Essa margem foi exercitada por regressões de EOF/leituras desalinhadas, não é uma prova de limite de todas as filas internas ou alocações Schannel/Qt/Windows.

## Build e checks locais

Na sessão MSVC descrita em [core-build](core-build.md#build-windows), configure a [biblioteca de navegador](browser-extension.md#biblioteca-nativa-windows) com Qt/Catch2 já disponíveis e acrescente:

```powershell
cmake -S . -B out/product/browser-integration-tests -DRC_MUSIC_BUILD_DOWNLOAD_NETWORK=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure de rede falhou' }
cmake --build out/product/browser-integration-tests --target rc_music_DownloadNetworkPolicy_tests rc_music_DownloadHttpResponse_tests rc_music_PinnedDownloadStream_tests rc_music_DirectDownloadSession_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de rede falhou' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.download-network\.' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes de rede falharam' }
```

O quarto opcional da raiz, `RC_MUSIC_BUILD_DOWNLOAD_NETWORK`, fica OFF por padrão e não liga o alvo ao app. Quando solicitado, inclui as bibliotecas de coordenação/admissão, Qt Network e os [adapters de conversão local Windows](download-conversion.md); não inclui engine, Qt Multimedia, binários FFmpeg/ffprobe, executável de host ou instalador. `BUILD_TESTING=OFF` exclui Qt Test, Catch2, helpers, fixtures, servidor de teste e certificado sintético.

As suites de policy/decoder/session usam entradas sintéticas e I/O falso, sem consultar DNS público. A suite de socket usa servidores efêmeros em loopback, bytes sintéticos e [certificado/chave não secretos de teste](../tests/qt/network-fixtures/README.md), com confiança somente na instância Qt. Endpoints de baixo nível desses testes não são entradas autorizadas da policy: loopback continua recusado no downloader. Não instalar certificados, extensão, ferramentas ou host; não usar mídias, contas ou perfis pessoais.

## Evidência TDD de 2026-09-08

- Policy: Red com 42 falhas comportamentais, seguido de **156/156**. Os testes incluem decisão comum com a admissão nativa, request-target assinado, DNS misto, faixas especiais e revalidação de cada redirect.
- Decoder: Red com 80 falhas, Green inicial **82/82**; sete regressões adicionais falharam ao diferenciar excesso de recursos de framing inválido, antes do erro tipado levar a **89/89**.
- Sessão: Red com 11 falhas usando I/O falso; regressões adicionais reproduziram cancelamento durante leitura/escrita sendo sobrescrito por conclusão. A sessão preserva cancelamento reentrante, quotas e deadlines: **18/18**.
- Socket: o Red inicial confirmou ausência do transporte. Regressões posteriores reproduziram recodificação de assinaturas, Host máximo, perda de 16–32 KiB no EOF TLS e excesso de buffer com leitura desalinhada. A última falha foi reproduzida com pausa explícita perto do EOF, sem retry cego; o teto de 64 KiB e a carga de 320 KiB foram mantidos. A suite passou **43/43**, incluindo HTTP/HTTPS, SNI, certificado inválido, proxy ignorado, cancelamento e leituras de 4.093/16.383/32.767 bytes.

Fechamento proporcional: build Release MSVC 19.44/Qt 6.11.2 com `/W4 /WX`, seguido da seleção `^rc_music\.(download-network\.|downloads\.|browser\.)`, **37/37 CTest** em 14,75 s, sem falhas/skips internos. São 28 casos do serviço e nove alvos Qt; os cinco alvos anteriores conservaram suas 222 passagens. Cliente JavaScript **110/110** também passou; o core independente não foi reexecutado neste incremento sem mudança no core, e sua evidência anterior não é reapresentada como execução atual.

Os três builds `BUILD_TESTING=OFF` passaram: serviço puro sem Qt; admissão/model com Qt Core sem Qt Network; transporte opcional com Qt Network. Todos listaram zero testes. O grafo do transporte OFF não contém Qt Test, Catch2, fixtures/certificado, Multimedia, engine ou conversor. Formato dos 16 C++ afetados, documentos **70 Markdown/83 requisitos/17 checks/80 etapas**, rastreabilidade **83 vínculos válidos** e `git diff --check` passaram. Nenhum requisito amplo mudou de status: 15 planejados, 66 em andamento e 2 comprovados.

Sem benchmark, mídia pessoal, download de mídia da internet, conversão, montagem de runtime/instalador ou instalação/registro de extensão/host. A revisão independente corrigiu os achados de cancelamento, classificação de quotas e reserva de EOF; os gates abaixo permanecem explícitos.

## Gates restantes

O Schannel original constrói cadeia sem restringir ao cache as consultas auxiliares AIA/CRL/OCSP. O [builder TLS restrito opt-in](../tools/download-tls/README.md) preserva autenticação, acrescenta flags cache-only/disable-AIA e teto pré-alocação de 256 KiB. Uma armadilha AIA local reproduziu consulta no original e zero consultas no restrito; a suite isolada passou 5/5. O backend não foi instalado ou selecionado no player. A política Qt de soft-fail para revogação desconhecida permanece explícita; consultas CRL/OCSP/CTL, memória/deadline do subprocesso e composição completa continuam gates no [ADR-0009 proposto](architecture/decisions/0009-download-execution-backends.md#backend-schannel-restrito-em-processo-de-ensaio). Não há prova exaustiva de ausência de tráfego auxiliar Windows nem aprovação de internet.

1. Compor execução completa em worker com área privada limitada, encerramento, persistência/recuperação de jobs e publicação atômica; conectar à porta `DownloadExecution`.
2. Validar DNS/transport/session juntos em ambiente isolado controlado, inclusive respostas tardias, buffers internos TLS e afinidade/lifecycle Qt. Testes separados não provam o fluxo inteiro com DNS real, proxy/rede do host ou mídia da internet.
3. Os [adapters locais](download-conversion.md) ensaiam conversão/validação/publicação com fixtures sintéticas; compor o fluxo inteiro, provar isolamento, importação real e referências externas. O FFmpeg CLI 8.1.1 disponível continua sem aprovação para mídia remota e precisa de correções/revisão próprias.
4. Host Native Messaging, importador, tela QML e composição no player; depois Lite/Completa, navegação interna e opcionais/ownership dos instaladores. YouTube segue separado.
5. Revisão final de segurança, licenças/notices e distribuição dos artefatos exatos; componentes funcionais não aprovam release.
