# room-signaling

Servidor de apresentação da sala de escuta do RC Music ([ADR-0022](../../docs/architecture/decisions/0022-listening-room.md)). É um Worker da Cloudflare com um Durable Object `RoomRelay` por sala. Ele só repassa envelopes cifrados ponta a ponta entre o host e os convidados enquanto a sala está aberta.

O protocolo, os limites, a privacidade e a evidência estão em [docs/room-signaling.md](../../docs/room-signaling.md).

## Arquivos

- `src/index.ts`: rotas e validação de `room_id` e do token do host;
- `src/RoomRelay.ts`: Durable Object com a API de hibernação, o relay, os limites e o alarme;
- `src/joinPage.ts`: página `/j/<room_id>`, "Abra no RC Music";
- `test/relay.test.ts`: testes no pool de Workers (workerd local);
- `worker-configuration.d.ts`: tipos gerados por `npx wrangler types`, que roda offline.

## Comandos

Dentro deste diretório:

```powershell
npm ci --ignore-scripts
npx vitest run
npx tsc --noEmit
```

Os testes rodam no workerd local, sem contato com a Cloudflare.

## Deploy (só o mantenedor)

1. Crie a conta gratuita da Cloudflare (plano Workers Free, sem cartão).
2. `npx wrangler login`
3. `npx wrangler deploy`
4. Copie a URL `*.workers.dev` exibida no fim do deploy para a configuração de build `RC_MUSIC_ROOM_SIGNALING_ORIGIN`.

O agente não faz o deploy. Não ative Workers Logs, Logpush nem `wrangler tail` neste Worker.
