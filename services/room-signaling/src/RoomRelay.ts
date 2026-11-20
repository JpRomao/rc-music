// Durable Object de uma sala de escuta: repassa envelopes cifrados entre o host e os convidados.
//
// O conteúdo é opaco: o servidor só confere o formato `{to, data}`, troca `to` por `from` e
// entrega. Nada é registrado. O armazenamento guarda só SHA-256(token do host) e o próximo peer.
//
// Limites (ADR-0022):
// - 16 KiB por mensagem, em bytes UTF-8 (texto) ou bytes (binário); acima disso fecha com 1009;
// - 20 mensagens por segundo por socket; a 21ª no mesmo segundo é descartada. O contador fica só
//   em memória e se perde quando o objeto hiberna, o que apenas libera uma janela nova;
// - 11 sockets: até 10 convidados e uma vaga reservada ao host. O 11º convidado é recusado com
//   429 antes do upgrade, haja host ou não, e um host com o mesmo token substitui o anterior;
// - 10 minutos sem host (depois que o host cai, ou depois que o primeiro convidado entra numa sala
//   sem host): o alarme fecha todos os sockets e apaga o armazenamento.
//
// Enquanto houver algo no armazenamento, há sempre um alarme pendente. Com o host conectado, o
// alarme só se rearma para daqui a 10 minutos. Assim, se o objeto reiniciar com o host conectado
// (deploy, despejo, troca de máquina), os sockets caem sem eventos de saída, e o alarme seguinte
// ainda apaga tudo: o alarme nunca fica mais de 10 minutos à frente, então no pior caso é 10 minutos
// depois da queda.
//
// Usa a API de hibernação: o papel, o peer e um id de conexão ficam em `serializeAttachment`.

import { DurableObject } from "cloudflare:workers";

export const HOST_TOKEN_HEADER = "X-Room-Host-Token";
/** Origem fictícia das requisições internas do Worker para o objeto. */
export const RELAY_ORIGIN = "https://room-relay.invalid";

const MAX_MESSAGE_BYTES = 16 * 1024;
const MAX_MESSAGES_PER_WINDOW = 20;
const RATE_WINDOW_MS = 1000;
const MAX_GUESTS = 10;
const HOST_ABSENCE_MS = 10 * 60 * 1000;
const MAX_PEER = 0xffffffff;

const CLOSE_TOO_BIG = 1009;
const CLOSE_ROOM_CLOSED = 1001;
const CLOSE_HOST_REPLACED = 4001;

const HOST_TAG = "host";
const GUEST_TAG = "guest";
const peerTag = (peer: number): string => `peer:${peer}`;

const TOKEN_HASH_KEY = "hostTokenSha256";
const NEXT_PEER_KEY = "nextPeer";

/** Base64 padrão ou base64url, com padding opcional. */
const CIPHERTEXT = /^[A-Za-z0-9+/_-]+={0,2}$/;

type Attachment =
  | { readonly role: "host"; readonly connection: string }
  | { readonly role: "guest"; readonly connection: string; readonly peer: number };

interface Envelope {
  readonly to: unknown;
  readonly data: string;
}

interface RateWindow {
  start: number;
  count: number;
}

const status = (code: number): Response => new Response(null, { status: code });

function attachmentOf(ws: WebSocket): Attachment | null {
  return ws.deserializeAttachment() as Attachment | null;
}

function isOpen(ws: WebSocket): boolean {
  return ws.readyState === WebSocket.OPEN;
}

function closeQuietly(ws: WebSocket, code: number, reason: string): void {
  try {
    ws.close(code, reason);
  } catch {
    // Já fechado.
  }
}

function sendQuietly(sockets: WebSocket[], message: string): void {
  for (const ws of sockets) {
    try {
      ws.send(message);
    } catch {
      // Fechando: o evento de saída cuida dele.
    }
  }
}

function byteLength(message: string | ArrayBuffer): number {
  if (typeof message !== "string") return message.byteLength;
  if (message.length > MAX_MESSAGE_BYTES) return message.length; // UTF-8 tem pelo menos 1 byte por unidade.
  return new TextEncoder().encode(message).byteLength;
}

function parseEnvelope(message: string): Envelope | null {
  let value: unknown;
  try {
    value = JSON.parse(message);
  } catch {
    return null;
  }
  if (typeof value !== "object" || value === null || Array.isArray(value)) return null;
  const keys = Object.keys(value);
  if (keys.length !== 2 || !keys.includes("to") || !keys.includes("data")) return null;
  const { to, data } = value as { to: unknown; data: unknown };
  if (typeof data !== "string" || !CIPHERTEXT.test(data)) return null;
  return { to, data };
}

function isPeer(value: unknown): value is number {
  return typeof value === "number" && Number.isInteger(value) && value >= 0 && value <= MAX_PEER;
}

async function sha256Hex(text: string): Promise<string> {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)));
  return Array.from(digest, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function sameDigest(left: string, right: string): boolean {
  const encoder = new TextEncoder();
  const a = encoder.encode(left);
  const b = encoder.encode(right);
  return a.byteLength === b.byteLength && crypto.subtle.timingSafeEqual(a, b);
}

export class RoomRelay extends DurableObject<Env> {
  private readonly rates = new Map<string, RateWindow>();
  private readonly departed = new Set<string>();

  override async fetch(request: Request): Promise<Response> {
    const role = new URL(request.url).pathname;
    if (role === "/host") return this.acceptHost(request.headers.get(HOST_TOKEN_HEADER) ?? "");
    if (role === "/guest") return this.acceptGuest();
    return status(404);
  }

  override async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    if (byteLength(message) > MAX_MESSAGE_BYTES) {
      closeQuietly(ws, CLOSE_TOO_BIG, "mensagem acima de 16 KiB");
      return;
    }
    const sender = attachmentOf(ws);
    if (sender === null || !this.withinRate(sender.connection)) return;
    if (typeof message !== "string") return;
    const envelope = parseEnvelope(message);
    if (envelope === null) return;

    if (sender.role === "guest") {
      if (envelope.to !== "host") return;
      sendQuietly(this.ctx.getWebSockets(HOST_TAG), JSON.stringify({ from: sender.peer, data: envelope.data }));
      return;
    }
    if (!isPeer(envelope.to)) return;
    sendQuietly(this.ctx.getWebSockets(peerTag(envelope.to)), JSON.stringify({ from: "host", data: envelope.data }));
  }

  override async webSocketClose(ws: WebSocket): Promise<void> {
    await this.leave(ws);
  }

  override async webSocketError(ws: WebSocket): Promise<void> {
    await this.leave(ws);
  }

  override async alarm(): Promise<void> {
    if (this.liveHosts().length > 0) {
      await this.ctx.storage.setAlarm(Date.now() + HOST_ABSENCE_MS);
      return;
    }
    for (const ws of this.ctx.getWebSockets()) closeQuietly(ws, CLOSE_ROOM_CLOSED, "sala encerrada");
    this.rates.clear();
    await this.ctx.storage.deleteAll();
  }

  private async acceptHost(token: string): Promise<Response> {
    const digest = await sha256Hex(token);
    // Sem await entre a leitura e a escrita: dois hosts simultâneos não disputam o primeiro token.
    const stored = this.ctx.storage.kv.get<string>(TOKEN_HASH_KEY);
    if (stored === undefined) this.ctx.storage.kv.put(TOKEN_HASH_KEY, digest);
    else if (!sameDigest(stored, digest)) return status(409);

    const previous = this.ctx.getWebSockets(HOST_TAG);
    const client = this.accept([HOST_TAG], { role: "host", connection: crypto.randomUUID() });
    for (const old of previous) closeQuietly(old, CLOSE_HOST_REPLACED, "host substituído");
    await this.ctx.storage.setAlarm(Date.now() + HOST_ABSENCE_MS);
    return new Response(null, { status: 101, webSocket: client });
  }

  private async acceptGuest(): Promise<Response> {
    // Contagem, peer e aceite sem await no meio: convidados simultâneos não furam o limite.
    if (this.ctx.getWebSockets(GUEST_TAG).filter(isOpen).length >= MAX_GUESTS) return status(429);
    const peer = this.ctx.storage.kv.get<number>(NEXT_PEER_KEY) ?? 0;
    if (peer > MAX_PEER) return status(503);
    this.ctx.storage.kv.put(NEXT_PEER_KEY, peer + 1);
    const client = this.accept([GUEST_TAG, peerTag(peer)], { role: "guest", connection: crypto.randomUUID(), peer });

    if (this.liveHosts().length === 0) {
      sendQuietly(this.ctx.getWebSockets(peerTag(peer)), JSON.stringify({ event: "host-offline" }));
    }
    await this.ensureAlarm();
    return new Response(null, { status: 101, webSocket: client });
  }

  private accept(tags: string[], attachment: Attachment): WebSocket {
    const pair = new WebSocketPair();
    const [client, server] = [pair[0], pair[1]];
    this.ctx.acceptWebSocket(server, tags);
    server.serializeAttachment(attachment);
    return client;
  }

  private async leave(ws: WebSocket): Promise<void> {
    closeQuietly(ws, 1000, "");
    const who = attachmentOf(ws);
    if (who === null || this.departed.has(who.connection)) return;
    this.departed.add(who.connection);
    this.rates.delete(who.connection);

    if (who.role === "guest") {
      sendQuietly(this.ctx.getWebSockets(HOST_TAG), JSON.stringify({ event: "peer-left", peer: who.peer }));
      return;
    }
    if (this.liveHosts(who.connection).length > 0) return; // Foi substituído por outro host.
    sendQuietly(this.ctx.getWebSockets(GUEST_TAG), JSON.stringify({ event: "host-offline" }));
    await this.ctx.storage.setAlarm(Date.now() + HOST_ABSENCE_MS);
  }

  /** Arma o alarme se não houver um; nunca o adia. */
  private async ensureAlarm(): Promise<void> {
    if ((await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(Date.now() + HOST_ABSENCE_MS);
  }

  private liveHosts(except?: string): WebSocket[] {
    return this.ctx
      .getWebSockets(HOST_TAG)
      .filter((ws) => isOpen(ws) && attachmentOf(ws)?.connection !== except);
  }

  private withinRate(connection: string): boolean {
    const now = Date.now();
    const window = this.rates.get(connection);
    if (window === undefined || now - window.start >= RATE_WINDOW_MS) {
      this.rates.set(connection, { start: now, count: 1 });
      return true;
    }
    window.count += 1;
    return window.count <= MAX_MESSAGES_PER_WINDOW;
  }
}
