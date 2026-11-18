// Página de `/j/<room_id>`: quem abre o convite no navegador é orientado a usar o RC Music.
// O segredo e a impressão digital ficam depois do `#`, que o navegador nunca envia ao servidor.
// A página não repete nada da requisição (nem o room_id); o botão copia `location.href` no próprio
// navegador. Script e estilo são inline e liberados na CSP só pelo hash SHA-256 do seu conteúdo.

const STYLE = `
:root { color-scheme: light dark; font-family: system-ui, sans-serif; }
body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: Canvas; color: CanvasText; }
main { max-width: 32rem; padding: 2rem; line-height: 1.5; }
h1 { font-size: 1.6rem; margin: 0 0 1rem; }
button { font: inherit; padding: 0.6rem 1.2rem; border-radius: 0.5rem; border: 1px solid ButtonBorder; background: ButtonFace; color: ButtonText; cursor: pointer; }
button:focus-visible { outline: 3px solid Highlight; outline-offset: 2px; }
.nota { font-size: 0.9rem; opacity: 0.8; }
`;

const SCRIPT = `
(function () {
  var botao = document.getElementById("copiar");
  var estado = document.getElementById("estado");
  function falhou() { estado.textContent = "Não foi possível copiar. Copie o endereço da barra do navegador."; }
  botao.addEventListener("click", function () {
    if (!navigator.clipboard || !navigator.clipboard.writeText) { falhou(); return; }
    navigator.clipboard.writeText(location.href).then(function () {
      estado.textContent = "Link copiado. Cole no RC Music para entrar na sala.";
    }, falhou);
  });
})();
`;

const HTML = `<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<title>Abra no RC Music</title>
<style>${STYLE}</style>
</head>
<body>
<main>
<h1>Abra no RC Music</h1>
<p>Este é um convite para uma sala de escuta do RC Music. Copie o link e cole no RC Music para entrar.</p>
<p><button id="copiar" type="button">Copiar o link</button></p>
<p id="estado" role="status" aria-live="polite"></p>
<p class="nota">A parte do link depois de # fica só no seu navegador: este servidor não a recebe.</p>
</main>
<script>${SCRIPT}</script>
</body>
</html>
`;

async function sha256Base64(text: string): Promise<string> {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)));
  let binary = "";
  for (const byte of digest) binary += String.fromCharCode(byte);
  return btoa(binary);
}

let contentSecurityPolicy: Promise<string> | undefined;

function policy(): Promise<string> {
  contentSecurityPolicy ??= Promise.all([sha256Base64(SCRIPT), sha256Base64(STYLE)]).then(
    ([script, style]) =>
      [
        "default-src 'none'",
        `script-src 'sha256-${script}'`,
        `style-src 'sha256-${style}'`,
        "base-uri 'none'",
        "form-action 'none'",
        "frame-ancestors 'none'",
      ].join("; "),
  );
  return contentSecurityPolicy;
}

export async function joinPage(): Promise<Response> {
  return new Response(HTML, {
    status: 200,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Content-Security-Policy": await policy(),
      "Referrer-Policy": "no-referrer",
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff",
    },
  });
}
