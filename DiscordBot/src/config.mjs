// @ts-check
import * as fs from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

// config.json (next to package.json; another file with --config=<path> or BOT_CONFIG), merged over these defaults.
// Paths in it are relative to the bot folder. DISCORD_BOT_TOKEN / DISCORD_CHANNEL_ID override the file, like the
// build machine (nodejsbuildmachine src/handlers/discordGateway.ts).

export const BOT_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

/**
 * @typedef {{
 *   enabled: boolean, botToken: string, channelId: string, commandsEnabled: boolean,
 *   commandAllowedUserIds: string[], commandAllowedRoleIds: string[], commandAllowEveryone: boolean,
 *   commandChannelIds: string[], commandGuildId: string, commandAllowDM: boolean, commandInChannel: boolean,
 *   maxUploadMB: number
 * }} DiscordConfig
 * @typedef {{
 *   host: string, port: number, autoStart: boolean, exePath: string, comfyServer: string, speechServer: string,
 *   extraArgs: string[], startTimeoutSec: number
 * }} ComfyLiveConfig
 * @typedef {{ folder: string, timeoutMin: number, maxQueue: number, keepDays: number }} JobsConfig
 * @typedef {{ bDevMode: boolean, discord: DiscordConfig, comfyLive: ComfyLiveConfig, jobs: JobsConfig }} Config
 */

/** @type {Config} */
const DEFAULTS = {
  bDevMode: false,
  discord: {
    enabled: true,
    botToken: '',
    channelId: '',
    commandsEnabled: true,
    commandAllowedUserIds: [],      // who may use the bot (closed by default: empty lists = nobody)
    commandAllowedRoleIds: [],      // ...or anyone with one of these roles in the server (works in DMs too)
    commandAllowEveryone: false,    // true = anyone who can reach the bot
    commandChannelIds: [],          // channels where @mentions are answered (default: [channelId])
    commandGuildId: '',             // the server (for roles); default: looked up from channelId
    commandAllowDM: true,           // answer direct messages to the bot
    commandInChannel: true,         // answer @mentions of the bot in the command channel(s)
    maxUploadMB: 10,                // Discord's upload limit for this server (10 MB without boosts)
  },
  comfyLive: {
    host: '127.0.0.1',
    port: 8766,                     // ComfyLive --headless default
    autoStart: true,                // start ComfyLive --headless when nothing answers on host:port
    exePath: '../ComfyLive/build/Release/ComfyLive.exe',
    comfyServer: '127.0.0.1:8188',  // --server= for the ComfyLive it starts
    speechServer: '',               // --speech-server= (empty: ComfyLive's default 127.0.0.1:8189)
    extraArgs: [],                  // more ComfyLive options, e.g. ["--comfy-root=C:/ComfyUI"]
    startTimeoutSec: 60,
  },
  jobs: {
    folder: './jobs',               // one folder per request, zipped when done
    timeoutMin: 30,                 // a job running longer is stopped
    maxQueue: 10,                   // more waiting requests are refused
    keepDays: 7,                    // job folders and zips older than this are deleted (0 = keep; bDevMode keeps all)
  },
};

/** @returns {{ config: Config, file: string }} */
export function loadConfig() {
  const arg = process.argv.find(a => a.startsWith('--config='));
  const file = path.resolve(BOT_DIR, arg ? arg.slice('--config='.length) : process.env.BOT_CONFIG || 'config.json');
  if (!fs.existsSync(file))
    throw new Error(`No config file ${file}: copy config.example.json to config.json and fill it in`);

  let raw;
  try {
    // Trailing commas ("[ "123", ]") are forgiven: easy to leave behind when editing lists by hand.
    raw = JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '').replace(/,(\s*[}\]])/g, '$1'));
  } catch (err) {
    throw new Error(`${file} is not valid JSON: ${err instanceof Error ? err.message : err}`);
  }

  /** @type {Config} */
  const config = {
    bDevMode: raw.bDevMode ?? DEFAULTS.bDevMode,
    discord: { ...DEFAULTS.discord, ...raw.discord },
    comfyLive: { ...DEFAULTS.comfyLive, ...raw.comfyLive },
    jobs: { ...DEFAULTS.jobs, ...raw.jobs },
  };
  config.discord.botToken = process.env.DISCORD_BOT_TOKEN || config.discord.botToken;
  config.discord.channelId = process.env.DISCORD_CHANNEL_ID || config.discord.channelId;
  config.comfyLive.exePath = path.resolve(BOT_DIR, config.comfyLive.exePath);
  config.jobs.folder = path.resolve(BOT_DIR, config.jobs.folder);
  return { config, file };
}
