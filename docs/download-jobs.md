# Coordenação opcional de downloads

**Estado:** incremento R1.8 em desenvolvimento; sem rede, conversor, tela QML ligada ao player ou host instalado.

Contratos: [requisitos FR-051–053](product/requirements.md#26-extensões-navegação-e-fontes-remotas-planejadas), [ADR-0008](architecture/decisions/0008-download-job-service.md), [avaliação de execução ADR-0009](architecture/decisions/0009-download-execution-backends.md). O pedido completo continua incluindo downloads reais/conversão/importação, ambas as edições e instaladores; esta camada não os substitui.

## Responsabilidades implementadas

[DownloadService](../include/m0/application/DownloadService.hpp) é C++ puro, separado de reprodução e Qt. Implementa a porta de admissão e mantém uma fila compartilhada por canais de navegador que usam a mesma instância. Fechar o canal não apaga o pedido nem gera outro job. Repetição do mesmo UUID/lote devolve IDs anteriores; lote diferente sob o mesmo ID é recusado. UUIDs não distinguem caixa. A composição deve gerar um prefixo de IDs confiável novo por execução.

Admissão copia o lote inteiro e não inicia I/O. O serviço conserva recibos, sem expulsão, até ser destruído; limita inicialmente 128 jobs, 64 pedidos e 1 MiB de strings de pedidos (ID/URLs/títulos, não o overhead de containers/snapshots). Ao atingir um teto, recusa novas admissões sem perder as anteriores. Esses limites são ajustáveis por composição confiável, não por mensagem da página. Ainda falta a persistência/limpeza limitada de histórico para recuperação após restart; não anunciar deduplicação entre processos independentes.

[DownloadExecution e DownloadImport](../include/m0/ports/DownloadExecution.hpp) separam execução e importação. O executor real ainda não existe. Testes usam fakes de I/O, não downloads simulados apresentados como concluídos ao usuário.

- Uma execução ativa, fila FIFO. Fases de download, conversão e publicação têm progresso próprio; total desconhecido mantém indicador indeterminado.
- Cancelamento de item na fila é imediato; em execução aguarda confirmação terminal. Timeout total inicial de 30 minutos também pede cancelamento, sem presumir que o worker parou. Nenhum próximo trabalho começa antes de confirmar a saída do ativo.
- Resultado obsoleto, inválido ou contraditório não importa arquivo; exceção não implica sucesso. Shutdown recusa novos pedidos e cancela/drena os próprios jobs, sem tocar na reprodução local. A composição deve aguardar `stopped()` antes de destruir adapters emprestados; o destrutor não encerra processos externos por conta própria.
- Apenas resultado publicado com path absoluto normalizado `.mp3` é encaminhado à importação. Essa checagem do core não valida bytes: o executor deve provar formato/áudio e publicação sem sobrescrita. Falha de importação preserva o arquivo e permite retry explícito pelo mesmo job, sem repetir download; o importador concreto deve ser idempotente e usar o pipeline local sem autoplay.

[DownloadJobsModel](../src/adapters/DownloadJobsModel.hpp) expõe títulos, estados, progresso, erro tipado e ações por ID para a futura UI Qt/QML. Não oferece papel de URL/query. `refresh()` não executa trabalho ou avança relógio; inserções/atualizações preservam índices existentes. Texto de página continua não confiável: a futura QML deve usar texto simples, não RichText/HTML. A projeção não é uma tela já disponível no RC Music.

## Checks locais Windows

Na sessão MSVC local descrita em [build do core](core-build.md#build-windows), reutilizando Qt/Catch2 já provisionados, configure conforme [biblioteca nativa](browser-extension.md#biblioteca-nativa-windows). Compile os alvos e filtre os testes desse componente:

```powershell
cmake --build out/product/browser-integration-tests --target rc_music_downloads_tests rc_music_NativeDownloadCodec_tests rc_music_NativeMessageFrame_tests rc_music_NativeDownloadSession_tests rc_music_NativeDownloadService_tests rc_music_DownloadJobsModel_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de downloads falhou' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.(downloads\.|browser\.)' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes de downloads falharam' }
```

`RC_MUSIC_BUILD_DOWNLOADS=ON` também permite compilar/testar o serviço puro sem `RC_MUSIC_BUILD_BROWSER_INTEGRATION` ou `RC_MUSIC_BUILD_DESKTOP`. Com `BUILD_TESTING=ON`, fornecer o source de Catch2 como no core. Para provar produção sem Qt/testes:

```powershell
$rcNinja = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
cmake -S . -B out/product/download-service-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DRC_MUSIC_BUILD_DOWNLOADS=ON -DRC_MUSIC_BUILD_BROWSER_INTEGRATION=OFF -DRC_MUSIC_BUILD_DESKTOP=OFF -DBUILD_TESTING=OFF "-DCMAKE_MAKE_PROGRAM=$rcNinja"
if ($LASTEXITCODE -ne 0) { throw 'Configure puro falhou' }
cmake --build out/product/download-service-release --target rc_music_downloads --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build puro falhou' }
ctest --test-dir out/product/download-service-release -N
```

Não solicitar benchmark, mídia pessoal, DLLs novas, download/instalação de dependências ou Registry para estes testes. Os três opcionais da raiz ficam OFF por padrão; a composição atual do player permanece sem serviços de rede/engine.

## Evidência do incremento de 2026-09-08

- TDD da coordenação: baseline com implementação vazia **1 passagem/26 falhas**; Green inicial **27/27**. Regressão adicional observou `Downloading` durante o callback de importação quando o contrato exigia `Publishing`; após corrigir a transição anterior ao handoff, **28/28**.
- TDD da projeção: model vazio falhou com zero papéis em vez dos 12 públicos. Regressão de `refresh()` reentrante durante inserção produzia três linhas para dois jobs; após coalescer atualizações, **22/22** casos Qt, sem skips. A ação reentrante em `dataChanged` também conserva o estado mais recente.
- Build Release MSVC 19.44/Qt 6.11.2 com `/W4 /WX` e seleção CTest **33/33**: 28 casos Catch2 do serviço e cinco alvos Qt. Os alvos Qt somam **222 passagens**, sem falhas/skips: codec 137, frame 35, sessão 21, sessão com serviço real 7 e model 22. São suites de componente, não testes de download na internet ou de UI instalada.
- Regressões locais: core independente **227/227**; cliente JavaScript **110/110**, sem rede ou navegador real. Nenhuma suíte de benchmark foi executada.
- Produção opcional: `download-service-release` compilou `rc_music_downloads` sem Qt/Catch2/adapters nativos; `browser-integration-release` compilou a biblioteca com Qt Core, sem Qt Test/Catch2/Qt Network/WebEngine. Ambos usam `BUILD_TESTING=OFF` e registram zero testes. Não geram host, runtime ou instalador atualizado.
- Formato dos oito C++ novos, validação documental **68 Markdown, 83 requisitos, 17 checks e 80 etapas**, rastreabilidade e `git diff --check` aprovados. Revisão independente da implementação e dos contratos não deixou achados materiais neste escopo.

Nenhum requisito amplo foi promovido: 15 planejados, 66 em andamento e 2 comprovados. A execução concreta, a integração com o player e as edições completas continuam os próximos incrementos, não funcionalidades entregues por estes fakes.

## Pendências funcionais

1. Executável Native Messaging com transporte binário e encaminhamento para o serviço dono no app; composição/prefixo/identidade por instalação.
2. Download real protegido contra SSRF/redirects e conversão isolada, com ferramentas corrigidas e verificadas conforme ADR-0009; fakes não provam esses controles.
3. Persistência limitada de jobs/recibos, recuperação/cancelamento no restart e tratamento acionável de capacidade/histórico sem duplicação silenciosa.
4. Importador concreto, timer/ciclo de vida e tela QML no player; MP3 real validado deve ser escutável na fila existente.
5. Navegação interna da Completa, integração comum instalada nas duas edições, opcionais do setup e testes reais autorizados do Brave. YouTube continua separado.
