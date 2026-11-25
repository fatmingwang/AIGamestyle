// @ts-check

// The Discord Gateway (WebSocket, built into Node 22): receives messages for the bot.
// Same connection, resume and permission logic as the build machine's src/handlers/discordGateway.ts, minus the
// menu and slash commands. This bot registers NO slash commands on purpose: a guild's command list is replaced as a
// whole, so registering here would remove the build machine's /menu and /build when both use the same bot.
//
// What it answers:
//  • direct messages to the bot (commandAllowDM)
//  • messages that @mention the bot in the command channel(s) (commandInChannel)
//  • clicks and form submits on its own components (custom_id "cl:...", forms.mjs), in those same places
// Messages of both kinds carry their text and attachments without the privileged MESSAGE_CONTENT intent, so nothing
// needs to be switched on in the Developer Portal.

const GATEWAY_URL = 'wss://gateway.discord.gg/?v=10&encoding=json';
const INTENT_GUILDS = 1 << 0;
const INTENT_GUILD_MESSAGES = 1 << 9;
const INTENT_DIRECT_MESSAGES = 1 << 12;
// Close codes after which reconnecting cannot help (bad token, bad intents...).
const FATAL_CLOSE_CODES = new Set([4004, 4010, 4011, 4012, 4013, 4014]);
const OP = { Dispatch: 0, Heartbeat: 1, Identify: 2, Resume: 6, Reconnect: 7, InvalidSession: 9, Hello: 10, HeartbeatAck: 11 };
// Message types a person writes (default, reply); joins, pins etc. are ignored.
const USER_MESSAGE_TYPES = new Set([0, 19]);

/**
 * @typedef {{ id: string, name: string, isAdmin: boolean }} BotUser
 * @typedef {{ id: string, filename: string, url: string, size: number, content_type?: string }} Attachment
 * @typedef {{ user: BotUser, channelId: string, messageId: string, inDM: boolean, text: string, attachments: Attachment[] }} BotRequest
 * @typedef {{ i: any, user: BotUser, customId: string, fields: Record<string, string>, inDM: boolean }} BotInteraction
 */

const INTERACTION_COMPONENT = 3;
const INTERACTION_MODAL_SUBMIT = 5;
const EPHEMERAL = 1 << 6;

export class DiscordGateway {
  /**
   * @param {import('./config.mjs').DiscordConfig} cfg
   * @param {import('./discordRest.mjs').DiscordRest} rest
   * @param {import('./logger.mjs').Logger} log
   * @param {{ onRequest: (req: BotRequest) => Promise<void>, onInteraction: (x: BotInteraction) => Promise<void> }} handlers
   */
  constructor(cfg, rest, log, handlers) {
    this.cfg = cfg;
    this.rest = rest;
    this.log = log;
    this.onRequest = handlers.onRequest;
    this.onInteractionHandler = handlers.onInteraction;
    /** @type {WebSocket | null} */
    this.ws = null;
    /** @type {NodeJS.Timeout | null} */
    this.heartbeatTimer = null;
    this.heartbeatAcked = true;
    /** @type {number | null} */
    this.seq = null;
    /** @type {string | null} */
    this.sessionId = null;
    /** @type {string | null} */
    this.resumeUrl = null;
    /** @type {string | null} */
    this.botId = null;
    this.reconnectDelayMs = 1000;
    this.stopped = false;
    /** @type {string | null} */
    this.guildId = null;
    /** @type {Map<string, { at: number, roles: string[] }>} */
    this.roleCache = new Map();
  }

  start() { this.connect(GATEWAY_URL); }

  stop() {
    this.stopped = true;
    this.stopHeartbeat();
    this.ws?.close(1000, 'bye');
  }

  // ── Gateway connection ─────────────────────────────────────────────────────

  /** @param {string} url */
  connect(url) {
    if (this.stopped) return;
    this.log.log('Discord gateway: connecting...');
    const ws = new WebSocket(url);
    this.ws = ws;
    ws.onmessage = ev => {
      try {
        this.onPayload(JSON.parse(String(ev.data)));
      } catch (err) {
        this.log.error('Discord gateway: bad payload', err);
      }
    };
    ws.onclose = ev => {
      this.stopHeartbeat();
      if (this.ws !== ws) return; // replaced by a newer connection
      this.ws = null;
      if (this.stopped) return;
      if (FATAL_CLOSE_CODES.has(ev.code)) {
        this.log.error(`Discord gateway closed with fatal code ${ev.code} (${ev.reason || 'no reason'}): check the bot token and intents. Not reconnecting.`);
        this.stopped = true;
        return;
      }
      const canResume = !!(this.sessionId && this.resumeUrl);
      const delay = this.reconnectDelayMs;
      this.reconnectDelayMs = Math.min(this.reconnectDelayMs * 2, 60000);
      this.log.log(`Discord gateway closed (${ev.code}); reconnecting in ${delay} ms${canResume ? ' (resume)' : ''}`);
      setTimeout(() => this.connect(canResume ? `${this.resumeUrl}/?v=10&encoding=json` : GATEWAY_URL), delay);
    };
    ws.onerror = () => this.log.error('Discord gateway: socket error');
  }

  /** @param {number} op  @param {unknown} d */
  send(op, d) {
    if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify({ op, d }));
  }

  /** @param {{ op: number, d: any, s: number | null, t: string | null }} p */
  onPayload(p) {
    if (p.s !== null && p.s !== undefined) this.seq = p.s;
    switch (p.op) {
      case OP.Hello:
        this.startHeartbeat(p.d.heartbeat_interval);
        if (this.sessionId && this.resumeUrl) this.send(OP.Resume, { token: this.cfg.botToken, session_id: this.sessionId, seq: this.seq });
        else this.identify();
        break;
      case OP.Heartbeat:
        this.send(OP.Heartbeat, this.seq);
        break;
      case OP.HeartbeatAck:
        this.heartbeatAcked = true;
        break;
      case OP.Reconnect:
        this.ws?.close(4000, 'reconnect requested');
        break;
      case OP.InvalidSession:
        if (!p.d) { this.sessionId = null; this.resumeUrl = null; this.seq = null; }
        setTimeout(() => (p.d ? this.ws?.close(4000, 'invalid session') : this.identify()), 1000 + Math.random() * 4000);
        break;
      case OP.Dispatch:
        this.onDispatch(p.t ?? '', p.d);
        break;
    }
  }

  identify() {
    this.send(OP.Identify, {
      token: this.cfg.botToken,
      intents: INTENT_GUILDS
        | (this.cfg.commandAllowDM !== false ? INTENT_DIRECT_MESSAGES : 0)
        | (this.cfg.commandInChannel !== false ? INTENT_GUILD_MESSAGES : 0),
      properties: { os: process.platform, browser: 'gamestyle-comfylive-bot', device: 'gamestyle-comfylive-bot' },
    });
  }

  /** @param {number} intervalMs */
  startHeartbeat(intervalMs) {
    this.stopHeartbeat();
    this.heartbeatAcked = true;
    const beat = () => {
      if (!this.heartbeatAcked) {
        this.log.log('Discord gateway: heartbeat not acknowledged, reconnecting');
        this.ws?.close(4000, 'heartbeat timeout');
        return;
      }
      this.heartbeatAcked = false;
      this.send(OP.Heartbeat, this.seq);
    };
    // First beat after a random jitter, as the Gateway docs ask.
    this.heartbeatTimer = setTimeout(() => {
      beat();
      this.heartbeatTimer = setInterval(beat, intervalMs);
    }, intervalMs * Math.random());
  }

  stopHeartbeat() {
    if (this.heartbeatTimer) { clearTimeout(this.heartbeatTimer); clearInterval(this.heartbeatTimer); }
    this.heartbeatTimer = null;
  }

  /** @param {string} type  @param {any} d */
  onDispatch(type, d) {
    if (type === 'READY') {
      this.sessionId = d.session_id;
      this.resumeUrl = d.resume_gateway_url;
      this.botId = d.user?.id ?? null;
      this.reconnectDelayMs = 1000;
      this.log.log(`Discord gateway ready as ${d.user?.username} (${this.botId}). DM the bot or @mention it in ${this.allowedChannels().join(', ') || '(no channel)'}.`);
    } else if (type === 'RESUMED') {
      this.reconnectDelayMs = 1000;
      this.log.log('Discord gateway resumed');
    } else if (type === 'MESSAGE_CREATE') {
      this.onMessage(d).catch(err => this.log.error('Discord: message handler crashed:', err));
    } else if (type === 'INTERACTION_CREATE' && (d.type === INTERACTION_COMPONENT || d.type === INTERACTION_MODAL_SUBMIT)) {
      this.onInteraction(d).catch(err => this.log.error('Discord: interaction handler crashed:', err));
    }
  }

  // ── Who may use the bot ────────────────────────────────────────────────────

  allowedChannels() {
    const list = this.cfg.commandChannelIds?.filter(Boolean) ?? [];
    return list.length ? list : [this.cfg.channelId].filter(Boolean);
  }

  async resolveGuildId() {
    if (this.guildId) return this.guildId;
    let guildId = this.cfg.commandGuildId || null;
    if (!guildId && this.allowedChannels()[0]) {
      const ch = await this.rest.call('GET', `/channels/${this.allowedChannels()[0]}`);
      guildId = ch.data?.guild_id ?? null;
    }
    this.guildId = guildId;
    return guildId;
  }

  // Closed by default: only people in commandAllowedUserIds, or with a role in commandAllowedRoleIds.
  // A DM carries no roles, so they are read from the person's membership in the server.
  /** @param {string} userId  @param {string[]} [roles] */
  async isAllowed(userId, roles) {
    if (this.cfg.commandAllowEveryone === true) return true;
    if (!userId) return false;
    if ((this.cfg.commandAllowedUserIds ?? []).includes(userId)) return true;
    const allowedRoles = this.cfg.commandAllowedRoleIds ?? [];
    if (allowedRoles.length === 0) return false;
    const memberRoles = roles ?? await this.memberRoles(userId);
    return memberRoles.some(r => allowedRoles.includes(r));
  }

  /** @param {string} userId  @returns {Promise<string[]>} */
  async memberRoles(userId) {
    const cached = this.roleCache.get(userId);
    if (cached && Date.now() - cached.at < 60_000) return cached.roles;
    const guildId = await this.resolveGuildId();
    if (!guildId) return [];
    const r = await this.rest.call('GET', `/guilds/${guildId}/members/${userId}`);
    const roles = r.ok && Array.isArray(r.data?.roles) ? r.data.roles : [];
    if (!r.ok && r.status !== 404) this.log.error(`Discord: could not read roles of ${userId} (${r.status})`);
    this.roleCache.set(userId, { at: Date.now(), roles });
    return roles;
  }

  // ── Messages ───────────────────────────────────────────────────────────────

  /** @param {any} m */
  async onMessage(m) {
    if (m.author?.bot || m.webhook_id || !USER_MESSAGE_TYPES.has(m.type)) return;
    const inDM = !m.guild_id;
    if (inDM) {
      if (this.cfg.commandAllowDM === false) return;
    } else {
      // channel chat is left alone: only messages that @mention the bot, in the command channel(s)
      if (this.cfg.commandInChannel === false || !this.botId) return;
      if (!(m.mentions ?? []).some((/** @type {any} */ u) => u.id === this.botId)) return;
      if (!this.allowedChannels().includes(m.channel_id)) {
        this.log.log(`Discord: mention from ${m.author?.username} in channel ${m.channel_id} ignored: not a command channel (${this.allowedChannels().join(', ')})`);
        return;
      }
    }

    const id = m.author?.id ?? '';
    /** @type {BotUser} */
    const user = {
      id,
      name: m.member?.nick || m.author?.global_name || m.author?.username || 'discord-user',
      isAdmin: (this.cfg.commandAllowedUserIds ?? []).includes(id),
    };
    const where = inDM ? 'DM' : `channel ${m.channel_id}`;
    if (!(await this.isAllowed(id, m.member?.roles))) {
      this.log.log(`Discord: message in ${where} refused for ${user.name} (${id}): not in allowed users/roles`);
      await this.rest.createMessage(m.channel_id, {
        content: `Sorry, you are not allowed to use this bot. Ask the owner to add your user ID (${id}) to commandAllowedUserIds.`,
        message_reference: { message_id: m.id, fail_if_not_exists: false },
        allowed_mentions: { replied_user: false },
      });
      return;
    }

    // the mention itself is not part of the request
    const text = String(m.content ?? '').replace(new RegExp(`<@!?${this.botId}>`, 'g'), '').trim();
    this.log.log(`Discord: request from ${user.name} (${id}) in ${where}: ${text.slice(0, 200)}${m.attachments?.length ? ` [+${m.attachments.length} attachment(s)]` : ''}`);
    await this.onRequest({ user, channelId: m.channel_id, messageId: m.id, inDM, text, attachments: m.attachments ?? [] });
  }

  // ── Buttons and forms ──────────────────────────────────────────────────────

  /** @param {any} i */
  async onInteraction(i) {
    const customId = String(i.data?.custom_id ?? '');
    if (!customId.startsWith('cl:')) return;   // not ours (e.g. the build machine's "bm:" menu)
    const inDM = !i.guild_id;
    if (inDM ? this.cfg.commandAllowDM === false : !this.allowedChannels().includes(i.channel_id)) return;

    const u = i.member?.user ?? i.user ?? {};
    const id = u.id ?? '';
    /** @type {BotUser} */
    const user = { id, name: i.member?.nick || u.global_name || u.username || 'discord-user', isAdmin: (this.cfg.commandAllowedUserIds ?? []).includes(id) };
    if (!(await this.isAllowed(id, i.member?.roles))) {
      this.log.log(`Discord: ${customId} refused for ${user.name} (${id}): not in allowed users/roles`);
      await this.rest.callback(i, { type: 4, data: { flags: EPHEMERAL, content: `Sorry, you are not allowed to use this bot (your user ID: ${id}).` } });
      return;
    }

    // form fields: rows of text inputs (newer Discord clients may wrap each in a label component)
    /** @type {Record<string, string>} */
    const fields = {};
    /** @param {any[]} nodes */
    const collect = nodes => {
      for (const n of nodes ?? []) {
        if (typeof n?.custom_id === 'string' && typeof n.value === 'string') fields[n.custom_id] = n.value;
        if (n?.components) collect(n.components);
        if (n?.component) collect([n.component]);
      }
    };
    collect(i.data.components);

    this.log.log(`Discord: ${customId} from ${user.name} (${id})${Object.keys(fields).length ? ' ' + JSON.stringify(fields).slice(0, 300) : ''}`);
    await this.onInteractionHandler({ i, user, customId, fields, inDM });
  }
}
