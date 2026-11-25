// @ts-check
import * as path from 'node:path';
import { BOT_DIR, loadConfig } from './config.mjs';
import { Logger } from './logger.mjs';
import { DiscordRest } from './discordRest.mjs';
import { DiscordGateway } from './discordGateway.mjs';
import { ComfyLive } from './comfyLive.mjs';
import { JobRunner } from './jobs.mjs';
import { FormStore } from './forms.mjs';
import { BotUi } from './ui.mjs';

// Discord (menu → form → Submit) → ComfyLive → zip → Discord. See README.md.
//   node src/main.mjs [--config=other.json]

const [major, minor] = process.versions.node.split('.').map(Number);
if (major < 22 || (major === 22 && minor < 4)) {
  console.error(`Node ${process.versions.node} is too old: this bot needs Node 22.4 or newer (built-in WebSocket and zlib.crc32).`);
  process.exit(1);
}

let loaded;
try {
  loaded = loadConfig();
} catch (err) {
  console.error(err instanceof Error ? err.message : err);
  process.exit(1);
}
const { config, file } = loaded;
const logDir = path.join(BOT_DIR, 'logs');
const log = new Logger(logDir, config.bDevMode);
log.log(`Config: ${file}${config.bDevMode ? ' (bDevMode: verbose log, job folders kept)' : ''}`);

const d = config.discord;
if (!d.enabled || !d.commandsEnabled) {
  log.error('discord.enabled and discord.commandsEnabled must both be true: nothing to do.');
  process.exit(1);
}
if (!d.botToken || d.botToken.startsWith('PUT-')) {
  log.error('discord.botToken is empty: set it in the config file or in DISCORD_BOT_TOKEN.');
  process.exit(1);
}
if (!d.commandAllowEveryone && !d.commandAllowedUserIds.length && !d.commandAllowedRoleIds.length)
  log.log('Warning: no allowed users or roles: the bot will refuse everyone (set discord.commandAllowedUserIds).');

const rest = new DiscordRest(d.botToken, log);
const comfy = new ComfyLive(config.comfyLive, logDir, log);
const jobs = new JobRunner(config, comfy, rest, log);
const ui = new BotUi(jobs, rest, new FormStore(path.join(BOT_DIR, 'data', 'formValues.json')), log);
const gateway = new DiscordGateway(d, rest, log, {
  onRequest: req => jobs.handle(req).catch(err => log.error('Request failed:', err)),
  onInteraction: x => ui.handle(x).catch(err => log.error('Button/form failed:', err)),
});

log.log(`ComfyLive: ${config.comfyLive.host}:${config.comfyLive.port} (ComfyUI ${config.comfyLive.comfyServer}; ` +
  `${config.comfyLive.autoStart ? `auto-start ${config.comfyLive.exePath}` : 'no auto-start'}). Jobs: ${config.jobs.folder}`);
comfy.probe().then(up => log.log(up ? 'ComfyLive is running.' : 'ComfyLive is not running yet: it starts with the first job.'));
gateway.start();

const quit = () => {
  log.log('Stopping (ComfyLive keeps running).');
  gateway.stop();
  setTimeout(() => process.exit(0), 300);
};
process.on('SIGINT', quit);
process.on('SIGTERM', quit);
