# Plano: biblioteca permanente e busca interna

**Base:** [ADR-0019](../../architecture/decisions/0019-internal-search-library-and-extension-retirement.md) e a proposta aprovada `out/product/uiux-planning-4efd8ba/BIBLIOTECA-PERMANENTE.md`.

**Estado:** em execução desde 2026-11-16, em fatias. Cada fatia termina com commit e checks próprios.

## Fatias

| Fatia | Entrega | Checks |
|---|---|---|
| L1 | Domínio `LocalLibrary` e store `library.json` v1 (escrita atômica, backup, lock, versão futura) | Catch2 do domínio; Qt Test do store |
| L2 | Ingresso local alimenta a biblioteca: Abrir, Adicionar, soltar, encaminhamento e playlist; semente aditiva da fila e das playlists; `Saved` correlacionado | binding + restart |
| L3 | Importação de download nos dois destinos (biblioteca e fila) com recibos, sem refazer download nem ressuscitar remoção | importação/retry |
| L4 | Página Biblioteca em Lista (Capas como alternativa), busca local sem acento, adicionar à fila/playlist, remover registro e localizar arquivo | Quick + modelo |
| L5 | Busca online pelo yt-dlp, só metadados, seleção virando pedido de aquisição por ID | resolvedor + UI |
| L6 | Retirada da extensão, do host Native Messaging e do empacotamento correspondente | frente própria |

## Contrato de L1

- Um registro por caminho exato (comparação de `std::filesystem::path` sem normalizar nem case-fold). Reingressar o mesmo caminho devolve o ID existente.
- O ID é próprio, estável e nunca é reaproveitado: `nextId` persiste no documento, então remover o maior ID e reiniciar não o devolve a outro arquivo.
- Remover tira só o registro. Indisponibilidade é um estado do registro, não remoção.
- Localizar troca o caminho do mesmo ID. Um caminho que já pertence a outro registro é conflito, sem fusão.
- Título, artista e álbum seguem a política de texto de linha única já usada (`sanitizeMetadataText` sem alterações) ou ficam vazios. Duração é opcional.
- Limites: 20.000 registros, 512 bytes por texto, 32.768 unidades por caminho e 24 MiB de carga útil.
- A busca sem acento fica na apresentação (L4), com `QString`, porque o núcleo não depende de Qt.
- O store copia o padrão do `QtJsonPlaylistStore`: lock cooperativo, primário mais `.bak`, versão futura nunca sobrescrita, e só `Saved` confirma.

## Contrato de L2

- `LibraryBinding` carrega e grava `library.json` num worker. O catálogo confirmado só avança com `Saved`. Ingressos que chegam durante uma gravação entram numa gravação seguinte.
- Uma falha mantém a mudança pendente até um novo ingresso ou `retrySave`, sem repetir sozinha. A saída do app espera só a gravação em andamento; uma falha nunca segura a saída.
- `PlayerPresenter::setLocalAdmissionObserver` recebe os caminhos aceitos em Abrir, Adicionar, soltar/encaminhamento e enfileirar playlist, depois que a fila confirma. Exceções do observador não desfazem a fila. Downloads ficam para L3.
- Semente de primeiro uso: só quando `library.json` não existia, uma vez da fila e uma vez das playlists (`PlaylistsBinding::entryPaths`). Depois disso, sementes são ignoradas, então um registro removido não volta.
- Biblioteca ilegível (inválida, versão futura, indisponível) nunca é sobrescrita; ingressos ficam em memória, com limite.

## Estado

- L1 concluída: commit `d4cbc8c`.
- L2 concluída: catálogo alimentado pelo ingresso local, com 704/704 CTest (sem a suíte Quick, sem mudança de QML).
