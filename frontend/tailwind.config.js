/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ['./src/**/*.{html,ts}'],
  darkMode: 'class',
  theme: {
    extend: {
      fontFamily: {
        sans: ['"Inter"', 'system-ui', 'sans-serif'],
        mono: ['"JetBrains Mono"', 'ui-monospace', 'SFMono-Regular', 'monospace'],
      },
      colors: {
        // "CarSentinel" brand palette — deep slate + cyan, the IoT/industrial-ops
        // console look (Grafana/Datadog-adjacent) rather than a consumer-app palette.
        brand: {
          50: '#ecfeff',
          100: '#cffafe',
          200: '#a5f3fc',
          300: '#67e8f9',
          400: '#22d3ee',
          500: '#06b6d4',
          600: '#0891b2',
          700: '#0e7490',
          800: '#155e75',
          900: '#164e63',
          950: '#083344',
        },
        surface: {
          DEFAULT: '#0b1220',
          50: '#f8fafc',
          100: '#f1f5f9',
          200: '#212a3a',
          300: '#1a2333',
          400: '#151d2c',
          500: '#111827',
          600: '#0d1420',
          700: '#0b1220',
          800: '#080d17',
          900: '#05080f',
        },
      },
      boxShadow: {
        panel: '0 1px 2px rgba(0,0,0,0.4), 0 0 0 1px rgba(255,255,255,0.04)',
      },
      animation: {
        'pulse-fast': 'pulse 1.4s cubic-bezier(0.4, 0, 0.6, 1) infinite',
      },
    },
  },
  plugins: [],
};
