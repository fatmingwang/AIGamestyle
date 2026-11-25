// @ts-check
import * as fs from 'node:fs';
import * as path from 'node:path';
import { parseRequest } from './request.mjs';
import { menuMessage, stopRow } from './forms.mjs';
import { ZipWriter } from './zipWriter.mjs';

// One request = one job, run one after another (ComfyLive runs one graph at a time anyway):
//   jobs/<yyyyMMdd-HHmmss>_<user>_<graph>/
//     <graph>.json                  the recipe sent to ComfyLive's `make`
//     <attachments>                 pictures etc. from the message (a recipe can name them by file name)
//     <graph>_<date-time>.png|mp3|mp4 + .json settings (+ _frames/ for a video), written by ComfyLive
//   jobs/<same name>_frames.zip     only for a video that came back as frames (no MP4): those frames
// Discord gets the result files themselves (the MP4 / MP3 / PNG), not the .json files. Over the upload limit, the
// reply says where they are on disk.

const MAX_ATTACHMENT_BYTES = 25 * 1024 * 1024;
const MAX_JSON_BYTES = 1024 * 1024;
const PROGRESS_EDIT_MS = 3000;   // Discord allows ~5 edits per 5 s per channel; keep well under
const COLOR = { green: 0x2ecc71, red: 0xe74c3c, orange: 0xffa500, blue: 0x3498db };
const STAMP_RE = /^\d{8}-\d{6}_/;

/**
 * @typedef {import('./discordGateway.mjs').BotRequest} BotRequest
 * @typedef {{
 *   req: BotRequest, recipe: Record<string, any>, label: string, summary: string,
 *   statusId: string | null, stopRequested: boolean, timedOut: boolean
 * }} Job
 */

/** @param {Date} d */
const stamp = d => {
  const p = (/** @type {number} */ n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
};
/** @param {string} s  @param {number} n */
const cut = (s, n) => (s.length > n ? s.slice(0, n - 1) + '…' : s);
/** @param {number} b */
const mb = b => `${(b / 1024 / 1024).toFixed(1)} MB`;
/** The upload's content type, so Discord shows a picture or a player.  @param {string} p */
const mimeOf = p => ({
  '.mp4': 'video/mp4', '.webm': 'video/webm', '.mov': 'video/quicktime', '.mp3': 'audio/mpeg', '.wav': 'audio/wav',
  '.flac': 'audio/flac', '.ogg': 'audio/ogg', '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.webp': 'image/webp',
})[path.extname(p).toLowerCase()] ?? 'application/octet-stream';
/** A file name that is safe on Windows. @param {string} s */
const safeName = s => s.replace(/[<>:"/\\|?*\x00-\x1f]/g, '_').replace(/^\.+/, '_').slice(0, 100) || 'file';

export class JobRunner {
  /**
   * @param {import('./config.mjs').Config} cfg
   * @param {import('./comfyLive.mjs').ComfyLive} comfy
   * @param {import('./discordRest.mjs').DiscordRest} rest
   * @param {import('./logger.mjs').Logger} log
   */
  constructor(cfg, comfy, rest, log) {
    this.cfg = cfg;
    this.comfy = comfy;
    this.rest = rest;
    this.log = log;
    /** @type {Job[]} */
    this.queue = [];
    /** @type {Job | null} */
    this.current = null;
    fs.mkdirSync(cfg.jobs.folder, { recursive: true });
    this.cleanup();
  }

  // ── Requests ───────────────────────────────────────────────────────────────

  /** @param {BotRequest} req */
  async handle(req) {
    let attachedJson = null;
    const jsonFile = req.attachments.find(a => /\.json$/i.test(a.filename));
    if (jsonFile) {
      if (jsonFile.size > MAX_JSON_BYTES) return this.reply(req, `❌ ${jsonFile.filename} is too big for a recipe (${mb(jsonFile.size)}).`);
      const r = await fetch(jsonFile.url);
      if (!r.ok) return this.reply(req, `❌ Could not download ${jsonFile.filename} (${r.status}).`);
      attachedJson = await r.text();
    }

    const parsed = parseRequest(req.text, attachedJson);
    switch (parsed.kind) {
      case 'menu': return this.postMenu(req);
      case 'status': return this.reply(req, await this.statusText());
      case 'stop': return this.reply(req, await this.stopFor(req.user));
      case 'error': return this.reply(req, `❌ ${parsed.message}\nSend \`help\` for the menu.`);
    }
    await this.enqueue(req, parsed);
  }

  /** The menu (forms.mjs) as a reply to the message.  @param {BotRequest} req */
  async postMenu(req) {
    const r = await this.rest.createMessage(req.channelId, {
      ...menuMessage(),
      message_reference: { message_id: req.messageId, fail_if_not_exists: false },
      allowed_mentions: { replied_user: false },
    });
    if (!r.ok) this.log.error(`Discord: could not post the menu (${r.status}):`, r.data);
  }

  /**
   * A job from a text command or a submitted form. Its status message (with a Stop button) replies to req.messageId.
   * @param {BotRequest} req  @param {{ recipe: Record<string, any>, label: string, summary: string }} what
   * @returns {Promise<boolean>} false when the queue is full
   */
  async enqueue(req, what) {
    if (this.queue.length >= this.cfg.jobs.maxQueue) {
      await this.reply(req, `⏳ The queue is full (${this.queue.length} waiting). Try again later.`);
      return false;
    }
    /** @type {Job} */
    const job = { req, recipe: what.recipe, label: what.label, summary: what.summary, statusId: null, stopRequested: false, timedOut: false };
    const ahead = this.queue.length + (this.current ? 1 : 0);
    const r = await this.rest.createMessage(req.channelId, {
      embeds: [this.embed(job, ahead ? `⏳ Queued: ${ahead} job(s) ahead.` : '⏳ Starting...', COLOR.orange)],
      components: stopRow(),
      message_reference: { message_id: req.messageId, fail_if_not_exists: false },
      allowed_mentions: { replied_user: false },
    });
    if (!r.ok) this.log.error(`Discord: could not post the status message (${r.status}):`, r.data);
    job.statusId = r.ok ? r.data?.id ?? null : null;
    this.queue.push(job);
    this.pump();
    return true;
  }

  /** @param {BotRequest} req  @param {string} content */
  async reply(req, content) {
    const r = await this.rest.createMessage(req.channelId, {
      content: cut(content, 2000),
      message_reference: { message_id: req.messageId, fail_if_not_exists: false },
      allowed_mentions: { replied_user: false },
    });
    if (!r.ok) this.log.error(`Discord: reply failed (${r.status}):`, r.data);
  }

  /** Queue, ComfyLive and ComfyUI state, as text. */
  async statusText() {
    const lines = [];
    const up = await this.comfy.probe();
    lines.push(`ComfyLive at ${this.cfg.comfyLive.host}:${this.cfg.comfyLive.port}: ${up ? '🟢 running' : this.cfg.comfyLive.autoStart ? '⚪ not running (starts with the next job)' : '🔴 not running'}`);
    if (this.current) {
      lines.push(`Running: **${this.current.label}** for ${this.current.req.user.name}${this.current.summary ? ` (${cut(this.current.summary, 80)})` : ''}`);
    } else if (up) {
      // only when idle: ComfyLive runs commands one at a time, so this would wait behind a running job
      try {
        const end = await this.comfy.run('comfyui-status', {});
        lines.push(`ComfyUI ${this.cfg.comfyLive.comfyServer}: ${end.success ? '🟢 reachable' : `🔴 ${end.message}`}`);
      } catch (err) {
        lines.push(`ComfyUI: could not ask (${err instanceof Error ? err.message : err})`);
      }
    }
    lines.push(`Waiting: ${this.queue.length}${this.queue.length ? ' (' + this.queue.map(j => `${j.label}/${j.req.user.name}`).join(', ') + ')' : ''}`);
    return lines.join('\n');
  }

  /** Removes the person's waiting jobs and stops their running one (an admin may stop anyone's); says what it did.
   *  @param {import('./discordGateway.mjs').BotUser} user */
  async stopFor(user) {
    const mine = this.queue.filter(j => j.req.user.id === user.id);
    this.queue = this.queue.filter(j => j.req.user.id !== user.id);
    for (const j of mine) await this.setStatus(j, '✖️ Cancelled before it started.', COLOR.red, true);
    let stopping = false;
    if (this.current && (this.current.req.user.id === user.id || user.isAdmin)) {
      this.current.stopRequested = true;
      stopping = true;
      await this.comfy.stop();
    }
    return stopping || mine.length
      ? `✖️ ${stopping ? 'Stopping the running job' : ''}${stopping && mine.length ? ' and ' : ''}${mine.length ? `cancelled ${mine.length} waiting job(s)` : ''}.`
      : this.current ? "The running job is someone else's, and you have no waiting job." : 'You have no running or waiting job.';
  }

  // ── Running ────────────────────────────────────────────────────────────────

  pump() {
    if (this.current || this.queue.length === 0) return;
    const job = /** @type {Job} */ (this.queue.shift());
    this.current = job;
    this.run(job)
      .catch(async err => {
        this.log.error(`Job ${job.label} for ${job.req.user.name} crashed:`, err);
        await this.setStatus(job, `❌ ${err instanceof Error ? err.message : err}`, COLOR.red, true).catch(() => undefined);
      })
      .finally(() => {
        this.current = null;
        this.cleanup();
        this.pump();
      });
  }

  /** @param {Job} job */
  async run(job) {
    const started = new Date();
    const name = `${stamp(started)}_${safeName(job.req.user.name).replace(/\s+/g, '_').slice(0, 30)}_${job.label}`;
    const dir = path.join(this.cfg.jobs.folder, name);
    fs.mkdirSync(dir, { recursive: true });
    this.log.log(`Job ${name}: start`);

    await this.downloadAttachments(job, dir);
    const recipePath = path.join(dir, `${job.label}.json`);
    fs.writeFileSync(recipePath, JSON.stringify(job.recipe, null, 2));

    await this.setStatus(job, '🚀 Sending to ComfyLive...', COLOR.blue);

    // A ComfyUI on another PC that does not answer: say so. (ComfyLive would try to start one on this PC instead,
    // failing with a confusing "No ComfyUI in ..." when this PC has none.)
    const which = ['speech', 'voice'].includes(String(job.recipe.graph)) ? 'speech' : 'main';
    const server = which === 'speech' ? this.cfg.comfyLive.speechServer : this.cfg.comfyLive.comfyServer;
    if (server && !/^(127\.0\.0\.1|localhost)(:|$)/i.test(server)) {
      const check = await this.comfy.run('comfyui-status', { which });
      if (!check.success) {
        this.log.log(`Job ${name}: ${which} ComfyUI ${server} is not answering`);
        await this.setStatus(job, `❌ The ${which === 'speech' ? 'speech ' : ''}ComfyUI at \`${server}\` is not running.\n`
          + `Start it on that PC with \`--listen 0.0.0.0 --port ${server.split(':')[1] ?? 8188}\`${which === 'speech' ? ' (the speech venv, with Qwen3-TTS)' : ''}, then submit again.`, COLOR.red, true);
        return;
      }
    }

    /** @type {string[]} */
    const logLines = [];
    /** @type {string[]} */
    const outputs = [];
    /** @type {string[]} */
    const results = [];    // full paths of the result files (the MP4, MP3, PNG ...): what is sent
    /** @type {string[]} */
    const frameDirs = [];  // a video's frame folder when no MP4 was made
    /** @type {string[]} */
    const warnings = [];   // the job worked, but something is missing (e.g. no MP4: ffmpeg not found)
    let lastEdit = 0;
    let progressText = '';
    // the last progress edit: waited for before the final status, so it can never land after it
    let pendingEdit = Promise.resolve();
    const timer = setTimeout(() => {
      job.timedOut = true;
      this.log.log(`Job ${name}: over ${this.cfg.jobs.timeoutMin} min, stopping`);
      this.comfy.stop();
    }, this.cfg.jobs.timeoutMin * 60_000);

    let end;
    try {
      end = await this.comfy.run('make', { path: recipePath }, e => {
        if (e.type === 'log' && typeof e.text === 'string') logLines.push(e.text);
        else if (e.type === 'queued') progressText = '🎨 Queued on ComfyUI...';
        else if (e.type === 'progress' && e.max > 0) {
          const pct = Math.round((e.value / e.max) * 100);
          const filled = Math.round(pct / 10);
          progressText = `🎨 ${'▓'.repeat(filled)}${'░'.repeat(10 - filled)} ${pct}% (step ${e.value}/${e.max})`;
        } else if (e.type === 'image' || e.type === 'audio' || e.type === 'video') {
          if (e.path) { outputs.push(path.basename(e.path)); results.push(e.path); }
          if (e.frames && !e.path) frameDirs.push(e.frames);   // a video with no MP4: its frames are the result
          if (e.error) warnings.push(`${e.type}: ${e.error}`);
          if (e.mp4Error) warnings.push(`No MP4, only the frames: ${e.mp4Error}`);
          progressText = e.type === 'video' && e.frames ? '🎞️ Frames in, packing...' : '📦 Packing...';
        } else return;
        if (Date.now() - lastEdit >= PROGRESS_EDIT_MS) {
          lastEdit = Date.now();
          const extra = this.cfg.bDevMode && logLines.length ? `\n\`\`\`\n${cut(logLines.slice(-5).join('\n'), 800)}\n\`\`\`` : '';
          pendingEdit = this.setStatus(job, progressText + extra, COLOR.blue).catch(() => undefined);
        }
      });
    } finally {
      clearTimeout(timer);
      await pendingEdit;
    }

    const seconds = ((Date.now() - started.getTime()) / 1000).toFixed(1);
    if (!end.success) {
      const why = job.timedOut ? `stopped: it ran longer than ${this.cfg.jobs.timeoutMin} min`
        : job.stopRequested ? 'stopped on request'
        : `${end.message} (code ${end.code})`;
      this.log.log(`Job ${name}: failed after ${seconds} s: ${why}`);
      const tail = logLines.length ? `\n\`\`\`\n${cut(logLines.slice(-8).join('\n'), 1200)}\n\`\`\`` : '';
      await this.setStatus(job, `❌ ${why}${tail}`, COLOR.red, true);
      return;
    }

    // ── send the result files themselves (MP4, MP3, PNG ...): Discord shows or plays them in place. The recipe and the
    // settings .json files stay in the job folder on the bot PC. Only a video that came back as frames (no MP4) is
    // zipped, and that zip holds the frames alone.
    const limit = this.cfg.discord.maxUploadMB * 1024 * 1024;
    /** @type {{ path: string, name: string, type: string }[]} */
    let send = [...new Set(results)].filter(p => fs.existsSync(p)).slice(0, 10)
      .map(p => ({ path: p, name: path.basename(p), type: mimeOf(p) }));
    if (!send.length && frameDirs.length && fs.existsSync(frameDirs[0])) {
      const zipPath = path.join(this.cfg.jobs.folder, `${name}_frames.zip`);
      this.zip(frameDirs[0], zipPath);
      send = [{ path: zipPath, name: path.basename(zipPath), type: 'application/zip' }];
    }
    const total = send.reduce((n, f) => n + fs.statSync(f.path).size, 0);
    const made = outputs.length ? outputs.join(', ') : send.length ? send.map(f => f.name).join(', ') : 'no file';
    const done = `${warnings.length ? '⚠️' : '✅'} **${job.label}** done in ${seconds} s: ${made}`
      + (warnings.length ? `\n${warnings.map(w => `⚠️ ${w}`).join('\n')}` : '');
    if (warnings.length) this.log.log(`Job ${name}: ${warnings.join(' | ')}`);
    this.log.log(`Job ${name}: done in ${seconds} s, sending ${send.map(f => f.name).join(', ') || 'nothing'} (${mb(total)})`);

    if (!send.length) {
      await this.setStatus(job, `${done}\nNo result file came back. The job folder is on the bot PC: \`${dir}\``, COLOR.orange, true);
      return;
    }
    if (total > limit) {
      await this.setStatus(job, `${done}\nThe file${send.length > 1 ? 's are' : ' is'} ${mb(total)}, over the ${this.cfg.discord.maxUploadMB} MB upload limit. `
        + `On the bot PC: \`${send.map(f => f.path).join('`, `')}\``, COLOR.orange, true);
      return;
    }
    const r = await this.rest.uploadFiles(job.req.channelId, {
      content: cut(`<@${job.req.user.id}> ${done}${job.summary ? `\n> ${cut(job.summary, 300)}` : ''}`, 2000),
      message_reference: { message_id: job.req.messageId, fail_if_not_exists: false },
      allowed_mentions: { users: [job.req.user.id], replied_user: true },
    }, send);
    if (!r.ok) {
      this.log.error(`Job ${name}: upload failed (${r.status}):`, r.data);
      await this.setStatus(job, `${done}\nThe upload failed (${r.status}). On the bot PC: \`${send.map(f => f.path).join('`, `')}\``, COLOR.orange, true);
      return;
    }
    // the reply with the file says it all: the status message goes away
    if (job.statusId) await this.rest.deleteMessage(job.req.channelId, job.statusId);
  }

  /** Pictures etc. from the message go into the job folder. A recipe value equal to an attachment's file name becomes
   *  its full path; a video recipe without "image" takes the first picture as its first frame.
   *  @param {Job} job  @param {string} dir */
  async downloadAttachments(job, dir) {
    /** @type {Map<string, string>} */
    const saved = new Map();
    let firstPicture = '';
    for (const a of job.req.attachments) {
      if (/\.json$/i.test(a.filename)) continue;   // the recipe itself
      if (a.size > MAX_ATTACHMENT_BYTES) { this.log.log(`Attachment ${a.filename} skipped: ${mb(a.size)}`); continue; }
      const r = await fetch(a.url);
      if (!r.ok) throw new Error(`Could not download ${a.filename} (${r.status})`);
      const file = path.join(dir, safeName(a.filename));
      fs.writeFileSync(file, Buffer.from(await r.arrayBuffer()));
      saved.set(a.filename, file);
      if (!firstPicture && (/^image\//.test(a.content_type ?? '') || /\.(png|jpe?g|webp)$/i.test(a.filename))) firstPicture = file;
    }
    if (saved.size === 0) return;
    for (const [key, value] of Object.entries(job.recipe))
      if (typeof value === 'string' && saved.has(value)) job.recipe[key] = saved.get(value);
    if (String(job.recipe.graph ?? '').startsWith('video') && job.recipe.image === undefined && firstPicture)
      job.recipe.image = firstPicture;
  }

  /** @param {string} dir  @param {string} zipPath  @param {(rel: string, isDir: boolean) => boolean} [skip] */
  zip(dir, zipPath, skip) {
    const zip = new ZipWriter(zipPath);
    try {
      zip.addFolder(dir, path.basename(dir), skip);
      return zip.close();
    } catch (err) {
      zip.abort();
      throw err;
    }
  }

  // ── Status message ─────────────────────────────────────────────────────────

  /** @param {Job} job  @param {string} text  @param {number} color */
  embed(job, text, color) {
    const fields = [{ name: 'Graph', value: job.label, inline: true }, { name: 'For', value: job.req.user.name, inline: true }];
    if (job.summary) fields.push({ name: 'Request', value: cut(job.summary, 1000), inline: false });
    return { description: cut(text, 4000), color, fields };
  }

  /** final: the job is over, the Stop button goes.  @param {Job} job  @param {string} text  @param {number} color  @param {boolean} [final] */
  async setStatus(job, text, color, final = false) {
    if (!job.statusId) return;
    const r = await this.rest.editMessage(job.req.channelId, job.statusId, { embeds: [this.embed(job, text, color)], ...(final ? { components: [] } : {}) });
    if (!r.ok) this.log.debug(`Discord: status edit failed (${r.status})`);
  }

  // ── Old jobs ───────────────────────────────────────────────────────────────

  /** Deletes job folders and zips older than keepDays (only names the bot makes; never in bDevMode). */
  cleanup() {
    const days = this.cfg.jobs.keepDays;
    if (this.cfg.bDevMode || !(days > 0)) return;
    const before = Date.now() - days * 86_400_000;
    for (const entry of fs.readdirSync(this.cfg.jobs.folder, { withFileTypes: true })) {
      if (!STAMP_RE.test(entry.name)) continue;
      const full = path.join(this.cfg.jobs.folder, entry.name);
      try {
        if (fs.statSync(full).mtimeMs < before) {
          fs.rmSync(full, { recursive: true, force: true });
          this.log.log(`Removed old job ${entry.name}`);
        }
      } catch (err) {
        this.log.error(`Could not remove ${entry.name}:`, err);
      }
    }
  }
}
