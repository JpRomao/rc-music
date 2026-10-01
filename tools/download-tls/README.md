# Plugin TLS restrito para o downloader

Builder local opt-in do backend `rcmusic-schannel-offline`, usando somente fontes Qt 6.11.2, SDK e MSVC já provisionados. Não instala plugin, altera o SDK, muda certificados/Registro ou seleciona backend no player. O subprocesso downloader será o proprietário da seleção e da configuração de runtime.

## Receita

Na raiz do repositório:

```powershell
node --test tools/download-tls/tests/builder.test.mjs
pwsh -NoProfile -File tools/download-tls/New-RcMusicRestrictedTlsBackend.ps1 -OutputDirectory out/product/download-tls/meu-build-01
```

O destino deve ser novo e ficar abaixo de `out/product/download-tls/`; destinos existentes, o próprio diretório de builds, destinos externos e ancestrais reparse são recusados. O builder nunca remove uma árvore existente. `-PrepareOnly` verifica e produz fontes/recibo sem invocar compilador. Paths de origem, Qt e toolchain podem ser passados explicitamente, mas seus conteúdos devem corresponder à versão/hash revisados. Não há downloads ou procura silenciosa de outra versão.

O builder valida SHA256 de 19 arquivos do plugin, versão/configuração e textos LGPL/GPL, além das duas DLLs Core/Network do SDK promovido, antes de criar o destino. Copia somente esses arquivos, preserva originais e aplica substituições com contagem exata de âncoras. `prepared.json` registra entradas e hashes de todos os arquivos gerados; `built.json` registra DLL, hash, compilador e vínculo ao recibo de preparação. `build/plugins/tls/qrcmusicschanneloffline.dll` é o resultado. A receita não chama `cmake --install`.

São usadas APIs privadas Qt, portanto plugin e DLLs devem permanecer vinculados aos hashes da mesma compilação. Não misturar outro SDK ou distribuir isoladamente uma DLL sem fonte correspondente, licença e revisão. A fonte original e a cópia modificada retêm copyrights e cabeçalhos LGPL-3.0-only/GPL/comercial; os textos LGPL-3.0-only e GPL-3.0-only são copiados para o resultado local.

## Política e alterações

- Classe/nome próprios distinguem o plugin do `schannel` original. O chamador deverá carregar/selecionar exclusivamente no processo de download antes de criar objetos TLS. `setActiveBackend()` é global ao processo Qt; não deve trocar o backend do player.
- `CertGetCertificateChain` conserva a checagem de revogação e acrescenta `CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL`, `CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY` e `CERT_CHAIN_DISABLE_AIA`. Nenhum deles sozinho equivale à combinação: o primeiro não controla consultas de revogação.
- Credenciais Schannel usam cache-only de URL/revogação/criação e `SCH_CRED_NO_DEFAULT_CREDS` fixo. O ambiente `QT_SCH_DEFAULT_CREDS` não pode habilitar credenciais de usuário nessa cópia. O SDK não possui um flag `SCH_CRED_DISABLE_AIA`; AIA fica explicitamente desativada na construção da cadeia e as credenciais só permitem o cache. A validação manual Qt de assinatura, confiança, tempo, uso, nome e revogação conhecida é preservada.
- **Revogação desconhecida:** mantém-se a política original Qt de soft-fail para status desconhecido/offline. Aceitação não comprova status de revogação conhecido. Um resultado explícito de revogado continua gerando erro e não é ignorado. Não confundir restrição de consultas externas com uma nova prova de revogação.
- O acumulador Qt `intermediateBuffer` admite no máximo 256 KiB, com aritmética sem overflow. Leituras e pedidos `missingData` são recusados antes de `reserve`/`resize`, nas duas rotas de handshake e na descriptografia. Excesso termina apenas o socket correspondente com erro TLS. Esse limite não mede nem limita sozinho as alocações opacas do Schannel/Windows; o subprocesso precisa de Job Object com limite de memória e encerramento controlado pelo proprietário.

A compilação usa Release, C++20, `/W4 /WX`. Exceções locais para código original Qt: C4702 em fallbacks após `Q_UNREACHABLE()` e C4456 somente no parser PKCS#12 que usa escopos internos com os mesmos nomes. As verificações do código acrescentado e demais warnings permanecem ativas. Não há mudança de algoritmo para satisfazer warnings.

## Evidência e limites

Os cinco contratos do builder começaram vermelhos por ausência da implementação e passaram após a preparação restrita. Eles observam preparação válida, recusa de fonte/SDK alterados, destino fora da área e preservação de arquivos existentes. O executável C++ adicional verifica limites exatos, incremento acima do teto, números negativos e extremos de `int64_t`, sem Qt/rede.

Build real de 2026-10-01: MSVC 19.44.35228/Qt 6.11.2, em `out/product/download-tls/rcmusic-schannel-offline-20261001-03`, passou com CTest **1/1** (12 casos internos do limite) e builder **5/5**. DLL SHA256 `7dd37d34ba02f75d58bbf3738e27103e205f5ba99727da9812fa3c7f359a90b4`; dependências PE observadas são Qt Core/Network, bibliotecas Windows de criptografia e runtime MSVC/UCRT, sem OpenSSL. Parser PowerShell, sintaxe JavaScript e formato dos dois C++ próprios passaram. Os dois primeiros builds localizaram os warnings originais Qt descritos acima; suas pastas de evidência permanecem separadas. Fontes e SDK originais continuam intactos.

### Handshake real isolado

`tests/CMakeLists.txt` compila um executável Qt Test próprio e a fonte real `PinnedDownloadStream.cpp`, sem alterar o CMake da raiz. Na sessão MSVC descrita no [guia de build](../../docs/core-build.md#build-windows):

```powershell
cmake -S tools/download-tls/tests -B out/product/download-tls/handshake-tests-01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/q/f75eb9bea8280844c6d1a2b3f4/sdk"
cmake --build out/product/download-tls/handshake-tests-01 --parallel 4
$env:RC_MUSIC_TLS_TEST_PLUGIN_ROOT = "$PWD/out/product/download-tls/rcmusic-schannel-offline-20261001-03/build/plugins"
$env:RC_MUSIC_TLS_TEST_BACKEND = 'rcmusic-schannel-offline'
ctest --test-dir out/product/download-tls/handshake-tests-01 --output-on-failure
```

Somente esse processo de teste acrescenta o diretório do plugin e seleciona o backend. A confiança é por instância Qt, sem instalação no Windows. Os testes exercitam certificado confiável/GET assinado, nome errado sem GET e cadeia incompleta com AIA apontando para servidor-armadilha em `127.0.0.1`. A armadilha é subprocesso Node independente, para atender mesmo enquanto o validador nativo bloqueia a thread Qt; não faz conexões externas. Chaves/CA/intermediário/leaf são sintetizados em `QTemporaryDir` pelo OpenSSL CLI existente e removidos depois. Cada ensaio de AIA cria identidades/chaves novas para impedir que cache de execução anterior esconda consultas. Os processos filhos pertencem somente ao teste e são encerrados ao sair.

Em 2026-10-01, o mesmo contrato com `RC_MUSIC_TLS_TEST_BACKEND=schannel` falhou exatamente ao detectar **REQUEST** na armadilha AIA (2 passagens/1 falha, 988 ms). Com o backend restrito compilado, a suite passou **5/5**, sem falhas/skips, em 1.278 ms: zero pedidos à armadilha, erro TLS para issuer ausente, zero GET de mídia em autenticação inválida e confiança padrão Qt intacta. Isso comprova esse caminho de AIA local, não ausência exaustiva de qualquer tráfego Windows.

Ainda faltam CRL/OCSP/CTL com cenários próprios, handshake excessivo/stall, filas/alocações internas e cancelamento/deadline do subprocesso. Testes separados não provam o downloader composto, publicação MP3, distribuição ou a edição Completa.

Fontes primárias: [flags de cadeia Windows](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certgetcertificatechain), [credenciais Schannel](https://learn.microsoft.com/en-us/windows/win32/api/schannel/ns-schannel-sch_credentials) e [seleção de backend Qt](https://doc.qt.io/qt-6/qsslsocket.html#setActiveBackend).

## Loader no componente opcional

O [loader Windows da raiz](../../docs/download-conversion.md#runtime-tls-no-futuro-processo-de-rede) verifica arquivos/pins/runtime e seleciona exclusivamente esta DLL em processo novo de teste, sem fallback e sem trocar o backend do player. A fixture real é indicada somente com `RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN` no configure dos testes da raiz. Não instala o plugin nem altera a receita acima. Controle do carregamento inicial de DLLs, launcher diskless, reconhecimento de DNS/I/O e composição continuam pendentes; ativação validada não aprova internet, cadeia TLS ou distribuição.
