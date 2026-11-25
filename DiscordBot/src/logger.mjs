// @ts-check
import * as fs from 'node:fs';
import * as path from 'node:path';

// Console + logs/bot-YYYYMMDD.log. debug() lines only appear in bDevMode.
export class Logger {
  /** @param {string} dir  @param {boolean} dev */
  constructor(dir, dev) {
    this.dir = dir;
    this.dev = dev;
    fs.mkdirSync(dir, { recursive: true });
  }

  /** @param {string} level  @param {unknown[]} parts */
  write(level, parts) {
    const now = new Date();
    const text = parts.map(p => (p instanceof Error ? p.stack || p.message : typeof p === 'string' ? p : JSON.stringify(p))).join(' ');
    const line = `${now.toISOString()} ${level} ${text}`;
    (level === 'ERROR' ? console.error : console.log)(line);
    const day = now.toISOString().slice(0, 10).replace(/-/g, '');
    try { fs.appendFileSync(path.join(this.dir, `bot-${day}.log`), line + '\n'); } catch { /* the console still has it */ }
  }

  /** @param {...unknown} parts */ log(...parts) { this.write('INFO ', parts); }
  /** @param {...unknown} parts */ error(...parts) { this.write('ERROR', parts); }
  /** @param {...unknown} parts */ debug(...parts) { if (this.dev) this.write('DEBUG', parts); }
}
