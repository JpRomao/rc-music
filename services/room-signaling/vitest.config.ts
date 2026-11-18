import { cloudflareTest } from "@cloudflare/vitest-pool-workers";
import { defineConfig } from "vitest/config";

// Os testes rodam localmente no workerd (Miniflare), sem contato com a Cloudflare.
export default defineConfig({
  plugins: [cloudflareTest({ wrangler: { configPath: "./wrangler.toml" } })],
});
