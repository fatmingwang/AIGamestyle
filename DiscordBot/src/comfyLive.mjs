// @ts-check
import * as fs from 'node:fs';
import * as net from 'node:net';
import * as path from 'node:path';
import { spawn } from 'node:child_process';

// Talks to `ComfyLive.exe --headless` (ComfyLive/README.md "Headless mode"): one TCP connection per command,
// one JSON line out ({"command", "args"}), NDJSON events back until {"type": "job_end"}.
// When nothing answers on host:port and autoStart is on, ComfyLive is started (hidden, left running) and waited for.

/**
 * @typedef {{ type: string, [key: string]: any }} ComfyEvent
 * @typedef {{ type: 'job_end', command: string, code: number, success: boolean, message: string }} JobEnd
 */

export class ComfyLive {
  /** @param {import('./config.mjs').ComfyLiveConfig} cfg  @param {string} logDir  @param {import('./logger.mjs').Logger} log */
  constructor(cfg, logDir, log) {
    this.cfg = cfg;
    this.logDir = logDir;
    this.log = log;
    /** @type {Promise<void> | null} */
    this.starting = null;
    // A ComfyLive that was already running (started earlier, maybe with other settings) is checked once
    /** @type {Promise<void> | null} */
    this.checking = null;
    this.checked = false;
  }

  /** Is the headless server answering? @param {number} [timeoutMs] @returns {Promise<boolean>} */
  probe(timeoutMs = 2000) {
    return new Promise(resolve => {
      const s = net.createConnection({ host: this.cfg.host, port: this.cfg.port });
      const done = (/** @type {boolean} */ ok) => { s.destroy(); resolve(ok); };
      s.setTimeout(timeoutMs, () => done(false));
      s.once('connect', () => done(true));
      s.once('error', () => done(false));
    });
  }

  /** Answering with our ComfyUI addresses, or started now (autoStart). Throws with a reason otherwise. */
  async ensureRunning() {
    if (await this.probe()) {
      this.checking ??= this.checkSettings().finally(() => { this.checking = null; });
      await this.checking;
      return;
    }
    if (!this.cfg.autoStart)
      throw new Error(`ComfyLive is not running at ${this.cfg.host}:${this.cfg.port} (autoStart is off): start "ComfyLive.exe --headless"`);
    this.starting ??= this.start().finally(() => { this.starting = null; });
    await this.starting;
  }

  /**
   * Once per bot run: is the ComfyLive that was already running using comfyServer / speechServer from the config?
   * ComfyLive only takes them on its command line, so one started earlier (by an older config, by hand, by another
   * tool) may point elsewhere. If so and autoStart is on, it is shut down and started again with ours.
   */
  async checkSettings() {
    if (this.checked) return;
    /** @param {string} s */
    const norm = s => s.trim().toLowerCase().replace(/^https?:\/\//, '').replace(/\/+$/, '').replace(/^localhost(?=:|$)/, '127.0.0.1');
    const wrong = [];
    for (const [which, want] of /** @type {const} */ ([['main', this.cfg.comfyServer], ['speech', this.cfg.speechServer]])) {
      if (!want) continue;
      let server = '';
      await this.exchange('comfyui-status', { which }, e => { if (e.type === 'result' && typeof e.server === 'string') server = e.server; });
      if (server && norm(server) !== norm(want)) wrong.push(`${which} ComfyUI ${server} (config: ${want})`);
    }
    this.checked = true;
    if (wrong.length === 0) return;
    if (!this.cfg.autoStart) {
      this.log.error(`The running ComfyLive uses ${wrong.join(', ')}. Restart it with the config's addresses (autoStart is off).`);
      return;
    }
    this.log.log(`The running ComfyLive uses ${wrong.join(', ')}: restarting it with the config's addresses.`);
    await this.exchange('shutdown', {}).catch(() => undefined);
    for (let t = 0; t < 20 && (await this.probe(500)); t++) await new Promise(r => setTimeout(r, 500));
    await this.start();
  }

  async start() {
    const exe = this.cfg.exePath;
    if (!fs.existsSync(exe)) throw new Error(`ComfyLive is not running and ${exe} does not exist (set comfyLive.exePath)`);
    const args = ['--headless', `--host=${this.cfg.host}`, `--port=${this.cfg.port}`];
    if (this.cfg.comfyServer) args.push(`--server=${this.cfg.comfyServer}`);
    if (this.cfg.speechServer) args.push(`--speech-server=${this.cfg.speechServer}`);
    args.push(...(this.cfg.extraArgs ?? []));

    fs.mkdirSync(this.logDir, { recursive: true });
    const out = fs.openSync(path.join(this.logDir, 'comfylive.log'), 'a');
    this.log.log(`Starting ComfyLive: ${exe} ${args.join(' ')} (output in logs/comfylive.log)`);
    const child = spawn(exe, args, { cwd: path.dirname(exe), detached: true, stdio: ['ignore', out, out], windowsHide: true });
    child.unref();
    fs.closeSync(out);
    this.checked = true;   // started by us, with our addresses
    let exited = /** @type {number | null} */ (null);
    child.once('exit', code => { exited = code ?? -1; });

    const deadline = Date.now() + this.cfg.startTimeoutSec * 1000;
    while (Date.now() < deadline) {
      await new Promise(r => setTimeout(r, 500));
      if (exited !== null) throw new Error(`ComfyLive exited at once (code ${exited}): see logs/comfylive.log`);
      if (await this.probe(1000)) {
        this.log.log(`ComfyLive is up at ${this.cfg.host}:${this.cfg.port}`);
        return;
      }
    }
    throw new Error(`ComfyLive did not answer on ${this.cfg.host}:${this.cfg.port} within ${this.cfg.startTimeoutSec} s`);
  }

  /**
   * One command; every event goes to onEvent; resolves with the job_end event.
   * @param {string} command  @param {object} args  @param {(e: ComfyEvent) => void} [onEvent]  @returns {Promise<JobEnd>}
   */
  async run(command, args, onEvent) {
    await this.ensureRunning();
    return this.exchange(command, args, onEvent);
  }

  /** run() without the running check.
   *  @param {string} command  @param {object} args  @param {(e: ComfyEvent) => void} [onEvent]  @returns {Promise<JobEnd>} */
  exchange(command, args, onEvent) {
    return new Promise((resolve, reject) => {
      const s = net.createConnection({ host: this.cfg.host, port: this.cfg.port });
      let buf = '';
      let ended = false;
      s.setEncoding('utf8');
      s.once('connect', () => s.write(JSON.stringify({ command, args }) + '\n'));
      s.on('data', chunk => {
        buf += chunk;
        let nl;
        while ((nl = buf.indexOf('\n')) >= 0) {
          const line = buf.slice(0, nl).trim();
          buf = buf.slice(nl + 1);
          if (!line) continue;
          /** @type {ComfyEvent} */
          let e;
          try { e = JSON.parse(line); } catch { this.log.debug(`ComfyLive: not JSON: ${line}`); continue; }
          this.log.debug(`ComfyLive ${command} <- ${line.length > 500 ? line.slice(0, 500) + '...' : line}`);
          try { onEvent?.(e); } catch (err) { this.log.error('ComfyLive event handler failed:', err); }
          if (e.type === 'job_end') {
            ended = true;
            s.end(JSON.stringify({ command: 'exit' }) + '\n');
            resolve(/** @type {JobEnd} */ (e));
          }
        }
      });
      s.once('error', err => { if (!ended) reject(new Error(`ComfyLive connection failed: ${err.message}`)); });
      s.once('close', () => { if (!ended) reject(new Error('ComfyLive closed the connection before the job ended')); });
    });
  }

  /** Stops the running generate (answered at once, beside it). */
  async stop() {
    if (!(await this.probe())) return;
    // exchange, not run: no settings check (it could restart ComfyLive under the job being stopped)
    try { await this.exchange('stop', {}); } catch (err) { this.log.error('ComfyLive stop failed:', err); }
  }
}
