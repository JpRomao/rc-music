import { SELF, env, evictDurableObject, runDurableObjectAlarm, runInDurableObject } from "cloudflare:test";
import { describe, expect, it } from "vitest";

const ORIGIN = "https://sala.example";
const MAX_MESSAGE_BYTES = 16 * 1024;
const TEN_MINUTES_MS = 10 * 60 * 1000;

function base64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

function randomBase64Url(length: number): string {
  return base64Url(crypto.getRandomValues(new Uint8Array(length)));
}

const newRoomId = (): string => randomBase64Url(16);
const newHostToken = (): string => randomBase64Url(32);
const data = (text: string): string => btoa(text);

type Json = Record<string, unknown>;

class Client {
  readonly received: unknown[] = [];
  readonly closed: Promise<number>;
  private waiter: (() => void) | undefined;

  constructor(readonly ws: WebSocket) {
    this.closed = new Promise((resolve) => {
      ws.addEventListener("close", (event) => resolve(event.code));
    });
    ws.addEventListener("message", (event) => {
      this.received.push(typeof event.data === "string" ? JSON.parse(event.data) : event.data);
      this.waiter?.();
    });
    ws.accept();
  }

  send(message: unknown): void {
    this.ws.send(typeof message === "string" ? message : JSON.stringify(message));
  }

  async next(timeoutMs = 2000): Promise<Json> {
    const deadline = Date.now() + timeoutMs;
    while (this.received.length === 0) {
      const remaining = deadline - Date.now();
      if (remaining <= 0) throw new Error("nenhuma mensagem recebida no prazo");
      await new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, remaining);
        this.waiter = () => {
          clearTimeout(timer);
          resolve();
        };
      });
      this.waiter = undefined;
    }
    return this.received.shift() as Json;
  }

  async expectSilence(ms = 150): Promise<void> {
    await new Promise((resolve) => setTimeout(resolve, ms));
    expect(this.received).toEqual([]);
  }

  async closeAndWait(): Promise<void> {
    this.ws.close(1000, "fim");
    await this.closed;
  }
}

function upgrade(roomId: string, role: string, headers: Record<string, string> = {}): Promise<Response> {
  return SELF.fetch(`${ORIGIN}/rooms/${roomId}/${role}`, { headers: { Upgrade: "websocket", ...headers } });
}

async function accepted(response: Response): Promise<Client> {
  expect(response.status).toBe(101);
  const ws = response.webSocket;
  if (ws === null) throw new Error("resposta sem WebSocket");
  return new Client(ws);
}

const openHost = async (roomId: string, token: string): Promise<Client> =>
  accepted(await upgrade(roomId, "host", { "X-Room-Host-Token": token }));
const openGuest = async (roomId: string): Promise<Client> => accepted(await upgrade(roomId, "guest"));

/** Guest says hello to the host so the host learns the guest's peer id. */
async function introduce(host: Client, guest: Client): Promise<number> {
  guest.send({ to: "host", data: data("oi") });
  const envelope = await host.next();
  expect(envelope).toEqual({ from: expect.any(Number), data: data("oi") });
  return envelope.from as number;
}

function roomStub(roomId: string): DurableObjectStub {
  return env.ROOM_RELAY.get(env.ROOM_RELAY.idFromName(roomId));
}

const alarmOf = (roomId: string): Promise<number | null> =>
  runInDurableObject(roomStub(roomId), (_instance, state) => state.storage.getAlarm());

const storageOf = (roomId: string): Promise<[string, unknown][]> =>
  runInDurableObject(roomStub(roomId), async (_instance, state) => [...(await state.storage.list()).entries()]);

async function waitForAlarm(roomId: string): Promise<number> {
  for (let attempt = 0; attempt < 50; attempt++) {
    const alarm = await alarmOf(roomId);
    if (alarm !== null) return alarm;
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  throw new Error("alarme não agendado");
}

describe("relay de envelopes", () => {
  it("leva o envelope do convidado ao host com from e o do host só ao convidado certo", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const first = await openGuest(roomId);
    const second = await openGuest(roomId);

    const firstPeer = await introduce(host, first);
    const secondPeer = await introduce(host, second);
    expect(firstPeer).not.toBe(secondPeer);
    for (const peer of [firstPeer, secondPeer]) {
      expect(Number.isInteger(peer) && peer >= 0 && peer <= 0xffffffff).toBe(true);
    }

    host.send({ to: secondPeer, data: data("só para o segundo") });
    expect(await second.next()).toEqual({ from: "host", data: data("só para o segundo") });
    await first.expectSilence();
    await host.expectSilence(0);
  });

  it("descarta o envelope de um convidado para outro convidado", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const first = await openGuest(roomId);
    const second = await openGuest(roomId);
    await introduce(host, first);
    const secondPeer = await introduce(host, second);

    first.send({ to: secondPeer, data: data("atalho") });
    first.send({ to: "host", data: data("depois") });

    expect(await host.next()).toEqual({ from: expect.any(Number), data: data("depois") });
    await second.expectSilence();
    await first.expectSilence(0);
  });

  it("descarta envelopes malformados do convidado sem eco", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const guest = await openGuest(roomId);
    const peer = await introduce(host, guest);

    const malformed: (string | Uint8Array)[] = [
      "não é json",
      "[]",
      "null",
      '"host"',
      JSON.stringify({ to: "host" }),
      JSON.stringify({ data: data("sem destino") }),
      JSON.stringify({ to: "host", data: 7 }),
      JSON.stringify({ to: "host", data: "" }),
      JSON.stringify({ to: "host", data: "não é base64!" }),
      JSON.stringify({ to: "host", data: data("x"), extra: true }),
      JSON.stringify({ to: "HOST", data: data("x") }),
      JSON.stringify({ event: "host-offline" }),
      JSON.stringify({ from: "host", data: data("x") }),
      new TextEncoder().encode(JSON.stringify({ to: "host", data: data("binário") })),
    ];
    for (const message of malformed) guest.ws.send(message);
    guest.send({ to: "host", data: data("válido") });

    expect(await host.next()).toEqual({ from: peer, data: data("válido") });
    await host.expectSilence();
    await guest.expectSilence(0);
  });

  it("descarta envelopes do host para um peer desconhecido ou malformados", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const guest = await openGuest(roomId);
    const peer = await introduce(host, guest);

    host.send({ to: peer + 1000, data: data("ninguém") });
    host.send({ to: "host", data: data("a si mesmo") });
    host.send({ to: String(peer), data: data("texto") });
    host.send({ to: peer + 0.5, data: data("fração") });
    host.send({ to: -1, data: data("negativo") });
    host.send({ to: 2 ** 32, data: data("grande") });
    host.send({ to: peer, data: 1 });
    host.send({ to: peer, data: data("certo") });

    expect(await guest.next()).toEqual({ from: "host", data: data("certo") });
    await guest.expectSilence();
    await host.expectSilence(0);
  });
});

describe("limites", () => {
  it("repassa 16 KiB e fecha com 1009 a mensagem de 16 KiB + 1", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const guest = await openGuest(roomId);
    await introduce(host, guest);

    const envelope = (bytes: number): string => {
      const shell = JSON.stringify({ to: "host", data: "" });
      return JSON.stringify({ to: "host", data: "A".repeat(bytes - shell.length) });
    };
    expect(envelope(MAX_MESSAGE_BYTES)).toHaveLength(MAX_MESSAGE_BYTES);

    guest.send(envelope(MAX_MESSAGE_BYTES));
    expect((await host.next()).from).toEqual(expect.any(Number));

    guest.send(envelope(MAX_MESSAGE_BYTES + 1));
    expect(await guest.closed).toBe(1009);
  });

  it("conta bytes UTF-8, não caracteres, no limite de 16 KiB", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const guest = await openGuest(roomId);
    await introduce(host, guest);

    // 8 193 "é" = 16 386 bytes em UTF-8, abaixo de 16 384 caracteres.
    guest.send("é".repeat(8193));
    expect(await guest.closed).toBe(1009);
  });

  it("descarta a 21ª mensagem no mesmo segundo", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const guest = await openGuest(roomId);
    await new Promise((resolve) => setTimeout(resolve, 1100));

    for (let index = 1; index <= 25; index++) guest.send({ to: "host", data: data(`m${index}`) });
    const delivered: string[] = [];
    for (let index = 0; index < 20; index++) delivered.push((await host.next()).data as string);
    expect(delivered).toEqual(Array.from({ length: 20 }, (_, index) => data(`m${index + 1}`)));
    await host.expectSilence();

    await new Promise((resolve) => setTimeout(resolve, 1100));
    guest.send({ to: "host", data: data("depois de um segundo") });
    expect((await host.next()).data).toBe(data("depois de um segundo"));
  });

  it("recusa o 12º socket com 429 e libera a vaga quando alguém sai", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const guests: Client[] = [];
    for (let index = 0; index < 10; index++) guests.push(await openGuest(roomId));

    const refused = await upgrade(roomId, "guest");
    expect(refused.status).toBe(429);
    expect(refused.webSocket).toBeNull();

    const leaving = guests[0];
    if (leaving === undefined) throw new Error("sem convidado");
    const peer = await introduce(host, leaving);
    await leaving.closeAndWait();
    expect(await host.next()).toEqual({ event: "peer-left", peer });

    await openGuest(roomId);
  });

  it("reserva a vaga do host: o 11º convidado é recusado mesmo sem host", async () => {
    const roomId = newRoomId();
    for (let index = 0; index < 10; index++) await openGuest(roomId);
    expect((await upgrade(roomId, "guest")).status).toBe(429);
    await openHost(roomId, newHostToken());
  });
});

describe("host", () => {
  it("recusa com 409 outro host com token diferente", async () => {
    const roomId = newRoomId();
    await openHost(roomId, newHostToken());
    const intruder = await upgrade(roomId, "host", { "X-Room-Host-Token": newHostToken() });
    expect(intruder.status).toBe(409);
    expect(intruder.webSocket).toBeNull();
  });

  it("substitui o host anterior quando o mesmo token reconecta, sem host-offline", async () => {
    const roomId = newRoomId();
    const token = newHostToken();
    const old = await openHost(roomId, token);
    const guest = await openGuest(roomId);
    const replacement = await openHost(roomId, token);
    expect(await old.closed).toBe(4001);

    const peer = await introduce(replacement, guest);
    expect(peer).toEqual(expect.any(Number));
    await guest.expectSilence();
    expect(old.received).toEqual([]);
  });

  it("guarda só o SHA-256 do token", async () => {
    const roomId = newRoomId();
    const token = newHostToken();
    await openHost(roomId, token);
    const stored = await runInDurableObject(roomStub(roomId), async (_instance, state) =>
      JSON.stringify([...(await state.storage.list()).entries()]),
    );
    expect(stored).not.toContain(token);
    const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(token)));
    const hex = [...digest].map((byte) => byte.toString(16).padStart(2, "0")).join("");
    expect(stored).toContain(hex);
  });

  it("avisa os convidados com host-offline quando o host cai", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const first = await openGuest(roomId);
    const second = await openGuest(roomId);
    await host.closeAndWait();
    expect(await first.next()).toEqual({ event: "host-offline" });
    expect(await second.next()).toEqual({ event: "host-offline" });
  });

  it("avisa host-offline ao convidado que entra sem host", async () => {
    const roomId = newRoomId();
    const guest = await openGuest(roomId);
    expect(await guest.next()).toEqual({ event: "host-offline" });
  });

  it("avisa o host com peer-left quando um convidado sai", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const guest = await openGuest(roomId);
    const peer = await introduce(host, guest);
    await guest.closeAndWait();
    expect(await host.next()).toEqual({ event: "peer-left", peer });
  });

  it("não reaproveita o peer de quem saiu", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const first = await openGuest(roomId);
    const firstPeer = await introduce(host, first);
    await first.closeAndWait();
    expect(await host.next()).toEqual({ event: "peer-left", peer: firstPeer });
    const second = await openGuest(roomId);
    expect(await introduce(host, second)).not.toBe(firstPeer);
  });
});

describe("alarme de 10 minutos sem host", () => {
  it("fecha a sala e apaga o estado", async () => {
    const roomId = newRoomId();
    const host = await openHost(roomId, newHostToken());
    const guest = await openGuest(roomId);
    const before = Date.now();
    await host.closeAndWait();
    expect(await guest.next()).toEqual({ event: "host-offline" });

    const alarm = await waitForAlarm(roomId);
    expect(alarm).toBeGreaterThanOrEqual(before + TEN_MINUTES_MS - 1000);
    expect(alarm).toBeLessThanOrEqual(Date.now() + TEN_MINUTES_MS + 1000);

    expect(await runDurableObjectAlarm(roomStub(roomId))).toBe(true);
    await guest.closed;
    const keys = await runInDurableObject(roomStub(roomId), async (_instance, state) => (await state.storage.list()).size);
    expect(keys).toBe(0);

    // O hash apagado libera a sala para um novo token.
    await openHost(roomId, newHostToken());
  });

  it("é agendado quando o primeiro convidado entra sem host e não é adiado pelos seguintes", async () => {
    const roomId = newRoomId();
    await openGuest(roomId);
    const first = await waitForAlarm(roomId);
    await new Promise((resolve) => setTimeout(resolve, 30));
    await openGuest(roomId);
    expect(await alarmOf(roomId)).toBe(first);
  });

  it("com o host de volta, rearma o alarme e não apaga nada", async () => {
    const roomId = newRoomId();
    const token = newHostToken();
    const host = await openHost(roomId, token);
    const guest = await openGuest(roomId);
    await host.closeAndWait();
    expect(await guest.next()).toEqual({ event: "host-offline" });
    await waitForAlarm(roomId);

    const back = await openHost(roomId, token);
    const storedBefore = await storageOf(roomId);
    const beforeAlarm = Date.now();
    expect(await runDurableObjectAlarm(roomStub(roomId))).toBe(true);

    expect(await storageOf(roomId)).toEqual(storedBefore);
    expect(await alarmOf(roomId)).toBeGreaterThanOrEqual(beforeAlarm + TEN_MINUTES_MS - 1000);
    const peer = await introduce(back, guest);
    expect(peer).toEqual(expect.any(Number));
  });

  it("fica armado enquanto o host está conectado e só se rearma", async () => {
    const roomId = newRoomId();
    const before = Date.now();
    const host = await openHost(roomId, newHostToken());
    const guest = await openGuest(roomId);
    const armed = await alarmOf(roomId);
    expect(armed).toBeGreaterThanOrEqual(before + TEN_MINUTES_MS - 1000);

    const storedBefore = await storageOf(roomId);
    expect(storedBefore.length).toBeGreaterThan(0);
    expect(await runDurableObjectAlarm(roomStub(roomId))).toBe(true);
    expect(await storageOf(roomId)).toEqual(storedBefore);
    expect(await alarmOf(roomId)).not.toBeNull();

    await introduce(host, guest);
    await guest.expectSilence(0);
  });

  it("apaga o estado mesmo quando o objeto reinicia sem eventos de saída", async () => {
    const roomId = newRoomId();
    await openHost(roomId, newHostToken());
    await openGuest(roomId);
    await evictDurableObject(roomStub(roomId), { webSockets: "close" });

    expect(await runDurableObjectAlarm(roomStub(roomId))).toBe(true);
    expect(await storageOf(roomId)).toEqual([]);
  });
});

describe("rotas", () => {
  const badRoomIds = [
    "",
    "A".repeat(21),
    "A".repeat(23),
    `${"A".repeat(21)}B`, // bits de sobra diferentes de zero
    `${"A".repeat(20)}+A`,
    `${"A".repeat(20)}/A`,
    `${"A".repeat(20)}AA==`,
    `${"A".repeat(20)}%41A`,
  ];

  it("devolve 404 para room_id fora do formato canônico", async () => {
    for (const roomId of badRoomIds) {
      for (const path of [`/rooms/${roomId}/guest`, `/rooms/${roomId}/host`, `/j/${roomId}`]) {
        const response = await SELF.fetch(`${ORIGIN}${path}`, {
          headers: { Upgrade: "websocket", "X-Room-Host-Token": newHostToken() },
        });
        expect(response.status, path).toBe(404);
      }
    }
  });

  it("aceita os quatro últimos caracteres canônicos do room_id", async () => {
    for (const last of ["A", "Q", "g", "w"]) {
      const response = await SELF.fetch(`${ORIGIN}/j/${"_".repeat(21)}${last}`);
      expect(response.status).toBe(200);
    }
  });

  it("devolve 404 para qualquer outra rota ou método", async () => {
    const roomId = newRoomId();
    const cases: [string, string][] = [
      ["GET", "/"],
      ["GET", "/rooms"],
      ["GET", `/rooms/${roomId}`],
      ["GET", `/rooms/${roomId}/other`],
      ["GET", `/rooms/${roomId}/guest/extra`],
      ["GET", `/j/${roomId}/extra`],
      ["GET", `/J/${roomId}`],
      ["POST", `/j/${roomId}`],
      ["POST", `/rooms/${roomId}/guest`],
      ["GET", "/favicon.ico"],
    ];
    for (const [method, path] of cases) {
      // No workerd local, um fetch com `Upgrade: websocket` chega sempre como GET; por isso os
      // métodos diferentes de GET vão sem o header.
      const headers: Record<string, string> = method === "GET" ? { Upgrade: "websocket" } : {};
      const response = await SELF.fetch(`${ORIGIN}${path}`, { method, headers });
      expect(response.status, `${method} ${path}`).toBe(404);
    }
  });

  it("devolve 400 para X-Room-Host-Token ausente ou malformado", async () => {
    const roomId = newRoomId();
    const tokens: (string | undefined)[] = [
      undefined,
      "",
      "A".repeat(42),
      "A".repeat(44),
      `${"A".repeat(42)}B`, // bits de sobra diferentes de zero
      `${"A".repeat(41)}+A`,
      `${"A".repeat(42)}=`,
    ];
    for (const token of tokens) {
      const response = await upgrade(roomId, "host", token === undefined ? {} : { "X-Room-Host-Token": token });
      expect(response.status, String(token)).toBe(400);
    }
    await openHost(roomId, newHostToken());
  });

  it("exige upgrade WebSocket nas rotas de sala", async () => {
    const roomId = newRoomId();
    expect((await SELF.fetch(`${ORIGIN}/rooms/${roomId}/guest`)).status).toBe(426);
    const host = await SELF.fetch(`${ORIGIN}/rooms/${roomId}/host`, { headers: { "X-Room-Host-Token": newHostToken() } });
    expect(host.status).toBe(426);
  });
});

describe("página /j/", () => {
  async function sha256Base64(text: string): Promise<string> {
    const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)));
    let binary = "";
    for (const byte of digest) binary += String.fromCharCode(byte);
    return btoa(binary);
  }

  it("mostra Abra no RC Music sem o fragmento, sem o room_id e sem recursos externos", async () => {
    const roomId = newRoomId();
    const secret = randomBase64Url(16);
    const fingerprint = randomBase64Url(32);
    const response = await SELF.fetch(`${ORIGIN}/j/${roomId}#${secret}.${fingerprint}`);
    expect(response.status).toBe(200);
    expect(response.headers.get("Content-Type")).toBe("text/html; charset=utf-8");
    expect(response.headers.get("Referrer-Policy")).toBe("no-referrer");
    expect(response.headers.get("Cache-Control")).toBe("no-store");
    expect(response.headers.get("X-Content-Type-Options")).toBe("nosniff");

    const html = await response.text();
    expect(html).toContain('<html lang="pt-BR">');
    expect(html).toContain("Abra no RC Music");
    expect(html).toContain("Copiar o link");
    expect(html).not.toContain(secret);
    expect(html).not.toContain(fingerprint);
    expect(html).not.toContain(roomId);
    expect(html).not.toMatch(/\b(src|href|action)\s*=/i);
    expect(html).not.toMatch(/<(link|iframe|object|embed|img|base|form)\b/i);
    expect(html).not.toMatch(/https?:|\/\//i);
    expect(html).toContain("location.href");
  });

  it("envia uma CSP estrita com o hash do script e do estilo inline", async () => {
    const response = await SELF.fetch(`${ORIGIN}/j/${newRoomId()}`);
    const html = await response.text();
    const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((match) => match[1] ?? "");
    const styles = [...html.matchAll(/<style>([\s\S]*?)<\/style>/g)].map((match) => match[1] ?? "");
    expect(scripts).toHaveLength(1);
    expect(styles).toHaveLength(1);

    const csp = response.headers.get("Content-Security-Policy") ?? "";
    const directives = new Map(
      csp.split(";").map((directive) => {
        const [name = "", ...values] = directive.trim().split(/\s+/);
        return [name, values.join(" ")] as const;
      }),
    );
    expect(directives.get("default-src")).toBe("'none'");
    expect(directives.get("script-src")).toBe(`'sha256-${await sha256Base64(scripts[0] ?? "")}'`);
    expect(directives.get("style-src")).toBe(`'sha256-${await sha256Base64(styles[0] ?? "")}'`);
    expect(directives.get("base-uri")).toBe("'none'");
    expect(directives.get("form-action")).toBe("'none'");
    expect(directives.get("frame-ancestors")).toBe("'none'");
  });
});

describe("privacidade do código", () => {
  it("não usa console em src/", () => {
    const sources = import.meta.glob("../src/**/*.ts", { query: "?raw", import: "default", eager: true });
    const entries = Object.entries(sources);
    expect(entries.map(([path]) => path).sort()).toEqual(
      ["../src/RoomRelay.ts", "../src/index.ts", "../src/joinPage.ts"].sort(),
    );
    for (const [path, source] of entries) expect(source as string, path).not.toMatch(/\bconsole\b/);
  });
});
