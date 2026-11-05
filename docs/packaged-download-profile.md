# Perfil de downloads com caminhos relativos ao pacote

A receita `tools/download-runtime/build_profile.py` prepara um candidato com ferramentas de downloads, um header C++ e um recibo de inventário. O header contém caminhos relativos ao futuro diretório do player, permitindo montar o pacote em outro local sem recompilar caminhos do checkout dentro dele.

Esse componente fornece os arquivos e a configuração revisável para uma montagem posterior. Não inclui `RCMusic.exe`, o runtime Qt completo do player ou um instalador, e não registra hosts nem ativa downloads públicos.

## Configuração da receita

O arquivo JSON de entrada tem exatamente estes campos:

| Campo | Contrato |
| --- | --- |
| `schemaVersion` | Inteiro `1`. |
| `output_directory` | Caminho absoluto novo, abaixo de `out/product` do repositório da própria receita. |
| `network` | Lista com exatamente nove registros `{role, path, bytes, sha256}`, um por papel da tabela abaixo. A ordem de entrada é normalizada. |
| `media` | Registro `{path, bytes, sha256}` do `RCMusicMediaWorker.exe`. |
| `youtube` | `null`, ou objeto `{receipt_path, receipt_sha256}` com caminho absoluto e pin explícito do recibo de produção YouTube. |

Os caminhos das fontes precisam ser absolutos em drive local do Windows. `bytes` é um inteiro positivo de até 128 MiB por arquivo; `sha256` tem 64 dígitos hexadecimais minúsculos e deve corresponder aos bytes observados. Campos extras, tipos incompatíveis, papéis ausentes ou duplicados são recusados.

| Papel da rede | Nome fixo |
| --- | --- |
| `Bootstrap` | `RCMusicDownloadWorker.exe` |
| `Payload` | `rcmusic-download-network.dll` |
| `Core` | `Qt6Core.dll` |
| `Network` | `Qt6Network.dll` |
| `Msvcp140` | `msvcp140.dll` |
| `Msvcp140_1` | `msvcp140_1.dll` |
| `Vcruntime140` | `vcruntime140.dll` |
| `Vcruntime140_1` | `vcruntime140_1.dll` |
| `Plugin` | `qrcmusicschanneloffline.dll` |

O [scanner estático de rede](../tools/download-runtime/README.md) fornece observações dos nove PEs com tamanhos, hashes e imports. Seu uso é explícito e separado; o gerador não inicia `dumpbin` nem executa as ferramentas examinadas. A configuração usa fontes e pins já revisados, sem descoberta no `PATH`.

## YouTube opcional

O recibo YouTube precisa ter exatamente `schemaVersion`, `purpose`, `artifact_root`, `sources` e `artifacts`, com schema inteiro `1` e propósito `production-candidate-v1`. O pin é conferido antes de interpretar o JSON. UTF-8 inválido, chaves duplicadas, documentos maiores que 8 MiB ou profundidade acima de 8 são recusados.

A lista de fontes exige os onze pins de `SOURCE_PINS`, os quatro inputs de projeto `entrypoint`, `spec`, `hook`, `build-lock` e os cinco registros de licença. `fixture` e `offline-entry` não pertencem a esse recorte. Esses registros são observações históricas do produtor: os arquivos originais de wheels, intérpretes e fontes não são reabertos para gerar o candidato.

O inventário de artefatos é validado pela função pura `onedir_plan.validate_inventory()`, incluindo os quatro papéis únicos, notices, colisões e limites de 256 arquivos, 128 MiB por arquivo e 512 MiB no conjunto YouTube. A árvore física precisa corresponder ao inventário completo. Arquivos ou diretórios extras, inclusive o marcador vazio `REQUESTED`, causam recusa e são preservados. O gerador não usa a etapa de build que remove esse marcador.

A confiança no produtor e a escolha do pin são pré-condições. Hashes e versões estabelecem consistência dos inputs selecionados; não autenticam publicador, navegador, segurança ou licença de distribuição.

## CLI e API

Use um Python local conhecido por caminho absoluto, com `-B -I`, e um arquivo de configuração absoluto já revisado. Na raiz do repositório, substitua os placeholders:

```powershell
$python = '<caminho absoluto do Python local escolhido>'
$config = '<caminho absoluto da configuração JSON revisada>'
& $python -B -I .\tools\download-runtime\build_profile.py --config $config --plan-only
```

`--plan-only` lê e valida as entradas e escreve o plano JSON em stdout, incluindo `header_text`; não cria o payload. Retire somente essa opção para materializar o candidato no `output_directory` declarado:

```powershell
& $python -B -I .\tools\download-runtime\build_profile.py --config $config
```

A CLI usa JSON com escapes ASCII na saída, preservando valores Unicode mesmo quando o terminal Windows usa outra code page. A interpretação estrita da configuração continua em UTF-8. O diretório corrente não escolhe fontes, ferramentas ou a raiz de saída autorizada.

Na API Python, o módulo conhecido fornece `plan(config)` e `materialize(candidate)`. A primeira função recebe o dicionário de configuração, inspeciona os inputs e retorna um plano próprio com `files`, `header_text`, hashes das receitas e referências do recibo YouTube. A segunda aceita esse plano, confere novamente seu conteúdo e materializa a saída. O mesmo plano determina a cópia e o header; editar o texto ou o inventário do plano causa recusa.

Cada plano carrega novamente o validador do arquivo conhecido `tools/youtube-resolver/onedir_plan.py`, sem busca de módulos no `PATH` ou cache de uma versão anterior desse arquivo. A receita não instala, atualiza ou baixa dependências.

## Layout e header C++

```text
output_directory/
  PlayerDownloadPackageProfile.hpp
  download-profile-receipt.json
  payload/
    download-tools/
      network/      # nove arquivos fixos
      media/
        RCMusicMediaWorker.exe
      youtube/      # somente quando incluído
        extractor/
        javascript/
        notices/
```

O futuro `RCMusic.exe` fica na raiz de `payload`. Por isso o header usa `download-tools/...`, sem prefixo `payload/` nem caminhos absolutos do checkout. O arquivo é um input explícito de `RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER`; o app não lê o JSON da receita ou do recibo para conceder autoridade em runtime.

O provider gerado fixa os pins da rede, Media Foundation e do resolver opcional, define `prepare_storage=true` e retorna `makePackagedPlayerDownloadProfile(std::move(profile), config_root)`. O helper C++ resolve as fontes em relação ao diretório da imagem do player. Os caminhos mutáveis vêm do [layout privado de storage](player-download-storage.md), sem repetir sua configuração no gerador.

Para YouTube presente, a composição C++ fornece ambiente novo com `PATH` vazio, `SystemRoot` obtido pela API do Windows e relógio de época explícito. O inventário não escolhe executáveis a partir do ambiente herdado.

As identidades Native vêm de `compiledPlayerNativeIngressProfiles()`. No candidato sem runtime YouTube, o provider faz opt-out explícito desse canal, preservando o canal principal mesmo se ambos estiverem compilados. O gerador não inventa ID de extensão ou origem.

## Ativação do componente instalado

O helper de pacote define `requires_installed_component=true`. Antes de criar o owner, executor, worker de checkpoint ou listeners, o bootstrap consulta em um worker o arquivo fixo `download-tools/installed-v1`, relativo à imagem real do player. O conteúdo deve ser exatamente `RCMusicDownloadTools-v1` seguido de LF, 24 bytes ASCII. Esse marcador registra a seleção do componente; não autentica publicador nem substitui a validação dos arquivos/pins pelos workers.

O leitor abre somente caminhos locais ordinários, retém ancestrais sem seguir reparse points e recusa diretórios, hardlinks ou conteúdo/tamanho diferentes. Ausência, erro ou objeto inválido deixam o player local disponível, sem provisionar storage de downloads. Não há busca no CWD, ambiente, Registro ou configuração JSON de runtime. A receita de ferramentas continua produzindo um candidato sem marcador; a futura montagem opcional deve publicá-lo após conferir os arquivos selecionados.

O probe é passivo até `start`, entrega no máximo um resultado após liberar o reader e confirmar a parada da thread. O fechamento sela a ativação permanentemente nessa execução e aguarda esse ACK, sem bloquear a UI. Um resultado atrasado não recria a integração. Após Enabled, a composição conserva o owner mesmo se `start` falhar e publica o binding no QML depois da tentativa única de início. O perfil retido possui cópias de paths, pins, identidades Native e ambiente Qt.

Perfis C++ confiáveis anteriores mantêm o padrão `requires_installed_component=false`; esse opt-in não transforma o app padrão em um downloader. O [instalador atual](windows-installer.md#checks-sem-compilação-ou-instalação) recusa o componente enquanto seleção, ownership, upgrade e remoção opcionais não forem implementados.

Evidência de 2026-11-05: Red do probe **5 passados/23 falhos**, perfil **61/2**, bootstrap **5/3**, seguido de Green **5/5 CTest, 102 casos Qt**, sem skips. São 28 do probe, 63 do perfil, 3 do perfil Native OFF e 8 do bootstrap, mais a fronteira de processos. O bootstrap preserva a jornada AAC local → MF/MP3 e replay e acrescenta ausência/corrupção do marcador e saída com reader pendente. Os negativos aguardam a leitura concluída e ticks da UI antes de avaliar ausência de binding; não passam apenas por fechar cedo. O marker da fixture usa `NEWLINE_STYLE LF`, corrigido no produtor após o primeiro ensaio encontrar CRLF/25 bytes. Logs em `out/product/test-runs/component-activation-20261105/`; os testes não instalaram o componente nem executaram mídia pública.

Formato dos **10 arquivos C++** passou. O perfil completo preservado, incluindo YouTube, compilou no player Release sem testes/Native OFF, com zero CTest. O cache foi restaurado ao perfil vazio e o player recompilado; a conferência somente leitura encontrou **337 comandos** no grafo padrão, sem o probe/perfil opcional, Native ou hooks/Qt Test. O app de produção não foi iniciado. Esse build não materializa o runtime instalado nem qualifica mídia pública.

## Preservação e limites

O destino precisa ser fresco e não pode sobrepor as fontes. Antes de abrir inputs, a receita recusa caminhos UNC, de dispositivo, ADS, componentes reservados e reparse points em qualquer ancestral. Confere metadados antes dos hashes e mantém streams dos inputs durante a cópia.

Antes de criar a saída, e novamente antes de concluir o recibo, o gerador revalida o plano, as fontes, a árvore YouTube e os hashes das receitas. Arquivos de destino usam criação sem sobrescrita; os hashes dos arquivos copiados e do header são conferidos. Falhas preservam o diretório parcial, sem rollback ou limpeza de fontes. Esses cuidados atendem à montagem local revisada; não são uma transação universal contra um host malicioso do mesmo usuário.

O recibo tem `state: candidate`, inventário do payload, hash do header, hashes das receitas e referência pinada do recibo YouTube opcional. O gerador não inspeciona PE, não executa os `.exe`/`.dll` copiados e não prova a coleção completa de dependências, sandbox, compatibilidade do sistema ou aprovação de distribuição. Registro, instalação, Brave, identidade real da extensão e aquisição pública continuam em seus próprios gates.

## Evidência da receita e candidato local

O Red inicial teve **16 métodos, 49 falhas e 1 erro** contra os stubs. Após Green dos 16 métodos, duas regressões detectaram o validador em cache associado a novos bytes de receita e a falha de stdout ASCII com paths Unicode: **16 passagens e 2 falhas**. Os fixes passaram **18/18 métodos**. As fixtures usam bytes próprios não executáveis e incluem preservação de fontes, drift, árvore fechada, plano adulterado e CLI chamada de outro diretório.

O candidato real foi gerado em `out/product/download-tools-20261105-01` e teve inventário físico conferido independentemente:

- **45 arquivos / 133.468.285 bytes no payload**: nove de rede, um worker MF e 35 do runtime YouTube de produção preservado;
- **47 arquivos no diretório completo**, incluindo header e recibo;
- SHA-256 de `download-profile-receipt.json`: `22b5999ab46ecc4316e21bd635d0e23f326bfc396adaac8c6b94126d8689d3de`;
- SHA-256 de `PlayerDownloadPackageProfile.hpp`: `0026002f082a87ae2d0f45a0b35da0938de007dbfd00404593f3c187262dc56b`.

Os três targets de ferramentas de produto — bootstrap de rede, payload de rede e worker Media Foundation — foram compilados com `BUILD_TESTING=OFF`. O relatório estático dos nove arquivos de rede usou `dumpbin` somente para leitura, por caminho explícito. O contexto dos inputs, logs Python e conferência do candidato ficam em `out/product/test-runs/packaged-download-profile-20261105/`.

O helper C++ teve Red **55 passados/5 falhos** e Green **60/60 Qt**, preservando os 28 anteriores. O fechamento passou **4/4 CTest, 68 Qt**, sem falhas/skips: perfil 60, sem-header 3, bootstrap 5 e fronteira de processos. O `main` da fixture compila o header real da receita em modo direto, com ambos os canais Native compilados ativos; o opt-out YouTube conserva a principal. O processo inicia com CWD no TEMP, encontra suas cópias relativas e mantém a conversão AAC local → MP3, Session/checkpoint Saved e replay em dois boots sem duplicar/autoplay. Não executa o runtime YouTube nessa jornada.

Formato dos **5 C++**, incluindo o template, passou. O header completo do candidato real, com os 35 arquivos YouTube, também compilou no player Release `BUILD_TESTING=OFF`/Native OFF. O cache foi restaurado ao header vazio e o app recompilado; zero CTest e grafo final de **337 comandos**, sem perfil, Native ou hooks/Qt Test. O executável de produção não foi iniciado. Esses resultados não são transferidos para aquisição pública ou runtime instalado.

A fixture usa Python 3.13+ somente durante o build de testes desse recorte; `Python3_EXECUTABLE` pode selecionar explicitamente o intérprete conhecido. Hashes e geração ocorrem depois dos targets, sem pin do próprio player ou ciclo de dependências. A saída não materializada de `--plan-only` fica sob a raiz autorizada da receita, independente do diretório de build. Uma verificação adicional gerou os headers fora de `out/product`, sem materializar o candidato; as cópias próprias foram preservadas intactas em `out/product/test-runs/packaged-download-profile-20261105/non-product-build-fixture/`, com o local original registrado na evidência.

Essa geração não iniciou o runtime YouTube/Node/EJS nem executou transferência ou conversão pelos binários do candidato completo. Montagem do app com Qt, dependências PE (inclusive `.pyd`), licenças, seleção opcional/ownership do instalador, identidade real Brave e aquisição pública permanecem etapas separadas.
