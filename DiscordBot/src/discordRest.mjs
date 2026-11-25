// @ts-check
import * as fs from 'node:fs';

// Discord REST calls (fetch: built into Node 22). Rate limits (429) are waited out and retried.

const API = 'https://discord.com/api/v10';
const USER_AGENT = 'DiscordBot (https://discord.com, 1.0) GameStyleAI-ComfyLive';

/** @typedef {{ ok: boolean, status: number, data: any }} RestResult */

export class DiscordRest {
  /** @param {string} token  @param {import('./logger.mjs').Logger} log */
  constructor(token, log) {
    this.token = token;
    this.log = log;
  }

  /** @param {string} method  @param {string} route  @param {unknown} [body]  @param {boolean} [auth]  @returns {Promise<RestResult>} */
  async call(method, route, body, auth = true) {
    return this.send(route, () => ({
      method,
      headers: {
        'Content-Type': 'application/json',
        'User-Agent': USER_AGENT,
        ...(auth ? { Authorization: `Bot ${this.token}` } : {}),
      },
      body: body === undefined ? undefined : JSON.stringify(body),
    }));
  }

  /** @param {string} route  @param {() => RequestInit} init  @returns {Promise<RestResult>} */
  async send(route, init) {
    for (let attempt = 0; ; attempt++) {
      const res = await fetch(`${API}${route}`, init());
      const text = await res.text();
      let data = null;
      try { data = text ? JSON.parse(text) : null; } catch { data = text; }
      if (res.status === 429 && attempt < 3) {
        const wait = Math.ceil((Number(data?.retry_after) || 1) * 1000) + 100;
        this.log.debug(`Discord rate limit on ${route}: waiting ${wait} ms`);
        await new Promise(r => setTimeout(r, wait));
        continue;
      }
      return { ok: res.ok, status: res.status, data };
    }
  }

  /** @param {string} channelId  @param {object} body */
  createMessage(channelId, body) { return this.call('POST', `/channels/${channelId}/messages`, body); }

  /** @param {string} channelId  @param {string} messageId  @param {object} body */
  editMessage(channelId, messageId, body) { return this.call('PATCH', `/channels/${channelId}/messages/${messageId}`, body); }

  /** @param {string} channelId  @param {string} messageId */
  deleteMessage(channelId, messageId) { return this.call('DELETE', `/channels/${channelId}/messages/${messageId}`); }

  /** Answers an interaction (button, form); must happen within 3 s.  @param {any} i  @param {object} body */
  callback(i, body) { return this.call('POST', `/interactions/${i.id}/${i.token}/callback`, body, false); }

  /** Replaces a deferred interaction answer (allowed for 15 minutes).  @param {any} i  @param {object} body */
  editOriginal(i, body) { return this.call('PATCH', `/webhooks/${i.application_id}/${i.token}/messages/@original`, body, false); }

  /** A message with one file attached.
   *  @param {string} channelId  @param {object} payload  @param {string} filePath  @param {string} fileName  @param {string} [type] */
  async uploadFile(channelId, payload, filePath, fileName, type = 'application/zip') {
    return this.uploadFiles(channelId, payload, [{ path: filePath, name: fileName, type }]);
  }

  /** One message with up to 10 files (Discord shows a picture, plays an MP4 or MP3 in place).
   *  @param {string} channelId  @param {object} payload  @param {{ path: string, name: string, type: string }[]} files */
  async uploadFiles(channelId, payload, files) {
    const all = await Promise.all(files.map(async f => ({ ...f, bytes: await fs.promises.readFile(f.path) })));
    return this.send(`/channels/${channelId}/messages`, () => {
      const form = new FormData();
      form.append('payload_json', JSON.stringify({ ...payload, attachments: all.map((f, i) => ({ id: i, filename: f.name })) }));
      all.forEach((f, i) => form.append(`files[${i}]`, new Blob([f.bytes], { type: f.type }), f.name));
      // no Content-Type: fetch sets multipart/form-data with its boundary
      return { method: 'POST', headers: { 'User-Agent': USER_AGENT, Authorization: `Bot ${this.token}` }, body: form };
    });
  }
}
