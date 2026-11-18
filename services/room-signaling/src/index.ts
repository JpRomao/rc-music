// Worker do servidor de apresentação da sala de escuta (ADR-0022).
// Rotas aceitas; qualquer outra coisa recebe 404:
//   GET /rooms/<room_id>/host   upgrade WebSocket do host, com X-Room-Host-Token;
//   GET /rooms/<room_id>/guest  upgrade WebSocket de um convidado;
//   GET /j/<room_id>            página "Abra no RC Music".
// Privacidade: nada é registrado (sem logs), e o Durable Object recebe uma requisição nova só
// com o papel e o token, sem IP nem outros headers do cliente.

import { joinPage } from "./joinPage";
import { HOST_TOKEN_HEADER, RELAY_ORIGIN } from "./RoomRelay";

export { RoomRelay } from "./RoomRelay";

// room_id: os 16 bytes do RoomInviteLink do app em base64url sem padding = 22 caracteres. Os 4 bits
// que sobram no último caractere são zero na forma canônica, então ele só pode ser A, Q, g ou w.
const ROOM_ID = /^[A-Za-z0-9_-]{21}[AQgw]$/;
// Token do host: 32 bytes aleatórios em base64url sem padding = 43 caracteres; sobram 2 bits zero.
const HOST_TOKEN = /^[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$/;

const ROOM_ROUTE = /^\/rooms\/([^/]+)\/(host|guest)$/;
const JOIN_ROUTE = /^\/j\/([^/]+)$/;

const status = (code: number): Response => new Response(null, { status: code });

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    if (request.method !== "GET") return status(404);
    const path = new URL(request.url).pathname;

    const join = JOIN_ROUTE.exec(path);
    if (join !== null) return ROOM_ID.test(join[1] ?? "") ? joinPage() : status(404);

    const room = ROOM_ROUTE.exec(path);
    if (room === null) return status(404);
    const [, roomId = "", role = ""] = room;
    if (!ROOM_ID.test(roomId)) return status(404);
    if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") return status(426);

    const headers = new Headers({ Upgrade: "websocket" });
    if (role === "host") {
      const token = request.headers.get(HOST_TOKEN_HEADER);
      if (token === null || !HOST_TOKEN.test(token)) return status(400);
      headers.set(HOST_TOKEN_HEADER, token);
    }

    const relay = env.ROOM_RELAY.get(env.ROOM_RELAY.idFromName(roomId));
    return relay.fetch(new Request(`${RELAY_ORIGIN}/${role}`, { headers }));
  },
} satisfies ExportedHandler<Env>;
