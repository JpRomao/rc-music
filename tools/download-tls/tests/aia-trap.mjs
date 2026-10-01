// Synthetic CA trap for a single isolated test. No proxy or outbound request.
import fs from 'node:fs';
import http from 'node:http';

const certificatePath = process.argv[2];
if (!certificatePath) throw new Error('Missing synthetic issuer path');
const server = http.createServer({ maxHeaderSize: 4096 }, (request, response) => {
  process.stdout.write('REQUEST\n');
  if (request.method !== 'GET' || request.url !== '/issuer.der') {
    response.writeHead(404).end();
    return;
  }
  const body = fs.readFileSync(certificatePath);
  response.writeHead(200, { 'Content-Type': 'application/pkix-cert', 'Content-Length': body.length });
  response.end(body);
});
server.listen(0, '127.0.0.1', () => {
  process.stdout.write(`${JSON.stringify({ port: server.address().port })}\n`);
});
