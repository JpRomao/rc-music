# Armazenamento privado do bootstrap de downloads

Este incremento prepara os diretórios necessários ao perfil opcional de downloads. Não ativa um perfil, registra hosts ou instala ferramentas. O player padrão continua sem composição de downloads.

## Configuração e layout

`WindowsPlayerDownloadProfile::prepare_storage` é opt-in de composição C++, falso por padrão. O provider recebe o `config_root` já escolhido pelo player. Quando a opção está ligada, os caminhos de persistência, runtimes e workspace usam um único layout fixo:

```text
config_root/
  downloads/
    state/
    network/
    media/
    youtube/
    staging/
    published/
```

`state` reúne checkpoint e journal de ownership. Os outros diretórios são os pais dos runtimes privados por job, do staging e das publicações MP3. O opt-in substitui esses caminhos no perfil, preservando pins, limites, backend de conversão e demais opções. Não cria um resolver YouTube ausente nem altera identidades Native Messaging.

`windowsDownloadStoragePaths()` deriva os caminhos sem abrir arquivos ou consultar o drive. A validação física pertence ao preparador na worker. Nenhuma URL, página, pedido da extensão ou documento persistido escolhe esses nomes.

## Preparação e persistência

`prepareWindowsDownloadStorage()` requer um `config_root` existente em drive local suportado e sem reparse points. Ele cria somente `downloads` e seus seis filhos fixos. Não cria ancestrais ausentes e não altera as permissões do diretório de configuração já existente.

Novos diretórios recebem a mesma política privada exigida pelos documentos: owner atual, DACL protegida e exatamente duas ACEs de acesso completo, para usuário atual e SYSTEM, sem herança. Diretórios existentes precisam satisfazer essa política. Arquivos em colisão, permissões amplas, reparse points e falhas de acesso são recusados. Não há reparo de ACL, exclusão ou rollback; qualquer progresso parcial fica preservado.

O preparador reutiliza a política dos documentos Windows. O helper de runtimes com ACEs herdáveis não atende ao contrato do checkpoint e não é usado nessa preparação. Pins e identidades limitam a operação local durante o preparo; não atestam publisher, sandbox, árvore atômica ou resistência a um host malicioso do mesmo usuário.

Os pins do preparador mantêm o pai existente e os diretórios preparados sem solicitar acesso `DELETE`. Isso permite conservar o root protegido enquanto os filhos abrem seus próprios pins de ancestralidade. Identidades e permissões são revalidadas antes do retorno; os handles são liberados antes do uso pelos stores. O pin de diretório dos documentos existentes, que atende às operações de rename, e o comportamento dos stores antigos permanecem inalterados.

`makePlayerDownloadCheckpointFactory()` captura cópias próprias das strings. Criar a factory ou construir seu store não provisiona diretórios. O primeiro `load()` executado pelo worker prepara o storage; falha retorna `Unreadable`, sem confirmar admissão. Um novo `load()` explícito pode tentar novamente no mesmo owner. Depois do primeiro preparo bem-sucedido, o wrapper não reprovisiona: os stores continuam validando seus próprios objetos e permissões em cada operação. `save()` antes de um preparo bem-sucedido falha sem criar pastas.

Com o opt-in desligado, a factory usa o store e o caminho existentes, sem chamar o preparador. A preparação de saída e a drenagem continuam pertencendo ao owner e aos workers existentes. O provisionamento não fornece um novo ACK de publicação, importação ou saída.

## Pasta de destino escolhida pelo usuário

**Preferências → Downloads → Alterar pasta** permite escolher um diretório local existente no mesmo volume do armazenamento temporário. **Abrir pasta de downloads** abre o destino atual no Explorador. A escolha vale para novos trabalhos de transferência. Os MP3 anteriores permanecem nos caminhos já registrados na fila e nas playlists.

O staging e os documentos de estado continuam na pasta privada. O journal de ownership registra o caminho de destino personalizado para permitir reabrir o pai que aquele trabalho usou. A recuperação confirma a identidade física do diretório antes de aceitar a prova existente de propriedade e conteúdo. Registros antigos continuam usando o destino padrão original.

Se o destino deixar de estar disponível, escolha outra pasta nas preferências. Novas transferências preservam o destino selecionado até que o usuário o altere.

## Evidência do adaptador

O Red observado de `WindowsDownloadStorageTests` teve **16 passagens e 6 falhas** contra os stubs; o Green passou **22/22 Qt**, sem falhas ou skips. Os três casos de junction em TEMP próprio — no pai, no root de downloads e em `state` — foram executados sem skip e preservaram seus alvos.

Os testes verificam o layout puro, criação com DACL exata, uso do mesmo `state` pelos stores reais de checkpoint e ownership, reutilização idempotente, preservação de arquivos e progresso parcial, recusas de ACL herdável/ampla, colisões e caminhos inválidos. O parent ausente não é criado recursivamente.

O perfil teve Red **21 passados/7 falhos** e Green **28/28 Qt**; o target sem header conservou **3/3**. A jornada pelo `main` teve Red **4 passados/1 falho**, por `downloads/state` ausente, e Green **5/5**. O driver não preprovisiona as pastas geridas. O app prepara o layout, admite o pedido, converte AAC local em MP3 com Media Foundation e persiste checkpoint/Session; o segundo boot reconhece replay sem outro arquivo, importação ou autoplay. As verificações de todos os diretórios ocorrem após o resultado terminal, para não interpretar uma pasta intermediária como conclusão do preparo.

O fechamento proporcional passou **8/8 CTest, 143 Qt**, sem falhas. Dois casos antigos de symlink do checkpoint foram pulados pela política do host; os três junctions novos passaram sem skip. A seleção inclui os stores de checkpoint, ownership e perfil de mídia, perfil do bootstrap, jornada pelo `main` e fronteira de processos. Formato dos **9 C++**, incluindo o template, e build Release com Native Messaging OFF passaram. O player OFF compilou com provider independente `None` e depois foi restaurado/recompilado com cache vazio; registrou zero CTest. O app de produção não foi iniciado. Recibos: `out/product/test-runs/download-storage-20261105/`.

## Limites

Esse preparo é um pré-requisito do perfil distribuível. Ferramentas, inventários e ambiente revisados, identidade da extensão, manifest Native real, integração opcional no instalador, aquisição pública e Letras primário continuam em suas próprias etapas. Não há AppContainer, UAC, Registro, rede ou instalação nessa operação de diretórios.
