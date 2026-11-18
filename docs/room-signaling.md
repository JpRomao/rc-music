# Servidor de apresentação da sala de escuta

O servidor fica em [`services/room-signaling`](../services/room-signaling/README.md). É um Worker da Cloudflare com um Durable Object `RoomRelay` por sala. Enquanto uma sala está aberta, ele repassa as mensagens de apresentação entre o host e os convidados. Essas mensagens já chegam cifradas ponta a ponta. O servidor não recebe a música e não guarda o conteúdo nem a sala depois que ela fecha. É a única exceção de rede do [ADR-0022](architecture/decisions/0022-listening-room.md) ao local-first. Faz parte da fatia S5 do [plano da sala](superpowers/plans/2026-11-17-listening-room.md).

O código existe e passa nos testes locais. **Ainda não houve deploy**: criar a conta e publicar são passos do mantenedor. O transporte C++ da S6.3 (`DataChannelRoomHostEndpoint` e `DataChannelRoomGuestConnector`) fala este protocolo pelo WinHTTP. Ele foi testado só contra o servidor local `tests/support/LoopbackSignaling.hpp`, que reproduz as rotas, os códigos e os limites daqui. A integração no app fica para a S7.

## Protocolo

| Rota | Uso | Respostas de erro |
|---|---|---|
| `GET /rooms/<room_id>/host` | Upgrade WebSocket do host, com o header `X-Room-Host-Token` | 400 token ausente ou malformado; 409 outro token já é dono da sala; 426 sem upgrade |
| `GET /rooms/<room_id>/guest` | Upgrade WebSocket de um convidado; o objeto atribui um `peer` (u32) | 426 sem upgrade; 429 sala cheia |
| `GET /j/<room_id>` | Página "Abra no RC Music", com o botão **Copiar o link** | — |
| qualquer outra rota ou método | — | 404 |

- `room_id` tem exatamente 22 caracteres base64url canônicos: são os 16 bytes do `room_id` do `RoomInviteLink`, sem padding. Os 4 bits que sobram no último caractere são zero, então ele só pode ser `A`, `Q`, `g` ou `w`. Qualquer outro valor recebe 404.
- `X-Room-Host-Token` tem exatamente 43 caracteres base64url canônicos: 32 bytes aleatórios, sem padding. O último caractere tem os 2 bits de sobra zerados. O primeiro host grava só o `SHA-256` do token, em hexadecimal. As conexões seguintes são comparadas em tempo constante. Um host com o mesmo token substitui o anterior, que é fechado com o código 4001.
- Envelopes são texto JSON com exatamente as chaves `to` e `data`. `data` é o bloco cifrado em base64 ou base64url.
  - O convidado envia `{"to":"host","data":…}`, e o host recebe `{"from":<peer>,"data":…}`.
  - O host envia `{"to":<peer>,"data":…}`, e só esse convidado recebe `{"from":"host","data":…}`.
  - São descartados, sem eco: envelope de convidado para outro convidado, envelope do host para um peer desconhecido, JSON inválido, chaves faltando ou sobrando, `data` vazio ou fora de base64, e frames binários.
- Eventos do servidor:
  - `{"event":"peer-left","peer":<peer>}` vai para o host quando um convidado sai;
  - `{"event":"host-offline"}` vai para os convidados quando o host cai, e também para o convidado que entra numa sala sem host.
- Os peers começam em 0 e não se repetem enquanto a sala existir: o contador fica no armazenamento do objeto.

## Conteúdo dos envelopes no app (S6.3)

O servidor não interpreta o `data`. No app, `data` é o base64 de `seal(JSON)`, com a cifra AES-256-GCM derivada do segredo do convite (`WindowsRoomEnvelopeCipher`). O JSON é `{"room_protocol_version":<u32>,"kind":"offer"|"answer"|"reject","sdp":<texto>}`, e o `reject` não tem `sdp`.

- Não há ICE trickle. Cada lado espera o fim da coleta de candidatos e manda uma única descrição, então a apresentação usa uma mensagem em cada sentido.
- O convidado oferece e cria os canais `control` e `media`. Antes de aplicar a resposta, confere se todo `a=fingerprint` é o `sha-256` da impressão digital do host que está no convite. Se não for, a conexão é recusada.
- Quem recebe outra `room_protocol_version` falha com `VersionMismatch`. O host responde com um `reject` que leva a versão dele.
- O token do host vem de `roomHostToken(identidade, room_id)`. Um token vazio é falha local e nunca é enviado.

## Limites

- **16 KiB por mensagem**, contados em bytes UTF-8. Uma mensagem maior fecha o socket com 1009.
- **20 mensagens por segundo por socket.** A 21ª no mesmo segundo é descartada. O contador fica só na memória do objeto. Se o objeto hibernar, a contagem recomeça, o que no máximo libera uma janela nova.
- **11 sockets por sala:** até 10 convidados e uma vaga reservada ao host. O 11º convidado recebe 429 antes do upgrade, mesmo sem host na sala, para que o host sempre consiga voltar.
- **10 minutos sem host:** o prazo começa quando o host cai ou quando o primeiro convidado entra numa sala sem host. Ao fim, um alarme fecha todos os sockets com 1001 e apaga o armazenamento, inclusive o hash do token. Enquanto houver algo no armazenamento, há sempre um alarme pendente. Com o host conectado, ou de volta antes do prazo, o alarme não apaga nada e só se rearma para dali a 10 minutos. Se o objeto reiniciar com o host conectado (deploy, despejo do runtime, troca de máquina), os sockets caem sem eventos de saída. Mesmo assim, o alarme seguinte apaga tudo, no pior caso até 20 minutos depois da queda. Depois de apagada, a sala aceita um novo token. A proteção do conteúdo continua sendo a cifragem com o segredo do convite.

## Privacidade

- O código em `src/` não usa `console`, e um teste garante isso. Nada é registrado: nem IP, nem headers, nem conteúdo.
- O Worker repassa ao Durable Object uma requisição nova, só com o papel e o token. O IP e os outros headers do cliente não chegam ao objeto.
- `wrangler.toml` desliga Workers Logs e os logs de invocação. Também desliga as métricas de uso do Wrangler (`send_metrics = false`) e o envio da lista de dependências no deploy (`dependencies_instrumentation`).
- O fragmento do convite (`#segredo.impressão`) nunca é enviado ao servidor. A página `/j/` não repete nada da requisição, nem o `room_id`. O botão copia `location.href` no próprio navegador.
- A página tem só script e estilo inline, liberados por hash na CSP `default-src 'none'`. Também envia `Referrer-Policy: no-referrer`, `Cache-Control: no-store` e `X-Content-Type-Options: nosniff`.

## Comandos locais

Use Node.js já disponível (validado com v24.20.0 e npm 11.19.0). Rode dentro de `services/room-signaling`:

```powershell
npm ci --ignore-scripts
npx vitest run
npx tsc --noEmit
```

- `npm ci --ignore-scripts` instala só as quatro dependências de desenvolvimento fixadas no `package-lock.json`, sem rodar scripts de instalação.
- `npx vitest run` roda os testes no pool de Workers: workerd/Miniflare local, sem contato com a Cloudflare.
- `npx tsc --noEmit` confere os tipos.

Os tipos do runtime estão em `worker-configuration.d.ts`, gerado offline por `npx wrangler types`. Depois de mudar o `wrangler.toml` ou a versão do Wrangler, gere o arquivo de novo e faça o commit.

Esses comandos são deste componente e não fazem parte do build CMake do player.

## Deploy (só o mantenedor)

O agente não faz o deploy. O mantenedor:

1. cria a conta gratuita da Cloudflare (plano Workers Free, sem cartão);
2. dentro de `services/room-signaling`, roda `npx wrangler login`;
3. roda `npx wrangler deploy`;
4. copia a URL `https://<nome>.<subdomínio>.workers.dev` exibida no fim do deploy para a variável de cache `RC_MUSIC_ROOM_SIGNALING_ORIGIN` ao configurar o build do produto, por exemplo `cmake -DRC_MUSIC_ROOM_SIGNALING_ORIGIN=https://<nome>.<subdomínio>.workers.dev ...`. Ela vira uma definição de compilação só do `m0_qt_shell` e aceita `https://<host>` (ou `http://127.0.0.1:<porta>` para um relay local). O padrão é vazio: a página **Ouvir junto** mostra "Sala indisponível neste build" e o app não cria identidade, transporte nem conexão. Os shells de teste nunca recebem a origem.

No plano gratuito, ao passar dos limites diários, as salas novas falham até o dia seguinte (UTC), sem cobrança. Não ative Workers Logs, Logpush nem `wrangler tail` neste Worker.

## Dependências

Autorizadas pelo mantenedor em 2026-11-17, fixadas com versão exata e baixadas de `registry.npmjs.org`: 79 pacotes, cerca de 253 MB em `node_modules`. São só ferramentas de desenvolvimento. O Worker publicado não tem dependências npm em runtime.

| Pacote | Versão | Licença |
|---|---|---|
| `wrangler` | 4.124.0 | MIT OR Apache-2.0 |
| `@cloudflare/vitest-pool-workers` | 0.23.0 | MIT |
| `vitest` | 4.1.12 | MIT |
| `typescript` | 5.9.3 | Apache-2.0 |

O npm marca `@cloudflare/vitest-pool-workers` como renomeado para `@cloudflare/vitest-plugin`, que não recebe mais atualizações. A troca precisa de nova autorização.

## Evidência

S5.1, em 2026-11-17: Red **0/26**, com stubs que respondem 501. Depois, Green **26/26** em `npx vitest run`, repetido três vezes, e `npx tsc --noEmit` limpo. Um teste de substituição do host pelo mesmo token entrou depois do Green e passou de primeira. O fechamento ficou em **27/27**, depois de um `npm ci --ignore-scripts` limpo. Os testes são locais no workerd. Não houve deploy nem teste contra a Cloudflare real, e nenhum cliente C++ foi conectado.

Correção 1 da S5.1: o alarme fica sempre armado enquanto houver estado. Red **26/29**: os 3 testes novos falharam, inclusive o reinício do objeto com `evictDurableObject(..., { webSockets: "close" })`, que deixava o estado sem alarme. Green **29/29**, duas vezes, e `npx tsc --noEmit` limpo.
