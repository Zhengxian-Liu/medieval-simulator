import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: 'e2e',
  timeout: 120_000,
  use: {
    baseURL: 'http://localhost:5299',
    viewport: { width: 1600, height: 900 },
  },
  webServer: {
    command: 'npx vite build --logLevel error && npx vite preview --port 5299 --strictPort',
    url: 'http://localhost:5299',
  },
});
