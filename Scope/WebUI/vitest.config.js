import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'jsdom',
    globals: true,
    include: ['tests/**/*.test.js'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'json', 'html'],
      exclude: [
        'src/scope.css',
        'src/mount/**',
        'src/utils/**'
      ],
      thresholds: {
        lines: 80,
        functions: 80,
        branches: 70,
        statements: 80
      }
    },
    setupFiles: ['tests/setup.js']
  },
  resolve: {
    alias: {
      '@abdsynths/scope': '/src'
    }
  }
});